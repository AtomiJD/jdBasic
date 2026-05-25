"""
trim_parallax.py - Bring all jdb/art/p_*.png onto the same 1280x756 canvas
so the parallax demo can draw them at native size without scaling.

Per layer we pick an anchor:
  stretch  - ignore aspect, fill 1280x756 (sky-like backdrops)
  top      - scale to width=1280 keeping aspect, anchor at top
  bottom   - scale to width=1280 keeping aspect, anchor at bottom
  center   - scale to width=1280 keeping aspect, anchor in vertical middle

Cropping happens when scaled_h > target; transparent padding fills the rest.

Run:  python trim_parallax.py
Out:  jdb/parallax_game/art/<normalized_name>.png
"""

from pathlib import Path
from PIL import Image

# --- Config ----------------------------------------------------------------
TARGET_W = 1280
TARGET_H = 756

SRC_DIR = Path(__file__).resolve().parents[1] / "art"          # jdb/art
DST_DIR = Path(__file__).resolve().parent / "art"              # jdb/parallax_game/art
DST_DIR.mkdir(parents=True, exist_ok=True)

# One entry per p_<key> input image; value = anchor mode.
# Unknown files in SRC_DIR get a default of "bottom" (most parallax layers
# care about keeping their ground line).
LAYERS = {
    "p_sky":      "stretch",
    "p_clouds":   "top",
    "p_mountain": "bottom",
    "p_hill":     "bottom",
    "p_grass":    "bottom",
}

# Normalize odd source names. Keys are stems (no .png), values are stems.
RENAME = {
    "p_hill.png":  "p_hill",     # double-extension fix (p_hill.png.png)
    "p_grass1":    "p_grass",
}

# --- Helpers ---------------------------------------------------------------

def normalize_stem(path: Path) -> str:
    """Map an input file to the canonical stem the .jdb script will load."""
    stem = path.stem                       # 'p_hill.png' for 'p_hill.png.png'
    if stem in RENAME:
        return RENAME[stem]
    if stem.endswith(".png"):              # paranoid second pass
        stem = stem[:-4]
    return stem


def fit_to_canvas(img: Image.Image, mode: str) -> Image.Image:
    """Scale + place onto a transparent TARGET_W x TARGET_H canvas."""
    if mode == "stretch":
        return img.resize((TARGET_W, TARGET_H), Image.LANCZOS).convert("RGBA")

    # Aspect-preserving: scale so width matches the canvas.
    aspect = img.height / img.width
    new_w = TARGET_W
    new_h = max(1, round(TARGET_W * aspect))
    scaled = img.resize((new_w, new_h), Image.LANCZOS).convert("RGBA")

    canvas = Image.new("RGBA", (TARGET_W, TARGET_H), (0, 0, 0, 0))

    if new_h >= TARGET_H:
        # Crop: pick a Y window based on the anchor.
        if mode == "top":
            y0 = 0
        elif mode == "center":
            y0 = (new_h - TARGET_H) // 2
        else:  # bottom
            y0 = new_h - TARGET_H
        cropped = scaled.crop((0, y0, TARGET_W, y0 + TARGET_H))
        canvas.paste(cropped, (0, 0), cropped)
    else:
        # Pad: place the scaled image on the canvas at the anchor row.
        if mode == "top":
            paste_y = 0
        elif mode == "center":
            paste_y = (TARGET_H - new_h) // 2
        else:  # bottom
            paste_y = TARGET_H - new_h
        canvas.paste(scaled, (0, paste_y), scaled)

    return canvas


# --- Main ------------------------------------------------------------------

def main():
    if not SRC_DIR.exists():
        raise SystemExit(f"Source dir not found: {SRC_DIR}")

    inputs = sorted(SRC_DIR.glob("p_*.png"))
    if not inputs:
        raise SystemExit(f"No p_*.png in {SRC_DIR}")

    print(f"Trimming {len(inputs)} layers to {TARGET_W}x{TARGET_H} -> {DST_DIR}")
    for src in inputs:
        stem = normalize_stem(src)
        mode = LAYERS.get(stem, "bottom")
        with Image.open(src) as im:
            out = fit_to_canvas(im, mode)
        dst = DST_DIR / f"{stem}.png"
        out.save(dst, "PNG", optimize=True)
        print(f"  {src.name:24s}  ->  {dst.name:14s}  [{mode}]  "
              f"native {im.size[0]}x{im.size[1]}")

    print("done.")


if __name__ == "__main__":
    main()
