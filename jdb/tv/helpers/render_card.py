#!/usr/bin/env python3
"""
render_card.py — render an HTML template to PNG via headless Chrome.

Usage:
    python render_card.py title   <out.png> <lesson> <title> <subtitle> [host]
    python render_card.py thumb   <out.png> <lesson> <title> <hook>

Reads cards/template_<kind>.html, substitutes {{TOKENS}}, renders via
Playwright (Chromium) at the template's CSS dimensions (1920x1080 for
title cards, 1280x720 for thumbnails).
"""
import sys, pathlib

if len(sys.argv) < 5:
    print("usage: render_card.py {title|thumb} <out.png> <lesson> <title> [extras…]", file=sys.stderr)
    sys.exit(2)

kind     = sys.argv[1].lower()
out_path = pathlib.Path(sys.argv[2])
lesson   = sys.argv[3]
title    = sys.argv[4]
extra1   = sys.argv[5] if len(sys.argv) > 5 else ""
extra2   = sys.argv[6] if len(sys.argv) > 6 else ""

here = pathlib.Path(__file__).resolve().parent.parent
if kind == "title":
    tpl_path = here / "cards" / "template_title.html"
    width, height = 1920, 1080
    substitutions = {
        "{{LESSON}}":   lesson,
        "{{TITLE}}":    title,
        "{{SUBTITLE}}": extra1,
        "{{HOST}}":     extra2 or "with Jaydee Basica",
    }
elif kind == "thumb":
    tpl_path = here / "cards" / "template_thumbnail.html"
    width, height = 1280, 720
    substitutions = {
        "{{LESSON}}": lesson,
        "{{TITLE}}":  title,
        "{{HOOK}}":   extra1,
    }
else:
    print(f"unknown kind: {kind!r} — use 'title' or 'thumb'", file=sys.stderr)
    sys.exit(2)

if not tpl_path.exists():
    print(f"ERROR: template not found: {tpl_path}", file=sys.stderr)
    sys.exit(3)

html = tpl_path.read_text(encoding="utf-8")
for token, value in substitutions.items():
    html = html.replace(token, value)

# Resolve to absolute now — Path.as_uri() requires an absolute path,
# and the user typically invokes us with a relative <out.png>.
out_path = out_path.resolve()
out_path.parent.mkdir(parents=True, exist_ok=True)

# Write the materialised HTML next to the PNG for inspection/debug.
mat_path = out_path.with_suffix(".html")
mat_path.write_text(html, encoding="utf-8")

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("ERROR: pip install playwright && playwright install chromium", file=sys.stderr)
    sys.exit(4)

with sync_playwright() as p:
    browser = p.chromium.launch()
    ctx = browser.new_context(viewport={"width": width, "height": height},
                              device_scale_factor=1)
    page = ctx.new_page()
    # file:// URL so the template's web fonts can load.
    page.goto(mat_path.as_uri())
    page.wait_for_load_state("networkidle")
    page.screenshot(path=str(out_path), full_page=False,
                    clip={"x": 0, "y": 0, "width": width, "height": height})
    browser.close()

print(f"{out_path}  ({width}x{height})")
