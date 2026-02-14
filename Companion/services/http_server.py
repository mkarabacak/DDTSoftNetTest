"""
HTTP Server - L7 Application Layer
Mini HTTP server for testing HTTP GET, status codes, and basic connectivity.
"""

import logging
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler

logger = logging.getLogger("http")


class _DDTSoftHandler(BaseHTTPRequestHandler):
    """HTTP request handler for DDTSoft companion."""

    server_version = "DDTSoft-Companion/1.0"

    def log_message(self, format, *args):
        logger.debug("HTTP %s", format % args)

    def do_GET(self):
        path = self.path

        if path == "/" or path == "/index.html":
            self._respond(200, "DDTSoft Test Companion OK\n")
        elif "404" in path or "nonexistent" in path:
            self._respond(404, "Not Found\n")
        elif path == "/status":
            self._respond(200, "status=running\nversion=1.0\n")
        elif path == "/health":
            self._respond(200, "healthy\n")
        elif path == "/echo":
            self._respond(200, f"path={path}\nmethod=GET\n")
        else:
            self._respond(200, f"DDTSoft Companion: {path}\n")

    def do_HEAD(self):
        self._respond(200, "", send_body=False)

    def _respond(self, code, body, content_type="text/plain", send_body=True):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Server", self.server_version)
        self.end_headers()
        if send_body and body:
            self.wfile.write(body.encode("utf-8"))


class HttpServer:
    """HTTP server for L7 application layer testing."""

    def __init__(self, local_ip, port):
        self.local_ip = local_ip
        self.port = port
        self.httpd = None
        self.thread = None
        self.running = False

    def prepare(self, test, args):
        logger.info("HTTP prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        return "running" if self.running else "stopped"

    def start(self):
        """Start HTTP server."""
        try:
            self.httpd = HTTPServer(
                (self.local_ip, self.port), _DDTSoftHandler)
            self.httpd.timeout = 1.0
        except OSError as e:
            logger.warning("HTTP bind %s:%d failed: %s",
                           self.local_ip, self.port, e)
            return

        self.running = True
        self.thread = threading.Thread(target=self._serve_loop, daemon=True)
        self.thread.start()
        logger.info("HTTP server on %s:%d", self.local_ip, self.port)

    def stop(self):
        self.running = False
        if self.httpd:
            self.httpd.shutdown()
            self.httpd = None
        if self.thread:
            self.thread.join(timeout=3)

    def _serve_loop(self):
        """Serve HTTP requests."""
        while self.running:
            self.httpd.handle_request()
