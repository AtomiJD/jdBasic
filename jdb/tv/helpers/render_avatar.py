#!/usr/bin/env python3
"""
render_avatar.py - render the VRM avatar offline, lip-synced to the
VOICE clips of a manifest, as a transparent VP9 .webm.

Usage:
    python render_avatar.py render <manifest.txt> <out.webm> <duration_s> [speaking|always]
    python render_avatar.py still  <out.png> [<background.png> <x> <y>]

The mouth follows each clip's loudness; the spectral centroid picks the
vowel shape (low: ou/oh, middle: aa, high: ih/ee). Blinks, breathing and
a slow head sway run on the clock. In "speaking" mode the avatar fades
in a little before each clip and out after it, and is transparent
between clips.

The page helpers/avatar.html loads three.js and three-vrm from a CDN;
the model and the page itself are served from disk through a Playwright
route on http://avatar.local/.
"""
import sys, pathlib, wave, random, subprocess
import numpy as np

FPS = 30
W, H = 480, 640
FADE_S = 0.35
HOLD_S = 0.6

here = pathlib.Path(__file__).resolve().parent
tv = here.parent
model_path = tv / "avatar" / "JayDee.vrm"


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        n, rate, width, chans = w.getnframes(), w.getframerate(), w.getsampwidth(), w.getnchannels()
        raw = w.readframes(n)
    dtype = {1: np.uint8, 2: np.int16, 4: np.int32}[width]
    a = np.frombuffer(raw, dtype=dtype).astype(np.float32)
    if width == 1:
        a = a - 128.0
    a /= float(np.abs(a).max() or 1.0)
    if chans > 1:
        a = a.reshape(-1, chans).mean(axis=1)
    return a, rate


def clip_mouth(path):
    """One {aa, ih, ou, ee, oh} weight map per video frame of the clip."""
    a, rate = read_wav(path)
    hop = rate // FPS
    frames = len(a) // hop + 1
    rms = np.zeros(frames)
    cen = np.zeros(frames)
    win = np.hanning(hop * 2)
    freqs = np.fft.rfftfreq(hop * 2, 1.0 / rate)
    for f in range(frames):
        seg = a[f * hop: f * hop + hop * 2]
        if len(seg) < hop * 2:
            seg = np.pad(seg, (0, hop * 2 - len(seg)))
        rms[f] = np.sqrt(np.mean(seg ** 2))
        spec = np.abs(np.fft.rfft(seg * win))
        band = (freqs > 200) & (freqs < 4000)
        s = spec[band]
        cen[f] = (freqs[band] * s).sum() / (s.sum() or 1.0)
    ref = np.percentile(rms[rms > 0.01], 90) if (rms > 0.01).any() else 1.0
    level = np.clip((rms - 0.02) / (ref - 0.02 or 1.0), 0.0, 1.0) ** 0.8
    out = []
    open_ = 0.0
    for f in range(frames):
        target = level[f]
        open_ += (target - open_) * (0.65 if target > open_ else 0.35)
        c = cen[f]
        m = {"aa": 0.0, "ih": 0.0, "ou": 0.0, "ee": 0.0, "oh": 0.0}
        if c < 900:
            m["ou"], m["oh"] = 0.55, 0.45
        elif c < 1300:
            m["oh"], m["aa"] = 0.5, 0.5
        elif c < 1900:
            m["aa"] = 1.0
        elif c < 2500:
            m["aa"], m["ee"] = 0.4, 0.6
        else:
            m["ih"], m["ee"] = 0.6, 0.4
        out.append({k: round(v * open_, 3) for k, v in m.items()})
    return out


def blink_track(n_frames, seed=7):
    rnd = random.Random(seed)
    track = np.zeros(n_frames)
    f = int(FPS * rnd.uniform(1.0, 3.0))
    shape = [0.5, 1.0, 1.0, 0.6, 0.2]
    while f < n_frames:
        for k, v in enumerate(shape):
            if f + k < n_frames:
                track[f + k] = v
        f += int(FPS * rnd.uniform(2.0, 5.0))
    return track


