"""Copies apps to /apps on a TRMNL X over its USB serial port (docs/apps.md, "Copy an app to the X").

  python3 apps/upload.py <port> apps/build/hello.elf [more.elf ...]

The X only has to be awake with the cable in: on a TRMNL screen, in a menu or with an app running, no menu needed
(touch the bar first if it sleeps; this script keeps asking for 30 s). Not running: installed at once. The same app
running: it is asked to exit, replaced and started again. Another app running: it keeps running. Only
/apps/<name>.elf is written; the file system with TRMNL's cached screens stays. Protocol:
lib/trmnl/include/app_upload.h. Needs pyserial (in a venv).
"""
import os
import sys
import time
import zlib

BLOCK = 64  # APP_UPLOAD_BLOCK
REPLY_S = 10
HEADER_TRY_S = 4  # wait for READY this long before sending the header again
WAKE_S = 30  # keep sending the header this long while the X does not answer at all (asleep, restarting)
SETTLE_S = 0.5  # after opening the port


class UploadError(Exception):
    pass


def upload(name, data, write, read_reply, wake_s=WAKE_S):
    """Sends one file. write(bytes); read_reply(timeout_s) returns the next "TAPP ..." line or None."""
    header = b"TAPP PUT %s %d %08x\n" % (name.encode(), len(data), zlib.crc32(data) & 0xFFFFFFFF)
    # The X skips noise before "TAPP ", but a header can be lost or the X asleep: ask again only while NOTHING
    # answers (an awake X replies at once; an ERR is an answer and ends it)
    tries = max(2, int(wake_s // HEADER_TRY_S))
    reply = None
    for attempt in range(1, tries + 1):
        write(header)
        reply = read_reply(REPLY_S if attempt == tries else HEADER_TRY_S)
        if reply is not None:
            break
    expect(reply, "READY")
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent + BLOCK]
        write(chunk)
        sent += len(chunk)
        if sent < len(data):
            expect(read_reply(REPLY_S), "ACK %d" % sent)
    expect(read_reply(REPLY_S), "OK %s" % name)


def expect(line, want):
    if line is None:
        raise UploadError("no reply (wanted %r)" % want)
    if line != "TAPP " + want:
        raise UploadError(line)


def serial_port(path):
    import serial  # pyserial

    # DTR and RTS low BEFORE the open: the X's USB-Serial/JTAG resets on the usual DTR/RTS toggle at open
    port = serial.Serial()
    port.port = path
    port.baudrate = 115200
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    port.open()
    time.sleep(SETTLE_S)

    def read_reply(timeout_s):
        end = time.time() + timeout_s
        while time.time() < end:
            line = port.readline().decode(errors="replace").strip()
            if line.startswith("TAPP "):
                return line
        return None

    return port, read_reply


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    port, read_reply = serial_port(argv[1])
    port.reset_input_buffer()
    for path in argv[2:]:
        name = os.path.basename(path)
        upload(name, open(path, "rb").read(), port.write, read_reply)
        print("sent %s" % name)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except UploadError as e:
        sys.exit("upload failed: %s" % e)
