"""
Control Channel Server - UDP port 9999
Handles the text-based protocol between EFI app and companion.

Protocol:
  EFI -> Companion: HELLO, PREPARE <layer> <test> [args], START, STOP, RESULT, DONE, GETREPORT
  Companion -> EFI: ACK, READY, ERROR, REPORT, CONFIRM
"""

import logging
import socket
import threading

logger = logging.getLogger("control")


class ControlServer:
    """UDP control channel server for EFI <-> Companion coordination."""

    def __init__(self, local_ip, port, on_prepare, on_start, on_stop, on_result):
        self.local_ip = local_ip
        self.port = port
        self.on_prepare = on_prepare
        self.on_start = on_start
        self.on_stop = on_stop
        self.on_result = on_result

        self.sock = None
        self.thread = None
        self.running = False
        self.connected = False
        self.dut_addr = None

    def start(self):
        """Start listening on the control channel."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.local_ip, self.port))
        self.sock.settimeout(1.0)

        self.running = True
        self.thread = threading.Thread(target=self._listen_loop, daemon=True)
        self.thread.start()
        logger.info("Control server listening on %s:%d", self.local_ip, self.port)

    def stop(self):
        """Stop the control server."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None
        logger.info("Control server stopped")

    def _send(self, message, addr):
        """Send a response to the EFI application."""
        if self.sock and addr:
            try:
                data = message.encode("ascii")
                self.sock.sendto(data, addr)
                logger.debug("TX -> %s: %s", addr, message.strip())
            except Exception as e:
                logger.error("Send failed: %s", e)

    def _listen_loop(self):
        """Main receive loop."""
        while self.running:
            try:
                data, addr = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                if self.running:
                    logger.error("Socket error in listen loop")
                break

            msg = data.decode("ascii", errors="replace").strip()
            if not msg:
                continue

            logger.debug("RX <- %s: %s", addr, msg)
            self._handle_command(msg, addr)

    def _handle_command(self, msg, addr):
        """Parse and dispatch a command."""
        parts = msg.split()
        cmd = parts[0].upper() if parts else ""

        if cmd == "HELLO":
            self.dut_addr = addr
            self.connected = True
            version = " ".join(parts[1:]) if len(parts) > 1 else "unknown"
            logger.info("HELLO from %s (version: %s)", addr, version)
            self._send("ACK DDTSoft Companion 1.0\n", addr)

        elif cmd == "PREPARE":
            if not self.connected:
                self._send("ERROR Not connected\n", addr)
                return

            layer = parts[1] if len(parts) > 1 else ""
            test = parts[2] if len(parts) > 2 else ""
            args = " ".join(parts[3:]) if len(parts) > 3 else ""

            try:
                ok, detail = self.on_prepare(layer, test, args)
                if ok:
                    self._send(f"READY {detail}\n", addr)
                else:
                    self._send(f"ERROR {detail}\n", addr)
            except Exception as e:
                logger.error("PREPARE handler error: %s", e)
                self._send(f"ERROR {e}\n", addr)

        elif cmd == "START":
            if not self.connected:
                self._send("ERROR Not connected\n", addr)
                return
            try:
                if self.on_start():
                    self._send("ACK\n", addr)
                else:
                    self._send("ERROR Start failed\n", addr)
            except Exception as e:
                self._send(f"ERROR {e}\n", addr)

        elif cmd == "STOP":
            if not self.connected:
                self._send("ERROR Not connected\n", addr)
                return
            try:
                if self.on_stop():
                    self._send("ACK\n", addr)
                else:
                    self._send("ERROR Stop failed\n", addr)
            except Exception as e:
                self._send(f"ERROR {e}\n", addr)

        elif cmd == "RESULT":
            if not self.connected:
                self._send("ERROR Not connected\n", addr)
                return
            try:
                result = self.on_result()
                self._send(f"REPORT {result}\n", addr)
            except Exception as e:
                self._send(f"ERROR {e}\n", addr)

        elif cmd == "GETREPORT":
            if not self.connected:
                self._send("ERROR Not connected\n", addr)
                return
            try:
                result = self.on_result()
                self._send(f"REPORT {result}\n", addr)
            except Exception as e:
                self._send(f"ERROR {e}\n", addr)

        elif cmd == "DONE":
            logger.info("DONE from %s - session ending", addr)
            self._send("CONFIRM\n", addr)
            self.connected = False
            self.dut_addr = None

        else:
            logger.warning("Unknown command: %s", cmd)
            self._send(f"ERROR Unknown command: {cmd}\n", addr)