def open_page(p):
    browser = p.chromium.launch(args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader"])
    ctx = browser.new_context(viewport={"width": W, "height": H}, device_scale_factor=1)
    page = ctx.new_page()

    def serve(route):
        name = route.request.url.split("avatar.local/", 1)[1].split("?", 1)[0]
        src = model_path if name == "JayDee.vrm" else here / name
        ctype = "text/html" if name.endswith(".html") else "application/octet-stream"
        route.fulfill(status=200, body=src.read_bytes(), headers={"Content-Type": ctype})

    page.route("http://avatar.local/**", serve)
    page.goto(f"http://avatar.local/avatar.html?w={W}&h={H}&model=http://avatar.local/JayDee.vrm")
    page.wait_for_function("window.avatarReady === true || window.avatarError !== undefined", timeout=120000)
    err = page.evaluate("window.avatarError")
    if err:
        print(f"ERROR: avatar load failed: {err}", file=sys.stderr)
        sys.exit(5)
    return browser, page


def shot(page, t, mouth, blink, opacity):
    page.evaluate("([t, m, b, o]) => window.setFrame(t, m, b, o)", [t, mouth, blink, opacity])
    return page.screenshot(omit_background=True, clip={"x": 0, "y": 0, "width": W, "height": H})


def parse_manifest(path):
    base = pathlib.Path(path).resolve().parent.parent
    clips = []
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        cols = line.split("\t")
        if cols[0] == "VOICE" and len(cols) >= 4:
            clips.append((int(cols[1]) / 1000.0, int(cols[2]) / 1000.0, base / cols[3].replace("\\", "/")))
    return clips


def cmd_render(manifest, out_path, duration, mode):
    from playwright.sync_api import sync_playwright
    n_frames = int(duration * FPS) + 1
    mouth = [None] * n_frames
    opacity = np.zeros(n_frames) if mode == "speaking" else np.ones(n_frames)
    for start, length, wav in parse_manifest(manifest):
        f0 = int(start * FPS)
        for k, m in enumerate(clip_mouth(wav)):
            if 0 <= f0 + k < n_frames:
                mouth[f0 + k] = m
        if mode == "speaking":
            a, b = start - FADE_S, start + length + HOLD_S
            for f in range(max(0, int(a * FPS)), min(n_frames, int((b + FADE_S) * FPS) + 1)):
                t = f / FPS
                v = min(1.0, (t - a) / FADE_S, (b + FADE_S - t) / FADE_S)
                opacity[f] = max(opacity[f], max(0.0, v))
    blinks = blink_track(n_frames)

    out_path = pathlib.Path(out_path).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    ff = subprocess.Popen(
        ["ffmpeg", "-y", "-loglevel", "error", "-f", "image2pipe", "-framerate", str(FPS),
         "-c:v", "png", "-i", "-", "-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p",
         "-deadline", "realtime", "-cpu-used", "8", "-b:v", "2M", str(out_path)],
        stdin=subprocess.PIPE)
    with sync_playwright() as p:
        browser, page = open_page(p)
        blank = None
        for f in range(n_frames):
            if opacity[f] <= 0.0:
                if blank is None:
                    blank = shot(page, f / FPS, {}, 0.0, 0.0)
                ff.stdin.write(blank)
            else:
                ff.stdin.write(shot(page, f / FPS, mouth[f] or {}, float(blinks[f]), float(opacity[f])))
            if f % (FPS * 10) == 0:
                print(f"  avatar {f / FPS:.0f}s / {duration:.0f}s", flush=True)
        browser.close()
    ff.stdin.close()
    if ff.wait() != 0:
        print("ERROR: ffmpeg failed", file=sys.stderr)
        sys.exit(6)
    print(f"{out_path}  ({n_frames} frames)")


def cmd_still(out_png, bg=None, x=0, y=0):
    from playwright.sync_api import sync_playwright
    with sync_playwright() as p:
        browser, page = open_page(p)
        for f in range(30):
            shot(page, f / FPS, {"aa": 0.0}, 0.0, 1.0)
        png = shot(page, 1.0, {"aa": 0.45, "oh": 0.2}, 0.0, 1.0)
        browser.close()
    out_png = pathlib.Path(out_png).resolve()
    if bg is None:
        out_png.write_bytes(png)
    else:
        tmp = out_png.with_suffix(".avatar.png")
        tmp.write_bytes(png)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(bg), "-i", str(tmp),
                        "-filter_complex", f"[0][1]overlay={x}:{y}", "-frames:v", "1", str(out_png)], check=True)
    print(out_png)


if __name__ == "__main__":
    if len(sys.argv) >= 5 and sys.argv[1] == "render":
        cmd_render(sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5] if len(sys.argv) > 5 else "speaking")
    elif len(sys.argv) in (3, 6) and sys.argv[1] == "still":
        cmd_still(*sys.argv[2:3], *(sys.argv[3:4]), *(int(v) for v in sys.argv[4:6]))
    else:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
