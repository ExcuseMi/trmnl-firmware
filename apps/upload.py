"""Copies apps to /apps on a TRMNL X over its USB serial port (docs/apps.md, "Copy an app to the X").

  python3 apps/upload.py <port> apps/build/hello.elf [more.elf ...]

The X only has to be awake with the cable in: on a TRMNL screen, in a menu or with an app running, no menu needed
(touch the bar first if it sleeps; this script keeps asking for 30 s). Not running: installed at once. The same app
running: it is asked to exit, replaced and started again. Another app running: it keeps running. Only
/apps/<name>.elf is written; the file system with TRMNL's cached screens stays. Protocol:
lib/trmnl/include/app_upload.h. Linux or WSL only (termios); no pyserial: its open restarts the X.
"""
import os
import struct
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


def open_quiet(path):
    """The port as a raw, non-blocking descriptor, the X not restarted.

    The X's USB Serial/JTAG restarts (rst:0x15) when the host sets RTS with DTR clear, the state esptool uses for
    EN low. Linux raises both lines at the open; pyserial then clears DTR and RTS in two calls, and the state
    between them is that reset (owner's X 2026-11-02: every upload restarted it). Here both lines are cleared in
    ONE request, and HUPCL is cleared so the close leaves them alone."""
    import fcntl
    import termios
    import tty

    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("I", termios.TIOCM_DTR | termios.TIOCM_RTS))
        tty.setraw(fd)
        a = termios.tcgetattr(fd)
        a[2] |= termios.CLOCAL | termios.CREAD
        a[2] &= ~termios.HUPCL
        a[4] = a[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, a)
    except Exception:
        os.close(fd)
        raise
    return fd


def serial_port(path):
    """(write, read_reply, flush_input) on the port, opened without restarting the X"""
    import select

    fd = open_quiet(path)
    buf = bytearray()

    def write(data):
        view = memoryview(data)
        while view:
            try:
                n = os.write(fd, view)
            except BlockingIOError:
                select.select([], [fd], [], 1.0)
                continue
            view = view[n:]

    def read_reply(timeout_s):
        end = time.time() + timeout_s
        while True:
            while b"\n" in buf:
                i = buf.index(b"\n")
                line = bytes(buf[:i]).decode(errors="replace").strip()
                del buf[:i + 1]
                if line.startswith("TAPP "):
                    return line
            left = end - time.time()
            if left <= 0:
                return None
            if select.select([fd], [], [], min(left, 0.2))[0]:
                try:
                    chunk = os.read(fd, 4096)
                except BlockingIOError:
                    continue
                if not chunk:
                    raise UploadError("the port closed (the X restarted or the cable came out)")
                buf.extend(chunk)

    def flush_input():
        time.sleep(SETTLE_S)
        while select.select([fd], [], [], 0)[0] and os.read(fd, 4096):
            pass
        buf.clear()

    return write, read_reply, flush_input


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    write, read_reply, flush_input = serial_port(argv[1])
    flush_input()
    for path in argv[2:]:
        name = os.path.basename(path)
        upload(name, open(path, "rb").read(), write, read_reply)
        print("sent %s" % name)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except UploadError as e:
        sys.exit("upload failed: %s" % e)
