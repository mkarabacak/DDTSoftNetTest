"""
Packet Capture Module
Captures packets on the test interface for validation and analysis.
"""

import logging
import socket
import struct
import threading
import time

logger = logging.getLogger("capture")

ETH_P_ALL = 0x0003


class PacketCapture:
    """Packet capture and statistics collector."""

    def __init__(self, interface):
        self.interface = interface
        self.sock = None
        self.thread = None
        self.running = False

        # Statistics
        self.total_packets = 0
        self.total_bytes = 0
        self.arp_count = 0
        self.icmp_count = 0
        self.tcp_count = 0
        self.udp_count = 0
        self.other_count = 0

        self.lock = threading.Lock()

    def prepare(self, test, args):
        return True, "OK"

    def stop_test(self):
        self.reset_stats()

    def get_result(self):
        with self.lock:
            return (f"total={self.total_packets},"
                    f"arp={self.arp_count},"
                    f"icmp={self.icmp_count},"
                    f"tcp={self.tcp_count},"
                    f"udp={self.udp_count}")

    def start(self):
        """Start packet capture."""
        try:
            self.sock = socket.socket(
                socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
            self.sock.bind((self.interface, 0))
            self.sock.settimeout(1.0)
        except PermissionError:
            logger.warning("Packet capture needs root - skipping")
            return
        except OSError as e:
            logger.warning("Capture socket error: %s", e)
            return

        self.running = True
        self.thread = threading.Thread(target=self._capture_loop, daemon=True)
        self.thread.start()
        logger.info("Packet capture started on %s", self.interface)

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None

    def reset_stats(self):
        """Reset all counters."""
        with self.lock:
            self.total_packets = 0
            self.total_bytes = 0
            self.arp_count = 0
            self.icmp_count = 0
            self.tcp_count = 0
            self.udp_count = 0
            self.other_count = 0

    def _capture_loop(self):
        """Capture packets and update statistics."""
        while self.running:
            try:
                data = self.sock.recv(65535)
            except socket.timeout:
                continue
            except OSError:
                break

            if len(data) < 14:
                continue

            with self.lock:
                self.total_packets += 1
                self.total_bytes += len(data)

                ether_type = struct.unpack("!H", data[12:14])[0]
                if ether_type == 0x0806:
                    self.arp_count += 1
                elif ether_type == 0x0800 and len(data) >= 24:
                    protocol = data[23]
                    if protocol == 1:
                        self.icmp_count += 1
                    elif protocol == 6:
                        self.tcp_count += 1
                    elif protocol == 17:
                        self.udp_count += 1
                    else:
                        self.other_count += 1
                else:
                    self.other_count += 1
