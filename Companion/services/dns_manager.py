"""
DNS Manager - L7 Application Layer
Minimal DNS server that resolves test domain names.
Responds to A (forward) and PTR (reverse) queries.
"""

import logging
import socket
import struct
import threading

logger = logging.getLogger("dns")

DNS_TYPE_A = 1
DNS_TYPE_PTR = 12
DNS_CLASS_IN = 1
DNS_FLAG_RESPONSE = 0x8000
DNS_FLAG_AA = 0x0400


class DnsManager:
    """Minimal DNS server for test domain resolution."""

    def __init__(self, local_ip, port, domain):
        self.local_ip = local_ip
        self.port = port
        self.domain = domain
        self.sock = None
        self.thread = None
        self.running = False
        self.query_count = 0

        # Static records
        self.records = {
            f"companion.{domain}": local_ip,
            f"test.{domain}": local_ip,
            f"www.{domain}": local_ip,
            domain: local_ip,
        }

    def prepare(self, test, args):
        logger.info("DNS prepare: %s", test)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        return f"queries={self.query_count}"

    def start(self):
        """Start DNS server."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.settimeout(1.0)
            self.sock.bind((self.local_ip, self.port))
        except OSError as e:
            logger.warning("DNS bind %s:%d failed: %s", self.local_ip, self.port, e)
            return

        self.running = True
        self.thread = threading.Thread(target=self._dns_loop, daemon=True)
        self.thread.start()
        logger.info("DNS server on %s:%d (domain: %s)",
                     self.local_ip, self.port, self.domain)

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=3)
        if self.sock:
            self.sock.close()
            self.sock = None

    def _dns_loop(self):
        """Handle DNS queries."""
        while self.running:
            try:
                data, addr = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break

            if len(data) < 12:
                continue

            self.query_count += 1
            response = self._handle_query(data)
            if response:
                try:
                    self.sock.sendto(response, addr)
                except OSError:
                    pass

    def _handle_query(self, data):
        """Parse DNS query and build response."""
        # Header: ID(2) + Flags(2) + QDCount(2) + ANCount(2) + NSCount(2) + ARCount(2)
        if len(data) < 12:
            return None

        txn_id = data[0:2]
        flags = struct.unpack("!H", data[2:4])[0]
        qd_count = struct.unpack("!H", data[4:6])[0]

        if qd_count == 0:
            return None

        # Parse question section
        qname, offset = self._decode_name(data, 12)
        if offset + 4 > len(data):
            return None

        qtype = struct.unpack("!H", data[offset:offset+2])[0]
        qclass = struct.unpack("!H", data[offset+2:offset+4])[0]

        logger.debug("DNS query: %s type=%d", qname, qtype)

        if qtype == DNS_TYPE_A:
            return self._build_a_response(txn_id, data[12:offset+4], qname)
        elif qtype == DNS_TYPE_PTR:
            return self._build_ptr_response(txn_id, data[12:offset+4], qname)

        # Return NXDOMAIN for unsupported types
        return self._build_nxdomain(txn_id, data[12:offset+4])

    def _decode_name(self, data, offset):
        """Decode a DNS name from the packet."""
        labels = []
        while offset < len(data):
            length = data[offset]
            if length == 0:
                offset += 1
                break
            if length & 0xC0 == 0xC0:
                # Pointer
                ptr = struct.unpack("!H", data[offset:offset+2])[0] & 0x3FFF
                name, _ = self._decode_name(data, ptr)
                labels.append(name)
                offset += 2
                break
            offset += 1
            labels.append(data[offset:offset+length].decode("ascii", errors="replace"))
            offset += length
        return ".".join(labels), offset

    def _encode_name(self, name):
        """Encode a DNS name."""
        result = b""
        for label in name.split("."):
            result += bytes([len(label)]) + label.encode("ascii")
        result += b"\x00"
        return result

    def _build_a_response(self, txn_id, question, qname):
        """Build DNS A record response."""
        qname_lower = qname.lower().rstrip(".")
        ip = self.records.get(qname_lower)

        if ip is None:
            # Try partial match
            for domain, addr in self.records.items():
                if qname_lower.endswith(domain):
                    ip = addr
                    break

        if ip is None:
            return self._build_nxdomain(txn_id, question)

        # Header
        flags = DNS_FLAG_RESPONSE | DNS_FLAG_AA
        header = txn_id + struct.pack("!HHHHH",
                                       flags, 1, 1, 0, 0)

        # Answer: name pointer + type + class + TTL + rdlength + rdata
        answer = b"\xC0\x0C"  # Name pointer to question
        answer += struct.pack("!HHI", DNS_TYPE_A, DNS_CLASS_IN, 300)
        answer += struct.pack("!H", 4)
        answer += socket.inet_aton(ip)

        logger.debug("DNS A reply: %s -> %s", qname, ip)
        return header + question + answer

    def _build_ptr_response(self, txn_id, question, qname):
        """Build DNS PTR record response."""
        # Reverse lookup: extract IP from in-addr.arpa name
        parts = qname.lower().rstrip(".").split(".")
        if len(parts) >= 4 and "in-addr" in qname:
            ip = ".".join(reversed(parts[:4]))
        else:
            return self._build_nxdomain(txn_id, question)

        # Find hostname for IP
        hostname = None
        for domain, addr in self.records.items():
            if addr == ip:
                hostname = domain
                break

        if hostname is None:
            hostname = f"host-{ip.replace('.', '-')}.{self.domain}"

        # Header
        flags = DNS_FLAG_RESPONSE | DNS_FLAG_AA
        header = txn_id + struct.pack("!HHHHH",
                                       flags, 1, 1, 0, 0)

        # Answer
        ptr_data = self._encode_name(hostname)
        answer = b"\xC0\x0C"
        answer += struct.pack("!HHI", DNS_TYPE_PTR, DNS_CLASS_IN, 300)
        answer += struct.pack("!H", len(ptr_data))
        answer += ptr_data

        logger.debug("DNS PTR reply: %s -> %s", qname, hostname)
        return header + question + answer

    def _build_nxdomain(self, txn_id, question):
        """Build NXDOMAIN response."""
        flags = DNS_FLAG_RESPONSE | DNS_FLAG_AA | 0x0003  # RCODE=3 (NXDOMAIN)
        header = txn_id + struct.pack("!HHHHH",
                                       flags, 1, 0, 0, 0)
        return header + question
