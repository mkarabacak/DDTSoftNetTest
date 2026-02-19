"""
TCP Listener - L4 Transport Layer
Multi-port TCP listener for connection testing.
Accepts connections and optionally echoes data.
Recognizes DDTECHO probe messages and tracks probe statistics.
"""

import logging
import socket
import threading
import time

logger = logging.getLogger("tcp")

DDTECHO_PREFIX = b"DDTECHO|"


class TcpListener:
    """Multi-port TCP server for L4 transport layer testing."""

    def __init__(self, local_ip, ports):
        self.local_ip = local_ip
        self.ports = ports
        self.servers = {}
        self.threads = []
        self.running = False
        self.connection_count = 0
        # Probe tracking
        self.probe_count = 0
        self.probe_last_id = None
        self.probe_last_time = None
        self.lock = threading.Lock()

    def prepare(self, test, args):
        logger.info("TCP prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        with self.lock:
            return (f"connections={self.connection_count},"
                    f"probes={self.probe_count}")

    def start(self):
        """Start TCP listeners on all configured ports."""
        self.running = True
        for port in self.ports:
            try:
                srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                srv.settimeout(1.0)
                srv.bind((self.local_ip, port))
                srv.listen(5)
                self.servers[port] = srv

                t = threading.Thread(
                    target=self._accept_loop, args=(srv, port), daemon=True)
                t.start()
                self.threads.append(t)
                logger.info("TCP listening on %s:%d", self.local_ip, port)
            except OSError as e:
                logger.warning("TCP bind %d failed: %s", port, e)

    def stop(self):
        """Stop all TCP listeners."""
        self.running = False
        for port, srv in self.servers.items():
            try:
                srv.close()
            except OSError:
                pass
        self.servers.clear()
        for t in self.threads:
            t.join(timeout=2)
        self.threads.clear()

    def _accept_loop(self, server, port):
        """Accept incoming TCP connections."""
        while self.running:
            try:
                client, addr = server.accept()
            except socket.timeout:
                continue
            except OSError:
                break

            with self.lock:
                self.connection_count += 1
            logger.debug("TCP connection from %s on port %d", addr, port)

            t = threading.Thread(
                target=self._handle_client, args=(client, addr, port),
                daemon=True)
            t.start()

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

    def _handle_client(self, client, addr, port):
        """Handle a single TCP connection with echo behavior."""
        client.settimeout(5.0)
        try:
            # For port 80/8080 - respond with minimal HTTP
            if port in (80, 8080):
                try:
                    request = client.recv(4096)
                    req_str = request.decode("ascii", errors="replace")

                    if "GET" in req_str:
                        path = req_str.split()[1] if len(req_str.split()) > 1 else "/"
                        status, body = self._http_response(path)
                        response = (
                            f"HTTP/1.1 {status}\r\n"
                            f"Content-Length: {len(body)}\r\n"
                            f"Content-Type: text/plain\r\n"
                            f"Server: DDTSoft-Companion/1.0\r\n"
                            f"Connection: close\r\n"
                            f"\r\n"
                            f"{body}"
                        )
                        client.sendall(response.encode("ascii"))
                except (socket.timeout, ConnectionResetError):
                    pass
            else:
                # Echo mode for other ports (22, 443, etc.)
                try:
                    data = client.recv(4096)
                    if data:
                        # Check for DDTECHO probe
                        probe = self._parse_probe(data)
                        if probe is not None:
                            seq_id, ts = probe
                            with self.lock:
                                self.probe_count += 1
                                self.probe_last_id = seq_id
                                self.probe_last_time = time.time()
                            logger.info(
                                "TCP PROBE #%s from %s:%d port %d (total: %d)",
                                seq_id, addr[0], addr[1], port,
                                self.probe_count)
                        else:
                            logger.debug("TCP echo %d bytes from %s port %d",
                                         len(data), addr, port)
                        client.sendall(data)
                except (socket.timeout, ConnectionResetError):
                    pass
        finally:
            client.close()

    def _http_response(self, path):
        """Generate HTTP response based on path."""
        if path == "/" or path == "/index.html":
            return "200 OK", "DDTSoft Test Companion OK"
        elif "404" in path or "nonexistent" in path:
            return "404 Not Found", "Not Found"
        elif "status" in path:
            return "200 OK", f"connections={self.connection_count}"
        else:
            return "200 OK", f"Path: {path}"
