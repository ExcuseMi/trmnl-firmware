"""apps/upload.py --remove: one "TAPP RM" line with an app name only; an ERR answer is a failure (owner 2026-11-02)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps"))
import upload  # noqa: E402


class Remove(unittest.TestCase):
    def run_remove(self, app, data, answer):
        sent = []
        upload.remove(app, data, sent.append, lambda _t: answer)
        return sent

    def test_sends_one_line_and_takes_the_ok(self):
        self.assertEqual(self.run_remove("trmnl-print-old", False, "TAPP RM OK trmnl-print-old"),
                         [b"TAPP RM trmnl-print-old\n"])
        self.assertEqual(self.run_remove("gameboy", True, "TAPP RM OK gameboy data"), [b"TAPP RM gameboy data\n"])

    def test_paths_are_refused_before_anything_is_sent(self):
        for bad in ["../config", "a/b", "/apps/x.elf", "", ".hidden", "x y"]:
            sent = []
            with self.assertRaises(upload.UploadError):
                upload.remove(bad, False, sent.append, lambda _t: "TAPP RM OK " + bad)
            self.assertEqual(sent, [], bad)

    def test_an_err_or_no_answer_fails(self):
        with self.assertRaises(upload.UploadError):
            self.run_remove("trmnl-sketch", False, "TAPP ERR rm trmnl-sketch: it is running or due to resume (end it first)")
        with self.assertRaises(upload.UploadError):
            self.run_remove("trmnl-sketch", False, None)


if __name__ == "__main__":
    unittest.main()
