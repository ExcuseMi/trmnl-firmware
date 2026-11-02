"""No firmware code erases the file system: it holds the owner's apps, saves, ROMs and drawings.

Owner's X 2026-11-02: filesystem_write_to_file called FS.format() when an image write came up short, so a full disk
would have erased everything. Mounting with formatOnFail (a file system that does not mount at all) is the one
allowed case. Run: .venv/bin/pytest test/config (no device, no build)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [p for d in ("src", "lib") for p in (ROOT / d).rglob("*") if p.suffix in (".c", ".cpp", ".h", ".hpp")]
FORMAT = re.compile(r"\b(FS|LittleFS|SPIFFS|FFat|SD)\s*\.\s*format\s*\(|\besp_littlefs_format\w*\s*\(|"
                    r"\bspiffs_format\w*\s*\(|\besp_spiffs_format\s*\(")


def code_lines(path):
    for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        code = line.split("//", 1)[0]
        if code.strip():
            yield n, code


def test_nothing_formats_the_file_system():
    hits = ["%s:%d: %s" % (p.relative_to(ROOT), n, c.strip()) for p in SOURCES for n, c in code_lines(p)
            if FORMAT.search(c)]
    assert hits == [], "the file system must never be erased:\n" + "\n".join(hits)


def test_the_sources_are_found():
    assert any(p.name == "filesystem.cpp" for p in SOURCES)
