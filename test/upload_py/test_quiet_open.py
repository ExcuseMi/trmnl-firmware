"""apps/upload.py opens the X's port without restarting it (owner's X 2026-11-02: every upload restarted the X).

The X's USB Serial/JTAG restarts when RTS is set with DTR clear. Linux raises both at the open; a library that then
clears DTR and RTS in two requests passes through that state. The open must clear both in ONE request and clear
HUPCL. Checked on a pseudo terminal with the modem-control requests recorded."""
import fcntl
import os
import sys
import termios
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps"))
import upload  # noqa: E402

MODEM = {termios.TIOCMBIC: "BIC", termios.TIOCMBIS: "BIS", termios.TIOCMSET: "SET"}
BOTH = termios.TIOCM_DTR | termios.TIOCM_RTS


class QuietOpen(unittest.TestCase):
    def test_one_request_clears_both_lines_and_hupcl(self):
        master, slave = os.openpty()
        path = os.ttyname(slave)
        calls, real = [], fcntl.ioctl

        def ioctl(fd, req, arg=0, *rest):
            if req in MODEM:  # a pty has no modem lines: record, do not send
                calls.append((MODEM[req], int.from_bytes(arg, "little") if isinstance(arg, bytes) else arg))
                return arg
            return real(fd, req, arg, *rest)

        fcntl.ioctl = ioctl
        try:
            upload.serial_port(path)
        finally:
            fcntl.ioctl = real
        self.assertEqual(calls, [("BIC", BOTH)])
        self.assertFalse(termios.tcgetattr(slave)[2] & termios.HUPCL)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    unittest.main()
