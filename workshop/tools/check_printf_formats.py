#!/usr/bin/env python3
"""Flag 64-bit printf conversions, which are silently broken in this build.

Why this exists
---------------
`CONFIG_LIBC_NEWLIB_NANO_FORMAT=y` is enabled, and ESP-IDF's own Kconfig says of it:
"This option doesn't support 64-bit integer formats". The ROM printf does not honour the
`ll` width, so it consumes four bytes where the caller pushed eight. Every argument after
that point is then read from the wrong offset.

That is far worse than a wrong number on screen. In `/api/info` it meant a trailing `%s`
picked up the high half of a 64-bit uptime — zero for any uptime under about 49 days —
so printf called `strlen(NULL)`, the HTTP task hit a LoadProhibited fault, and the device
rebooted within seconds of anyone opening the portal.

**The compiler cannot catch this.** `printf("%llu", (unsigned long long)x)` is correct C;
`-Wformat` has no complaint. The defect lives in the runtime library, so it can only be
caught by looking, which is what this does.

    python3 workshop/tools/check_printf_formats.py

Exits non-zero on a hit. The fix is not to reach for a different specifier — split the
value into two 32-bit halves, cast down when the range allows, or build the JSON with
cJSON and skip varargs entirely, which is what `/api/info` now does.
"""

from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))

# Only our own firmware. Upstream xiaozhi and the managed components are not ours to
# police, and most of their 64-bit formats are in log lines where a wrong number is the
# whole consequence.
ROOTS = [os.path.join(REPO, "firmware", "main")]

SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp")
SKIP_FILES = {"portal_page.h"}  # embedded JavaScript, not C

# %llu %lld %llx, and the PRI*64 macros that expand to the same thing.
PATTERN = re.compile(r'%[-+ #0-9.*]*ll[diouxX]|PRI[diouxX]64')


def main() -> int:
    hits = []

    for root in ROOTS:
        for folder, _, files in os.walk(root):
            for name in sorted(files):
                if not name.endswith(SUFFIXES) or name in SKIP_FILES:
                    continue
                path = os.path.join(folder, name)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    for number, line in enumerate(fh, 1):
                        text = line.strip()
                        # Prose about the problem is not the problem. Without this the
                        # comment explaining the bug trips the check that found it.
                        if text.startswith(("*", "//", "/*")):
                            continue
                        if PATTERN.search(line):
                            hits.append((os.path.relpath(path, REPO), number, text))

    if not hits:
        print("no 64-bit printf conversions in firmware/main — good")
        return 0

    print("64-bit printf conversions found. These do NOT work with nano formatting;")
    print("they shift every following argument and can crash on a trailing %s.\n")
    for path, number, text in hits:
        print(f"  {path}:{number}")
        print(f"      {text}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
