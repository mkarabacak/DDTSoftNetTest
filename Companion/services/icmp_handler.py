"""
ICMP Handler - L3 Network Layer
Handles ICMP echo requests (ping) and provides custom TTL responses.
The kernel normally handles ICMP, but this module enables monitoring
and custom behavior when needed.
"""

import logging
import os
import socket
import struct
import threading

logger = logging.getLogger("icmp")

ICMP_ECHO_REQUEST = 8
ICMP_ECHO_REPLY = 0
ETH_P_IP = 0x0800


class IcmpHandler:
    """Monitors and optionally handles ICMP traffic."""

    def __init__(self, interface, local_ip):
        self.interface = interface
        self.local_ip = local_ip
        self.running = False
        self.thread = None
        self.echo_count = 0
        self.custom_ttl = None

    def prepare(self, test, args):
        """Prepare for an ICMP test."""
        test_upper = test.upper()
        if "TTL" in test_upper and args:
            try:
                self.custom_ttl = int(args.split()[0])
                logger.info("Custom TTL set to %d", self.custom_ttl)
            except (ValueError, IndexError):
                self.custom_ttl = None
        return True, "OK"

    def stop_test(self):
        self.custom_ttl = None

    def get_result(self):
        return f"echo_count={self.echo_count}"

    def start(self):
        """Enable kernel ICMP reply and start monitoring."""
        self._set_kernel_icmp(True)
        self.running = True
        self.echo_count = 0
        self.thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()
        logger.info("ICMP handler started (kernel replies enabled)")

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)

    def _set_kernel_icmp(self, enable):
        """Enable/disable kernel ICMP echo replies."""
        val = "0" if enable else "1"
        try:
            with open("/proc/sys/net/ipv4/icmp_echo_ignore_all", "w") as f:
                f.write(val)
        except PermissionError:
            logger.warning("Cannot set icmp_echo_ignore_all (need root)")
        except FileNotFoundError:
            pass

    def _monitor_loop(self):
        """Monitor ICMP traffic for statistics."""
        try:
            sock = socket.socket(
                socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP)
            sock.settimeout(1.0)
        except PermissionError:
            logger.warning("ICMP monitor needs root - skipping")
            return
        except OSError as e:
            logger.warning("ICMP socket error: %s", e)
            return

        while self.running:
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break

            if len(data) < 28:
                continue

            # IP header length
            ihl = (data[0] & 0x0F) * 4
            if len(data) < ihl + 8:
                continue

            icmp_type = data[ihl]
            if icmp_type == ICMP_ECHO_REQUEST:
                self.echo_count += 1
                logger.debug("ICMP echo request from %s (#%d)",
                             addr[0], self.echo_count)

        sock.close()
