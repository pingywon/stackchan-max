#!/usr/bin/env python3
"""Lint the portal page before building firmware.

`portal_page.h` holds the entire web UI inside a C++ raw string literal, which means the
compiler validates none of it. Three failures are possible and all of them are expensive to
find on hardware:

1. The closing delimiter appearing inside the markup — the literal ends early and the
   build fails with errors that point nowhere near the real problem.
2. A JavaScript syntax error — the whole script fails to parse, so every control on the
   page is dead while the HTML still renders. It reads as a styling bug.
3. The script reaching for an element id that no longer exists — a null dereference that
   silently kills whichever panel it was in.

    python3 workshop/portal/check_page.py

Exits non-zero on anything that would break the page. Needs `node` for the JavaScript
check and skips it with a warning if node is not installed.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE_HEADER = os.path.normpath(
    os.path.join(HERE, "..", "..", "firmware", "main", "portal", "portal_page.h"))

OPEN, CLOSE = 'R"HTML(', ')HTML"'


def main() -> int:
    problems: list[str] = []

    def check(ok: bool, label: str):
        print(("  PASS  " if ok else "  FAIL  ") + label)
        if not ok:
            problems.append(label)

    with open(PAGE_HEADER, encoding="utf-8") as fh:
        src = fh.read()

    html = src[src.index(OPEN) + len(OPEN): src.rindex(CLOSE)]
    js = html[html.index("<script>") + len("<script>"): html.index("</script>")]

    print(f"\n{os.path.relpath(PAGE_HEADER)}  —  {len(html)} bytes of page, "
          f"{len(js)} of script\n")

    check(CLOSE not in html, "raw-string delimiter does not appear inside the page")

    # ------------------------------------------------------------- javascript --
    node = shutil.which("node")
    if node:
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False,
                                         encoding="utf-8") as tmp:
            tmp.write(js)
            path = tmp.name
        try:
            result = subprocess.run([node, "--check", path],
                                    capture_output=True, text=True)
            check(result.returncode == 0, "javascript parses")
            if result.returncode != 0:
                print(result.stderr.strip())
        finally:
            os.unlink(path)
    else:
        print("  SKIP  javascript parse (node not installed)")

    # --------------------------------------------------------------- element ids --
    ids = set(re.findall(r'\bid="([^"]+)"', html))
    used = set(re.findall(r'\$\("([^"]+)"\)', js))
    missing = sorted(used - ids)
    check(not missing, "every element the script reaches for exists")
    if missing:
        print("        missing: " + ", ".join(missing))

    # ------------------------------------------------------------------- nav --
    sections = set(re.findall(r'<section id="s-([a-z]+)"', html))
    targets = set(re.findall(r'data-go="([a-z-]+)"', html)) - {"ai-help"}
    orphans = sorted(targets - sections)
    check(not orphans, "every nav target has a section")
    if orphans:
        print("        orphaned: " + ", ".join(orphans))

    # No external requests: the page has to work with the assets partition wiped.
    external = re.findall(r'(?:src|href)="(https?://[^"]+)"', html)
    check(not external, "page makes no external requests")
    if external:
        print("        external: " + ", ".join(external))

    print()
    if problems:
        print(f"{len(problems)} problem(s) — do not build this")
        return 1
    print("page is clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
