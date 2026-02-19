"""
ICMP Handler - L3 Network Layer
Handles ICMP echo requests (ping) and provides custom TTL responses.
The kernel normally handles ICMP, but this module enables monitoring
and custom behavior when needed.
Tracks probe statistics including DDTECHO identifier detection.
"""

import logging
import os
import socket
import struct
import threading
import time

logger = logging.getLogger("icmp")

ICMP_ECHO_REQUEST = 8
ICMP_ECHO_REPLY = 0
ETH_P_IP = 0x0800

# DDTECHO ICMP probe uses Identifier = 0xDD50
DDTECHO_ICMP_ID = 0xDD50


class IcmpHandler:
    """Monitors and optionally handles ICMP traffic."""

    def __init__(self, interface, local_ip):
        self.interface = interface
        self.local_ip = local_ip
        self.running = False
        self.thread = None
        self.echo_count = 0
        self.custom_ttl = None
        # Probe tracking
        self.probe_count = 0
        self.probe_last_seq = None
        self.probe_last_time = None
        self.probe_sources = {}   # src_ip -> count
        self.lock = threading.Lock()

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
        with self.lock:
            return f"echo_count={self.echo_count},probes={self.probe_count}"

    def start(self):
        """Enable kernel ICMP reply and start monitoring."""
        self._set_kernel_icmp(True)
        self.running = True
        self.echo_count = 0
        self.probe_count = 0
        self.thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()
        logger.info("ICMP handler started (kernel replies enabled, "
                     "DDTECHO ID=0x%04X tracking)", DDTECHO_ICMP_ID)

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

                # Parse ICMP Identifier and Sequence Number
                icmp_id = struct.unpack("!H", data[ihl + 4:ihl + 6])[0]
                icmp_seq = struct.unpack("!H", data[ihl + 6:ihl + 8])[0]

                # Check if this is a DDTECHO probe
                if icmp_id == DDTECHO_ICMP_ID:
                    with self.lock:
                        self.probe_count += 1
                        self.probe_last_seq = icmp_seq
                        self.probe_last_time = time.time()
                        self.probe_sources[addr[0]] = \
                            self.probe_sources.get(addr[0], 0) + 1

                    # Check for DDTECHO payload
                    payload_info = ""
                    if len(data) >= ihl + 8 + 7:
                        payload = data[ihl + 8:ihl + 8 + 28]
                        try:
                            text = payload.decode("ascii", errors="replace")
                            if text.startswith("DDTECHO|"):
                                payload_info = f" [{text.rstrip(chr(0))}]"
                        except Exception:
                            pass

                    logger.info(
                        "ICMP PROBE seq=%d from %s (total: %d)%s",
                        icmp_seq, addr[0], self.probe_count, payload_info)
                else:
                    logger.debug("ICMP echo request from %s id=0x%04X "
                                 "seq=%d (#%d)",
                                 addr[0], icmp_id, icmp_seq, self.echo_count)

        sock.close()
