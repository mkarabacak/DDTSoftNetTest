"""
DHCP Manager - L7 Application Layer
Minimal DHCP server for testing DHCP discovery and lease operations.
Uses dnsmasq if available, otherwise provides a stub DHCP via raw UDP.
"""

import logging
import os
import signal
import socket
import struct
import subprocess
import threading

logger = logging.getLogger("dhcp")

DHCP_SERVER_PORT = 67
DHCP_CLIENT_PORT = 68

# DHCP message types
DHCP_DISCOVER = 1
DHCP_OFFER = 2
DHCP_REQUEST = 3
DHCP_ACK = 5


class DhcpManager:
    """DHCP server manager - uses dnsmasq or built-in minimal server."""

    def __init__(self, interface, local_ip, pool_start, pool_end,
                 lease_time, domain):
        self.interface = interface
        self.local_ip = local_ip
        self.pool_start = pool_start
        self.pool_end = pool_end
        self.lease_time = lease_time
        self.domain = domain
        self.dnsmasq_proc = None
        self.sock = None
        self.thread = None
        self.running = False
        self.offers_sent = 0
        self._next_ip_offset = 0

    def prepare(self, test, args):
        logger.info("DHCP prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        return f"offers={self.offers_sent}"

    def start(self):
        """Start DHCP server (try dnsmasq first, fall back to built-in)."""
        if self._start_dnsmasq():
            return
        self._start_builtin()

    def stop(self):
        """Stop DHCP server."""
        self.running = False
        if self.dnsmasq_proc:
            try:
                self.dnsmasq_proc.terminate()
                self.dnsmasq_proc.wait(timeout=3)
            except Exception:
                try:
                    self.dnsmasq_proc.kill()
                except Exception:
                    pass
            self.dnsmasq_proc = None
            logger.info("dnsmasq stopped")

        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None

    def _start_dnsmasq(self):
        """Try to start dnsmasq for DHCP service."""
        try:
            subprocess.check_call(
                ["which", "dnsmasq"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except (subprocess.CalledProcessError, FileNotFoundError):
            logger.info("dnsmasq not found, using built-in DHCP")
            return False

        cmd = [
            "dnsmasq",
            "--no-daemon",
            f"--interface={self.interface}",
            f"--dhcp-range={self.pool_start},{self.pool_end},{self.lease_time}",
            f"--domain={self.domain}",
            "--bind-interfaces",
            "--log-dhcp",
            "--no-resolv",
            "--no-hosts",
            f"--listen-address={self.local_ip}",
        ]

        try:
            self.dnsmasq_proc = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                preexec_fn=os.setpgrp)
            logger.info("dnsmasq started: %s", " ".join(cmd))
            return True
        except (OSError, subprocess.SubprocessError) as e:
            logger.warning("Failed to start dnsmasq: %s", e)
            return False

    def _start_builtin(self):
        """Start built-in minimal DHCP server."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self.sock.settimeout(1.0)
            self.sock.bind(("0.0.0.0", DHCP_SERVER_PORT))
        except OSError as e:
            logger.warning("DHCP bind failed (need root?): %s", e)
            return

        self.running = True
        self.thread = threading.Thread(target=self._dhcp_loop, daemon=True)
        self.thread.start()
        logger.info("Built-in DHCP server started on port %d", DHCP_SERVER_PORT)

    def _dhcp_loop(self):
        """Handle DHCP requests."""
        while self.running:
            try:
                data, addr = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break

            if len(data) < 240:
                continue

            msg_type = self._get_dhcp_msg_type(data)
            if msg_type == DHCP_DISCOVER:
                self._send_offer(data, addr)
            elif msg_type == DHCP_REQUEST:
                self._send_ack(data, addr)

    def _get_dhcp_msg_type(self, data):
        """Extract DHCP message type from options."""
        # Options start at offset 240
        i = 240
        while i < len(data) - 2:
            opt = data[i]
            if opt == 0xFF:
                break
            if opt == 0:
                i += 1
                continue
            length = data[i + 1]
            if opt == 53 and length == 1:
                return data[i + 2]
            i += 2 + length
        return 0

    def _next_offer_ip(self):
        """Get next IP from pool."""
        parts = self.pool_start.split(".")
        base = int(parts[3])
        ip = f"{parts[0]}.{parts[1]}.{parts[2]}.{base + self._next_ip_offset}"
        self._next_ip_offset = (self._next_ip_offset + 1) % 100
        return ip

    def _send_offer(self, request, addr):
        """Send DHCP OFFER."""
        offer_ip = self._next_offer_ip()
        response = self._build_dhcp_response(request, offer_ip, DHCP_OFFER)
        try:
            self.sock.sendto(response, ("255.255.255.255", DHCP_CLIENT_PORT))
            self.offers_sent += 1
            logger.debug("DHCP OFFER: %s", offer_ip)
        except OSError:
            pass

    def _send_ack(self, request, addr):
        """Send DHCP ACK."""
        offer_ip = self._next_offer_ip()
        response = self._build_dhcp_response(request, offer_ip, DHCP_ACK)
        try:
            self.sock.sendto(response, ("255.255.255.255", DHCP_CLIENT_PORT))
            logger.debug("DHCP ACK: %s", offer_ip)
        except OSError:
            pass

    def _build_dhcp_response(self, request, offer_ip, msg_type):
        """Build a minimal DHCP response packet."""
        xid = request[4:8]
        client_mac = request[28:34]

        resp = bytearray(300)
        resp[0] = 2           # BOOTREPLY
        resp[1] = 1           # Ethernet
        resp[2] = 6           # HW addr len
        resp[3] = 0           # Hops
        resp[4:8] = xid
        resp[16:20] = socket.inet_aton(offer_ip)      # yiaddr
        resp[20:24] = socket.inet_aton(self.local_ip)  # siaddr
        resp[28:34] = client_mac

        # Magic cookie
        resp[236:240] = b'\x63\x82\x53\x63'

        # Options
        i = 240
        # Message type
        resp[i:i+3] = bytes([53, 1, msg_type])
        i += 3
        # Server identifier
        resp[i:i+6] = bytes([54, 4]) + socket.inet_aton(self.local_ip)
        i += 6
        # Lease time
        resp[i:i+6] = bytes([51, 4]) + struct.pack("!I", self.lease_time)
        i += 6
        # Subnet mask
        resp[i:i+6] = bytes([1, 4]) + socket.inet_aton("255.255.255.0")
        i += 6
        # Router
        resp[i:i+6] = bytes([3, 4]) + socket.inet_aton(self.local_ip)
        i += 6
        # DNS server
        resp[i:i+6] = bytes([6, 4]) + socket.inet_aton(self.local_ip)
        i += 6
        # End
        resp[i] = 0xFF

        return bytes(resp)
