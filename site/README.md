# The project site

One page, published on GitHub Pages from the `gh-pages` branch.

## Change it

1. Edit `index.src.html`. Its pictures come from `docs/img/`.
2. Bump `VERSION`.
3. Build it: `python3 site/build.py`. The result lands in `site/out/`.
4. Look at it: `python3 -m http.server 8140 --directory site/out`, then open `http://localhost:8140/`.
5. Publish it: `bash site/publish.sh`. Pages updates in about a minute.

## What not to type by hand

- **The firmware version.** `build.py` reads it from `firmware/CMakeLists.txt`.
- **The site version.** It comes from `site/VERSION`.
- **The repository address.** It comes from the `origin` remote.

Both versions show at the bottom of the page.
