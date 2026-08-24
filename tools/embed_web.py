#!/usr/bin/env python3
"""Embed web/index.html into PcPower_BLE/src/web_ui.h.

The web UI is served from flash, so it has to travel inside the firmware image. This
turns the page into one C++ raw string literal, which keeps the generated header
readable and diffable instead of an unreviewable array of bytes.

Standard library only, Python 3.8+, and safe to run from any working directory:
the build scripts call it as `python3 tools/embed_web.py` from the repository root.
"""

import sys
from pathlib import Path

DELIM = "HTMLPAGE"
TERMINATOR = ')' + DELIM + '"'          # what would end the raw string early
SIZE_BUDGET = 40 * 1024                 # the page is meant to stay small; advisory only

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "web" / "index.html"
HEADER = ROOT / "PcPower_BLE" / "src" / "web_ui.h"


def rel(path):
    """Repository-relative when possible - Path.is_relative_to is 3.9+."""
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def die(message):
    sys.stderr.write("embed_web.py: error: %s\n" % message)
    raise SystemExit(1)


def main(argv):
    source = Path(argv[1]).resolve() if len(argv) > 1 else SOURCE
    header = Path(argv[2]).resolve() if len(argv) > 2 else HEADER

    if not source.is_file():
        die("cannot read %s" % source)

    raw = source.read_bytes()
    try:
        html = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        die("%s is not valid UTF-8 (%s)" % (source, exc))

    html = html.replace("\r\n", "\n").replace("\r", "\n")

    if TERMINATOR in html:
        line = html[: html.index(TERMINATOR)].count("\n") + 1
        die(
            '%s line %d contains the sequence %s, which would end the C++ raw string '
            "literal early. Rewrite that text (for example split it across a "
            "concatenation) and run this script again." % (source, line, TERMINATOR)
        )

    # No newline between the ( and the doctype: the page must start with it.
    body = html if html.endswith("\n") else html + "\n"
    out = (
        "// GENERATED FILE - DO NOT EDIT BY HAND.\n"
        "// Produced by tools/embed_web.py from web/index.html; edit that page instead\n"
        "// and re-run the script (tools/build.sh does it for you).\n"
        "#pragma once\n"
        "\n"
        "static const char WEB_INDEX_HTML[] PROGMEM = R\"%s(%s)%s\";\n"
        % (DELIM, body, DELIM)
    )
    encoded = out.encode("utf-8")

    header.parent.mkdir(parents=True, exist_ok=True)
    unchanged = header.is_file() and header.read_bytes() == encoded
    if not unchanged:
        header.write_bytes(encoded)

    page_bytes = len(body.encode("utf-8"))
    print(
        "embed_web.py: %s -> %s (page %d bytes, header %d bytes)%s"
        % (rel(source), rel(header), page_bytes, len(encoded),
           " [unchanged]" if unchanged else "")
    )
    if page_bytes > SIZE_BUDGET:
        print(
            "embed_web.py: note: the page is %d bytes, over the %d byte budget"
            % (page_bytes, SIZE_BUDGET)
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
