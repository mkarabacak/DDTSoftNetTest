"""
ARP Responder - L2 Data Link Layer
Responds to ARP requests for the companion IP address.
Tracks probe statistics for echo test monitoring.
"""

import logging
import socket
import struct
import threading
import time

logger = logging.getLogger("arp")

ETH_P_ARP = 0x0806
ARP_OP_REQUEST = 1
ARP_OP_REPLY = 2


class ArpResponder:
    """Responds to ARP requests directed at the companion IP."""

    def __init__(self, interface, local_ip):
        self.interface = interface
        self.local_ip = local_ip
        self.sock = None
        self.thread = None
        self.running = False
        self.mac = None
        # Probe tracking
        self.reply_count = 0
        self.reply_last_time = None
        self.reply_sources = {}   # src_ip -> count
        self.lock = threading.Lock()

    def prepare(self, test, args):
        logger.info("ARP prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        with self.lock:
            return f"replies={self.reply_count}"

    def start(self):
        """Start ARP responder on raw socket."""
        try:
            self.sock = socket.socket(
                socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ARP))
            self.sock.bind((self.interface, 0))
            self.sock.settimeout(1.0)
            self.mac = self.sock.getsockname()[4]
        except PermissionError:
            logger.warning("ARP responder needs root - skipping")
            return
        except OSError as e:
            logger.warning("ARP responder socket error: %s", e)
            return

        self.running = True
        self.thread = threading.Thread(target=self._respond_loop, daemon=True)
        self.thread.start()
        logger.info("ARP responder started on %s (probe tracking enabled)",
                     self.interface)

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None

    def _respond_loop(self):
        """Listen for ARP requests and respond."""
        target_ip = socket.inet_aton(self.local_ip)

        while self.running:
            try:
                frame = self.sock.recv(65535)
            except socket.timeout:
                continue
            except OSError:
                break

            if len(frame) < 42:
                continue

            # Parse Ethernet + ARP
            eth_type = struct.unpack("!H", frame[12:14])[0]
            if eth_type != ETH_P_ARP:
                continue

            arp_op = struct.unpack("!H", frame[20:22])[0]
            if arp_op != ARP_OP_REQUEST:
                continue

            # Target IP is at offset 38 (14 eth + 24 arp offset)
            arp_target_ip = frame[38:42]
            if arp_target_ip != target_ip:
                continue

            sender_mac = frame[22:28]
            sender_ip = frame[28:32]
            sender_ip_str = socket.inet_ntoa(sender_ip)
            sender_mac_str = ":".join(f"{b:02x}" for b in sender_mac)

            # Build ARP reply
            reply = self._build_arp_reply(sender_mac, sender_ip, target_ip)
            try:
                self.sock.send(reply)
                with self.lock:
                    self.reply_count += 1
                    self.reply_last_time = time.time()
                    self.reply_sources[sender_ip_str] = \
                        self.reply_sources.get(sender_ip_str, 0) + 1

                logger.info("ARP REPLY to %s (%s) - total: %d",
                            sender_ip_str, sender_mac_str, self.reply_count)
            except OSError:
                pass

    def _build_arp_reply(self, dst_mac, dst_ip, src_ip):
        """Build an ARP reply frame."""
        eth = dst_mac + self.mac + struct.pack("!H", ETH_P_ARP)
        arp = struct.pack("!HHBBH",
                          0x0001,        # Hardware: Ethernet
                          0x0800,        # Protocol: IPv4
                          6,             # HW addr len
                          4,             # Proto addr len
                          ARP_OP_REPLY)  # Operation
        arp += self.mac + src_ip         # Sender MAC + IP
        arp += dst_mac + dst_ip          # Target MAC + IP
        return eth + arp
