"""
UDP Echo Server - L4 Transport Layer
Echoes back UDP packets for testing.
"""

import logging
import socket
import threading

logger = logging.getLogger("udp_echo")


class UdpEcho:
    """UDP echo server for L4 transport layer testing."""

    def __init__(self, local_ip, port):
        self.local_ip = local_ip
        self.port = port
        self.sock = None
        self.thread = None
        self.running = False
        self.packet_count = 0
        self.bytes_received = 0

    def prepare(self, test, args):
        logger.info("UDP prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        return f"packets={self.packet_count},bytes={self.bytes_received}"

    def start(self):
        """Start UDP echo server."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.settimeout(1.0)
            self.sock.bind((self.local_ip, self.port))
        except OSError as e:
            logger.warning("UDP bind %d failed: %s", self.port, e)
            return

        self.running = True
        self.thread = threading.Thread(target=self._echo_loop, daemon=True)
        self.thread.start()
        logger.info("UDP echo server on %s:%d", self.local_ip, self.port)

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None

    def _echo_loop(self):
        """Receive and echo back UDP packets."""
        while self.running:
            try:
                data, addr = self.sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break

            self.packet_count += 1
            self.bytes_received += len(data)

            try:
                self.sock.sendto(data, addr)
                logger.debug("UDP echo %d bytes to %s", len(data), addr)
            except OSError:
                pass
