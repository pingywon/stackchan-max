#!/usr/bin/env bash
# Build the site and push it to the gh-pages branch. Run from anywhere in the repo.
set -euo pipefail
cd "$(dirname "$0")/.."

python3 site/build.py

tmp=$(mktemp -d)
trap 'git worktree remove --force "$tmp" 2>/dev/null || true' EXIT

if git fetch -q origin gh-pages 2>/dev/null; then
  git worktree add -q -B gh-pages "$tmp" origin/gh-pages
else
  git worktree add -q --orphan -b gh-pages "$tmp"
fi

find "$tmp" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -a site/out/. "$tmp"/

git -C "$tmp" add -A
if git -C "$tmp" diff --cached --quiet; then
  echo "nothing changed; gh-pages is already up to date"
else
  git -C "$tmp" commit -q -m "Site v$(cat site/VERSION)"
  git -C "$tmp" push -q origin gh-pages
  echo "published site v$(cat site/VERSION)"
fi
