"""
Packet Validator
Validates captured packets against expected test patterns.
"""

import logging
import socket
import struct

logger = logging.getLogger("validator")


class PacketValidator:
    """Validates packet correctness for test verification."""

    @staticmethod
    def validate_ethernet(frame):
        """Validate Ethernet frame structure."""
        if len(frame) < 14:
            return False, "Frame too short"
        return True, "OK"

    @staticmethod
    def validate_arp(frame):
        """Validate ARP packet structure."""
        if len(frame) < 42:
            return False, "ARP packet too short"

        ether_type = struct.unpack("!H", frame[12:14])[0]
        if ether_type != 0x0806:
            return False, f"Not ARP (EtherType=0x{ether_type:04x})"

        hw_type = struct.unpack("!H", frame[14:16])[0]
        if hw_type != 1:
            return False, f"Unexpected HW type: {hw_type}"

        proto_type = struct.unpack("!H", frame[16:18])[0]
        if proto_type != 0x0800:
            return False, f"Unexpected proto type: 0x{proto_type:04x}"

        return True, "Valid ARP"

    @staticmethod
    def validate_ipv4(frame):
        """Validate IPv4 packet."""
        if len(frame) < 34:
            return False, "IPv4 packet too short"

        ether_type = struct.unpack("!H", frame[12:14])[0]
        if ether_type != 0x0800:
            return False, "Not IPv4"

        version = (frame[14] >> 4) & 0x0F
        if version != 4:
            return False, f"Not IPv4 (version={version})"

        ihl = (frame[14] & 0x0F) * 4
        total_len = struct.unpack("!H", frame[16:18])[0]

        if total_len < ihl:
            return False, "Invalid total length"

        return True, "Valid IPv4"

    @staticmethod
    def validate_icmp_echo(frame):
        """Validate ICMP echo request/reply."""
        ok, msg = PacketValidator.validate_ipv4(frame)
        if not ok:
            return False, msg

        ihl = (frame[14] & 0x0F) * 4
        offset = 14 + ihl

        if len(frame) < offset + 8:
            return False, "ICMP too short"

        protocol = frame[23]
        if protocol != 1:
            return False, f"Not ICMP (proto={protocol})"

        icmp_type = frame[offset]
        if icmp_type not in (0, 8):
            return False, f"Not echo (type={icmp_type})"

        return True, "Valid ICMP echo"

    @staticmethod
    def validate_tcp(frame):
        """Validate TCP segment."""
        ok, msg = PacketValidator.validate_ipv4(frame)
        if not ok:
            return False, msg

        protocol = frame[23]
        if protocol != 6:
            return False, f"Not TCP (proto={protocol})"

        return True, "Valid TCP"

    @staticmethod
    def validate_udp(frame):
        """Validate UDP datagram."""
        ok, msg = PacketValidator.validate_ipv4(frame)
        if not ok:
            return False, msg

        protocol = frame[23]
        if protocol != 17:
            return False, f"Not UDP (proto={protocol})"

        return True, "Valid UDP"
