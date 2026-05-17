#!/usr/bin/env python3
"""
render_card.py — render an HTML template to PNG via headless Chrome.

Usage:
    python render_card.py title  <out.png> <args.json>
    python render_card.py thumb  <out.png> <args.json>

args.json (UTF-8) carries the template substitutions:
    title-kind: {"lesson": ..., "title": ..., "subtitle": ..., "host": ...}
    thumb-kind: {"lesson": ..., "title": ..., "hook": ...}

The JSON-sidecar approach exists to bypass cmd.exe's ANSI/CP1252 arg
parsing on Windows.  Passing non-ASCII (em-dashes, umlauts, …) via the
command line gets mangled before Python ever sees it; routing through a
UTF-8 file keeps the bytes intact.

Reads cards/template_<kind>.html, substitutes {{TOKENS}}, renders via
Playwright (Chromium) at the template's CSS dimensions (1920x1080 for
title cards, 1280x720 for thumbnails).
"""
import sys, pathlib, json

if len(sys.argv) != 4:
    print("usage: render_card.py {title|thumb} <out.png> <args.json>", file=sys.stderr)
    sys.exit(2)

kind     = sys.argv[1].lower()
out_path = pathlib.Path(sys.argv[2])
args_path = pathlib.Path(sys.argv[3])

if not args_path.exists():
    print(f"ERROR: args file not found: {args_path}", file=sys.stderr)
    sys.exit(3)
payload = json.loads(args_path.read_text(encoding="utf-8"))

here = pathlib.Path(__file__).resolve().parent.parent
if kind == "title":
    tpl_path = here / "cards" / "template_title.html"
    width, height = 1920, 1080
    substitutions = {
        "{{LESSON}}":   payload.get("lesson", ""),
        "{{TITLE}}":    payload.get("title", ""),
        "{{SUBTITLE}}": payload.get("subtitle", ""),
        "{{HOST}}":     payload.get("host") or "with Jaydee Basica",
    }
elif kind == "thumb":
    tpl_path = here / "cards" / "template_thumbnail.html"
    width, height = 1280, 720
    substitutions = {
        "{{LESSON}}": payload.get("lesson", ""),
        "{{TITLE}}":  payload.get("title", ""),
        "{{HOOK}}":   payload.get("hook", ""),
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
