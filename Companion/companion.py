#!/usr/bin/env python3
"""
DDTSoft Test Companion - Main Entry Point

Linux-side companion application for the DDTSoft EFI Network Test & OSI Analyzer.
Coordinates with the EFI application via UDP control channel (port 9999).

Protocol:
  Commands (EFI -> Companion): HELLO, PREPARE, START, STOP, RESULT, DONE, GETREPORT
  Responses (Companion -> EFI): ACK, READY, ERROR, REPORT, CONFIRM

Usage:
  sudo python3 companion.py [--config CONFIG] [--interface IFACE] [--ip IP]
"""

import argparse
import configparser
import logging
import os
import signal
import subprocess
import sys
import threading
import time

from services.control_server import ControlServer
from services.link_control import LinkControl
from services.arp_responder import ArpResponder
from services.icmp_handler import IcmpHandler
from services.tcp_listener import TcpListener
from services.udp_echo import UdpEcho
from services.dhcp_manager import DhcpManager
from services.dns_manager import DnsManager
from services.http_server import HttpServer
from services.frame_generator import FrameGenerator
from capture.packet_capture import PacketCapture

APP_NAME = "DDTSoft Test Companion"
APP_VERSION = "1.0.0"

LOG_FORMAT = "%(asctime)s [%(levelname)-7s] %(name)-18s: %(message)s"
LOG_DATE = "%H:%M:%S"

logger = logging.getLogger("companion")


