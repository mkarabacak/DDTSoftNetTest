"""
UDP Echo Server - L4 Transport Layer
Echoes back UDP packets for testing.
Recognizes DDTECHO probe messages and tracks probe statistics.
"""

import logging
import socket
import threading
import time

logger = logging.getLogger("udp_echo")

DDTECHO_PREFIX = b"DDTECHO|"


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
        # Probe tracking
        self.probe_count = 0
        self.probe_last_id = None
        self.probe_last_time = None
        self.probe_sources = {}   # addr -> count
        self.lock = threading.Lock()

    def prepare(self, test, args):
        logger.info("UDP prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        with self.lock:
            return (f"packets={self.packet_count},"
                    f"bytes={self.bytes_received},"
                    f"probes={self.probe_count}")

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
        logger.info("UDP echo server on %s:%d (DDTECHO probe aware)",
                     self.local_ip, self.port)

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None

    def _parse_probe(self, data):
        """Parse DDTECHO probe payload.

        Format: DDTECHO|ID=xxxx|TS=xxxxxxxx  (28 bytes)
        Returns (seq_id, timestamp) or None.
        """
        if not data.startswith(DDTECHO_PREFIX):
            return None
        try:
            text = data[:28].decode("ascii", errors="replace")
            parts = text.split("|")
            seq_id = None
            ts = None
            for part in parts:
                if part.startswith("ID="):
                    seq_id = part[3:]
                elif part.startswith("TS="):
                    ts = part[3:]
            return seq_id, ts
        except Exception:
            return None

    def _echo_loop(self):
        """Receive and echo back UDP packets."""
        while self.running:
            try:
                data, addr = self.sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break

            with self.lock:
                self.packet_count += 1
                self.bytes_received += len(data)

            # Check for DDTECHO probe
            probe = self._parse_probe(data)
            if probe is not None:
                seq_id, ts = probe
                with self.lock:
                    self.probe_count += 1
                    self.probe_last_id = seq_id
                    self.probe_last_time = time.time()
                    src_key = addr[0]
                    self.probe_sources[src_key] = \
                        self.probe_sources.get(src_key, 0) + 1

                logger.info("UDP PROBE #%s from %s:%d (total: %d)",
                            seq_id, addr[0], addr[1], self.probe_count)
            else:
                logger.debug("UDP echo %d bytes to %s", len(data), addr)

            try:
                self.sock.sendto(data, addr)
            except OSError:
                pass
