"""
Frame Generator - L2 Data Link Layer
Generates raw Ethernet frames for L2 testing.
"""

import logging
import socket
import struct
import threading

logger = logging.getLogger("frame_gen")

ETH_P_ALL = 0x0003


class FrameGenerator:
    """Raw frame generator for L2 data link layer testing."""

    def __init__(self, interface, local_ip):
        self.interface = interface
        self.local_ip = local_ip
        self.sock = None
        self.mac = None
        self.running = False

    def prepare(self, test, args):
        logger.info("Frame prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        return None

    def start(self):
        """Initialize raw socket for frame generation."""
        try:
            self.sock = socket.socket(
                socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
            self.sock.bind((self.interface, 0))
            self.mac = self.sock.getsockname()[4]
            logger.info("Frame generator ready on %s", self.interface)
        except PermissionError:
            logger.warning("Frame generator needs root - skipping")
        except OSError as e:
            logger.warning("Frame generator socket error: %s", e)

    def stop(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_frame(self, dst_mac, ether_type, payload):
        """Send a raw Ethernet frame."""
        if not self.sock or not self.mac:
            return False

        frame = dst_mac + self.mac + struct.pack("!H", ether_type) + payload
        try:
            self.sock.send(frame)
            return True
        except OSError as e:
            logger.error("Frame send failed: %s", e)
            return False

    def send_broadcast(self, ether_type, payload):
        """Send a broadcast frame."""
        return self.send_frame(b"\xff\xff\xff\xff\xff\xff", ether_type, payload)
