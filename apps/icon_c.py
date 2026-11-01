#!/usr/bin/env python3
"""A 64x64 PBM (P1 or P4, 1 = black) -> the C define for APP_ICON (include/app_api.h).
  apps/icon_c.py icon.pbm MY_ICON > icon.h      then in the app:  #include "icon.h"  APP_ICON(MY_ICON);
Convert a picture first, e.g. magick logo.png -resize 64x64 -threshold 50% icon.pbm"""
import sys


def read_pbm(path):
    data = open(path, "rb").read()
    tok, pos = [], 0
    while len(tok) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tok.append(data[start:pos])
    magic, w, h = tok[0], int(tok[1]), int(tok[2])
    if (w, h) != (64, 64) or magic not in (b"P1", b"P4"):
        sys.exit("need a 64x64 P1 or P4 PBM, got %s %dx%d" % (magic.decode(), w, h))
    body = data[pos + 1:] if magic == b"P4" else data[pos:]
    if magic == b"P1":
        bits = [c == 49 for c in body if c in (48, 49)]
        return [bits[y * 64:(y + 1) * 64] for y in range(64)]
    return [[bool(body[y * 8 + x // 8] & (0x80 >> (x % 8))) for x in range(64)] for y in range(64)]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rows = read_pbm(sys.argv[1])
    out = []
    for r in rows:  # APP_ICON: 1 = white, so black PBM pixels are 0 bits
        for x in range(0, 64, 8):
            out.append(sum((0 if r[x + k] else 1) << (7 - k) for k in range(8)))
    print("#define %s \\" % sys.argv[2])
    for i in range(0, 512, 16):
        print("    " + ", ".join("0x%02X" % v for v in out[i:i + 16]) + (", \\" if i + 16 < 512 else ""))


main()