class CompanionApp:
    """Main companion application orchestrator."""

    def __init__(self, config_path=None, interface=None, local_ip=None):
        self.config = self._load_config(config_path)
        if interface:
            self.config["interface"] = interface
        if local_ip:
            self.config["local_ip"] = local_ip

        self.running = False
        self.services = {}
        self.control = None

    def _load_config(self, path):
        """Load configuration from file or return defaults."""
        defaults = {
            "interface": "eth0",
            "local_ip": "192.168.100.1",
            "subnet_mask": "255.255.255.0",
            "dut_ip": "192.168.100.10",
            "control_port": "9999",
            "dhcp_pool_start": "192.168.100.100",
            "dhcp_pool_end": "192.168.100.200",
            "dhcp_lease_time": "3600",
            "dhcp_domain": "test.ddtsoft.local",
            "dns_port": "53",
            "dns_domain": "test.ddtsoft.local",
            "http_port": "80",
            "tcp_ports": "80,443,8080,22",
            "udp_echo_port": "5000",
            "udp_ports": "5000,5001,5002",
            "command_timeout": "10",
        }

        if path and os.path.exists(path):
            cp = configparser.ConfigParser()
            cp.read_string("[companion]\n" + open(path).read())
            for key, val in cp.items("companion"):
                defaults[key] = val
            logger.info("Loaded config from %s", path)

        return defaults

    def _ensure_interface_ip(self):
        """Assign the configured IP to the interface if not already present."""
        iface = self.config["interface"]
        ip = self.config["local_ip"]
        mask = self.config["subnet_mask"]
        self._ip_was_added = False

        # Check if IP is already assigned
        try:
            result = subprocess.run(
                ["ip", "addr", "show", "dev", iface],
                capture_output=True, text=True, timeout=5,
            )
            if f"inet {ip}/" in result.stdout:
                logger.info("IP %s already assigned to %s", ip, iface)
                return True
        except Exception as e:
            logger.warning("Failed to check interface: %s", e)

        # Calculate CIDR prefix from subnet mask
        prefix = sum(bin(int(x)).count("1") for x in mask.split("."))

        # Bring interface up and add IP
        logger.info("Configuring %s with %s/%d", iface, ip, prefix)
        try:
            subprocess.run(["ip", "link", "set", iface, "up"],
                           check=True, timeout=5)
            subprocess.run(["ip", "addr", "add", f"{ip}/{prefix}", "dev", iface],
                           check=True, timeout=5)
            self._ip_was_added = True
            logger.info("IP %s/%d assigned to %s", ip, prefix, iface)
            # Small delay for interface to be ready
            time.sleep(0.5)
            return True
        except subprocess.CalledProcessError as e:
            logger.error("Failed to configure interface: %s", e)
            print(f"  [!] Failed to assign {ip}/{prefix} to {iface}")
            print(f"  [!] Try manually: sudo ip addr add {ip}/{prefix} dev {iface}")
            return False

    def _cleanup_interface_ip(self):
        """Remove the IP we added (if we added it)."""
        if not getattr(self, "_ip_was_added", False):
            return
        iface = self.config["interface"]
        ip = self.config["local_ip"]
        mask = self.config["subnet_mask"]
        prefix = sum(bin(int(x)).count("1") for x in mask.split("."))
        try:
            subprocess.run(["ip", "addr", "del", f"{ip}/{prefix}", "dev", iface],
                           timeout=5)
            logger.info("Removed %s/%d from %s", ip, prefix, iface)
        except Exception:
            pass

    def _init_services(self):
        """Initialize all service modules."""
        iface = self.config["interface"]
        ip = self.config["local_ip"]

        self.services["link_control"] = LinkControl(iface)
        self.services["arp_responder"] = ArpResponder(iface, ip)
        self.services["icmp_handler"] = IcmpHandler(iface, ip)
        self.services["frame_generator"] = FrameGenerator(iface, ip)
        self.services["packet_capture"] = PacketCapture(iface)

        tcp_ports = [int(p) for p in self.config["tcp_ports"].split(",")]
        self.services["tcp_listener"] = TcpListener(ip, tcp_ports)

        udp_port = int(self.config["udp_echo_port"])
        self.services["udp_echo"] = UdpEcho(ip, udp_port)

        self.services["dhcp_manager"] = DhcpManager(
            iface, ip,
            self.config["dhcp_pool_start"],
            self.config["dhcp_pool_end"],
            int(self.config["dhcp_lease_time"]),
            self.config["dhcp_domain"],
        )

        self.services["dns_manager"] = DnsManager(
            ip,
            int(self.config["dns_port"]),
            self.config["dns_domain"],
        )

        http_port = int(self.config["http_port"])
        self.services["http_server"] = HttpServer(ip, http_port)

        logger.info("All services initialized")

    def _start_services(self):
        """Start background services that run continuously."""
        for name in ("arp_responder", "icmp_handler", "tcp_listener",
                     "udp_echo", "dhcp_manager", "dns_manager", "http_server"):
            svc = self.services.get(name)
            if svc:
                try:
                    svc.start()
                    logger.info("Started: %s", name)
                except Exception as e:
                    logger.warning("Failed to start %s: %s", name, e)

    def _stop_services(self):
        """Stop all running services."""
        for name, svc in self.services.items():
            try:
                svc.stop()
                logger.debug("Stopped: %s", name)
            except Exception:
                pass

    def _handle_prepare(self, layer, test, args):
        """Handle PREPARE command from EFI."""
        logger.info("PREPARE layer=%s test=%s args=%s", layer, test, args)

        layer_upper = layer.upper()

        if layer_upper == "L1":
            lc = self.services.get("link_control")
            if lc:
                return lc.prepare(test, args)
        elif layer_upper == "L2":
            if "ARP" in test.upper():
                svc = self.services.get("arp_responder")
                if svc:
                    return svc.prepare(test, args)
            fg = self.services.get("frame_generator")
            if fg:
                return fg.prepare(test, args)
        elif layer_upper == "L3":
            svc = self.services.get("icmp_handler")
            if svc:
                return svc.prepare(test, args)
        elif layer_upper == "L4":
            if "UDP" in test.upper():
                svc = self.services.get("udp_echo")
            else:
                svc = self.services.get("tcp_listener")
            if svc:
                return svc.prepare(test, args)
        elif layer_upper == "L7":
            if "DHCP" in test.upper():
                svc = self.services.get("dhcp_manager")
            elif "DNS" in test.upper():
                svc = self.services.get("dns_manager")
            elif "HTTP" in test.upper():
                svc = self.services.get("http_server")
            else:
                return False, "Unknown L7 test"
            if svc:
                return svc.prepare(test, args)

        return True, "OK"

    def _handle_start(self):
        """Handle START command."""
        logger.info("START received")
        return True

    def _handle_stop(self):
        """Handle STOP command."""
        logger.info("STOP received")
        for svc in self.services.values():
            try:
                svc.stop_test()
            except AttributeError:
                pass
        return True

    def _handle_result(self):
        """Handle RESULT command, collect results from services."""
        results = []
        for name, svc in self.services.items():
            try:
                r = svc.get_result()
                if r:
                    results.append(f"{name}={r}")
            except AttributeError:
                pass
        return ";".join(results) if results else "OK"

    def run(self):
        """Main run loop."""
        print(f"\n  {'=' * 56}")
        print(f"  {APP_NAME} v{APP_VERSION}")
        print(f"  EFI Network Test & OSI Layer Analyzer - Linux Side")
        print(f"  {'=' * 56}")
        print(f"  Interface : {self.config['interface']}")
        print(f"  IP        : {self.config['local_ip']}")
        print(f"  Control   : UDP port {self.config['control_port']}")
        print(f"  {'-' * 56}")
        print(f"  Echo Probe Services:")
        print(f"    ARP   : Auto-reply to ARP requests")
        print(f"    ICMP  : Kernel echo reply (ID=0xDD50 tracking)")
        print(f"    UDP   : Echo on port {self.config['udp_echo_port']}")
        print(f"    TCP   : Echo on ports {self.config['tcp_ports']}")
        print(f"  {'=' * 56}\n")

        if not self._ensure_interface_ip():
            print("  [!] Cannot continue without interface IP. Exiting.")
            return

        self._init_services()
        self._start_services()

        self.control = ControlServer(
            local_ip=self.config["local_ip"],
            port=int(self.config["control_port"]),
            on_prepare=self._handle_prepare,
            on_start=self._handle_start,
            on_stop=self._handle_stop,
            on_result=self._handle_result,
        )

        self.running = True

        def signal_handler(sig, frame):
            logger.info("Shutdown signal received")
            self.running = False

        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)

        logger.info("Waiting for EFI application connection...")
        print("  [*] Waiting for EFI application on UDP %s:%s" % (
            self.config["local_ip"], self.config["control_port"]))
        print("  [*] Press Ctrl+C to stop\n")

        try:
            self.control.start()
            while self.running:
                time.sleep(0.5)
        except KeyboardInterrupt:
            pass
        finally:
            logger.info("Shutting down...")
            self.control.stop()
            self._stop_services()
            self._cleanup_interface_ip()
            print("\n  [*] DDTSoft Test Companion stopped.")


def main():
    parser = argparse.ArgumentParser(
        description=f"{APP_NAME} v{APP_VERSION}")
    parser.add_argument(
        "--config", "-c",
        default=os.path.join(os.path.dirname(__file__), "config", "default.conf"),
        help="Path to configuration file")
    parser.add_argument(
        "--interface", "-i",
        default=None,
        help="Network interface (overrides config)")
    parser.add_argument(
        "--ip",
        default=None,
        help="Local IP address (overrides config)")
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable verbose logging")

    args = parser.parse_args()

    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(format=LOG_FORMAT, datefmt=LOG_DATE, level=level)

    if os.geteuid() != 0:
        print("  [!] Warning: Some features require root privileges.")
        print("  [!] Run with: sudo python3 companion.py\n")

    app = CompanionApp(
        config_path=args.config,
        interface=args.interface,
        local_ip=args.ip,
    )
    app.run()


if __name__ == "__main__":
    main()
