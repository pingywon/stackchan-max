#!/usr/bin/env python3
"""Build the project site into site/out/, ready to publish on GitHub Pages.

    python3 site/build.py                      # repo taken from the "origin" remote
    python3 site/build.py --repo owner/name    # or name it

The page is one file, `site/index.src.html`. This script fills in the things that must
never be typed by hand, and copies the pictures the page uses out of `docs/img/`:

- the firmware version, read from `firmware/CMakeLists.txt` by the same reader
  `workshop/tools/check_version.py` uses
- the site's own version, from `site/VERSION`
- the repository's address
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(HERE, "out")

sys.path.insert(0, os.path.join(ROOT, "workshop", "tools"))
from check_version import read_version  # noqa: E402


def origin_slug() -> str:
    try:
        url = subprocess.run(["git", "-C", ROOT, "remote", "get-url", "origin"],
                             capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        sys.exit("no 'origin' remote found; pass --repo owner/name")
    match = re.search(r"github\.com[:/]([^/]+)/(.+?)(?:\.git)?$", url)
    if not match:
        sys.exit(f"cannot read owner/name from {url!r}; pass --repo owner/name")
    return f"{match.group(1)}/{match.group(2)}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", help="owner/name on GitHub (default: the origin remote)")
    args = ap.parse_args()

    slug = args.repo or origin_slug()
    owner, name = slug.split("/", 1)
    values = {
        "FW_VERSION": read_version(ROOT),
        "SITE_VERSION": open(os.path.join(HERE, "VERSION"), encoding="utf-8").read().strip(),
        "REPO_SLUG": slug,
        "REPO_NAME": name,
        "REPO_URL": f"https://github.com/{slug}",
        "SITE_URL": f"https://{owner.lower()}.github.io/{name}/",
    }

    page = open(os.path.join(HERE, "index.src.html"), encoding="utf-8").read()
    for key, value in values.items():
        page = page.replace("{{" + key + "}}", value)
    left = sorted(set(re.findall(r"\{\{[A-Z_]+\}\}", page)))
    if left:
        sys.exit(f"unfilled placeholders: {', '.join(left)}")

    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(OUT, "img"))
    open(os.path.join(OUT, "index.html"), "w", encoding="utf-8").write(page)
    open(os.path.join(OUT, ".nojekyll"), "w").close()
    for static in ("VERSION", "og.png", "favicon.png", "favicon.ico", "apple-touch-icon.png"):
        shutil.copy(os.path.join(HERE, static), os.path.join(OUT, static))

    pictures = sorted(set(re.findall(r'(?:img/|data-img=")([\w.-]+\.(?:png|gif))', page)))
    for picture in pictures:
        source = os.path.join(ROOT, "docs", "img", picture)
        if not os.path.exists(source):
            sys.exit(f"the page uses {picture}, which is not in docs/img/")
        shutil.copy(source, os.path.join(OUT, "img", picture))

    print(f"built site v{values['SITE_VERSION']} for firmware {values['FW_VERSION']} "
          f"({slug}): {len(pictures)} pictures -> {os.path.relpath(OUT, ROOT)}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
