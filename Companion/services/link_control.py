"""
Link Control - L1 Physical Layer
Controls network interface link state via ethtool/ip commands.
"""

import logging
import subprocess

logger = logging.getLogger("link_ctrl")


class LinkControl:
    """L1 Physical layer link control via ethtool."""

    def __init__(self, interface):
        self.interface = interface

    def prepare(self, test, args):
        """Prepare for a L1 test."""
        logger.info("L1 prepare: %s %s", test, args)
        return True, "OK"

    def stop_test(self):
        pass

    def get_result(self):
        return None

    def start(self):
        pass

    def stop(self):
        pass

    def get_link_status(self):
        """Get current link status via ethtool."""
        try:
            out = subprocess.check_output(
                ["ethtool", self.interface],
                stderr=subprocess.DEVNULL, text=True)
            for line in out.splitlines():
                if "Link detected:" in line:
                    return "yes" in line.lower()
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
        return None

    def set_link(self, up=True):
        """Set link up or down."""
        state = "up" if up else "down"
        try:
            subprocess.check_call(
                ["ip", "link", "set", self.interface, state],
                stderr=subprocess.DEVNULL)
            logger.info("Link %s: %s", state, self.interface)
            return True
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            logger.error("Failed to set link %s: %s", state, e)
            return False

    def get_speed(self):
        """Get link speed via ethtool."""
        try:
            out = subprocess.check_output(
                ["ethtool", self.interface],
                stderr=subprocess.DEVNULL, text=True)
            for line in out.splitlines():
                if "Speed:" in line:
                    return line.split(":")[1].strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
        return "Unknown"
