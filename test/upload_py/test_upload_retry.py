"""apps/upload.py on a cable update: it keeps asking while the X does not answer at all, and stops at an answer."""
import os
import sys
import zlib

import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps"))
import upload  # noqa: E402

DATA = b"\x7fELF" + b"x" * 200


class Fake:
    def __init__(self, silent_headers, then):
        self.silent, self.then, self.headers, self.sent = silent_headers, list(then), 0, 0

    def write(self, b):
        if b.startswith(b"TAPP PUT"):
            self.headers += 1
        else:
            self.sent += len(b)

    def read_reply(self, timeout_s):
        if self.headers <= self.silent:
            return None
        return self.then.pop(0) if self.then else None


def replies():
    out = ["TAPP READY"]
    out += ["TAPP ACK %d" % n for n in range(64, len(DATA), 64)]
    return out + ["TAPP OK a.elf"]


class Retry(unittest.TestCase):
    def test_asks_again_while_the_x_sleeps_then_sends(self):
        f = Fake(silent_headers=5, then=replies())
        upload.upload("a.elf", DATA, f.write, f.read_reply, wake_s=40)
        self.assertEqual((f.headers, f.sent), (6, len(DATA)))

    def test_an_error_ends_it_at_once_without_a_second_header(self):
        f = Fake(silent_headers=0, then=["TAPP ERR busy: USB host mode has the port"])
        with self.assertRaisesRegex(upload.UploadError, "host mode"):
            upload.upload("a.elf", DATA, f.write, f.read_reply, wake_s=40)
        self.assertEqual((f.headers, f.sent), (1, 0))

    def test_gives_up_when_nothing_ever_answers(self):
        f = Fake(silent_headers=99, then=[])
        with self.assertRaisesRegex(upload.UploadError, "no reply"):
            upload.upload("a.elf", DATA, f.write, f.read_reply, wake_s=12)
        self.assertEqual(f.headers, 3)


if __name__ == "__main__":
    unittest.main()
