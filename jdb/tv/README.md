# jdBasic TV — Lesson Recording Pipeline

End-to-end production rig for the *Train jdBasic* video series. A
single `jdbasic director.jdb <script>.txt` does the whole take:
renders all voice clips and title cards up-front, starts OBS, drives
the jdBasic IDE via simulated keystrokes, writes a manifest, then
runs FFmpeg to mux the recording, voice tracks and title-card
overlays into a finished MP4.

Persona: **Jaydee Basica** — `en-US-JennyNeural` at `+20 Hz` pitch.

---

## What's in this folder

```
director.jdb           Walks a script.txt, drives the recording.
compose.jdb            Standalone re-composer (manifest → MP4).
scripts/               Lesson scripts (script_NN_*.txt).
Intro.jsws             Workspace with the ASCII banner INIT().
CARDS.jdb              Title-card / thumbnail renderer (Playwright shim).
FFMPEG.jdb             ComposeFromManifest$ — overlays + adelay + amix.
OBS.jdb                obs-websocket bridge (start/stop/scene).
REMOTE.jdb             Win32 keyboard + window FFI helpers.
TTS.jdb                edge-tts → WAV shim.
helpers/               Python shims (obs_cmd.py, tts_render.py, render_card.py).
cards/                 HTML templates + rendered PNGs (gitignored).
voice/                 Pre-rendered WAVs per script (gitignored).
raw/                   OBS recordings + manifest (gitignored).
final/                 Muxed MP4s (gitignored).
avatar/                JayDee.vrm (gitignored, future Phase 4 work).
.env                   OBS host/port/password, voice config (gitignored).
```

---

## Prereqs

External tools the pipeline shells out to — install once, then forget:

| Tool          | Why                                   |
|---------------|---------------------------------------|
| **OBS Studio**| Records the IDE window. v28+ ships with `obs-websocket` v5 built in. |
| **Python 3**  | Drives `obs_cmd.py`, `tts_render.py`, `render_card.py`. |
| `pip install obsws-python edge-tts playwright` |  |
| `playwright install chromium` | Headless browser for HTML → PNG title cards. |
| **FFmpeg**    | `ffplay` for voice monitoring during the shoot; `ffmpeg` for the final mux. Both must be on `PATH`. |

`.env` (not committed) holds:

```
OBS_HOST=127.0.0.1
OBS_PORT=4455
OBS_PASS=<your-obs-websocket-password>
TTS_VOICE=en-US-JennyNeural
TTS_PITCH=+20Hz
TTS_RATE=+0%
```

---

## Quick start — shoot one lesson

1. Open jdBasic in interactive mode (legacy console with the `WSx>`
   prompt). Make sure the window title is exactly `jdBasic`.
2. Start OBS. Confirm the **IDE** scene is selected.
3. From a separate terminal (or via the MCP server), run:

   ```
   jdbasic director.jdb script_03_arrays.txt
   ```

   The director will:
   - Pre-render every `SAY:` line as a WAV under `voice/`.
   - Pre-render every `TITLE:` as a PNG under `cards/`.
   - Reposition the IDE window to `0,0,1920×1080`.
   - Tell OBS to start recording.
   - Type / `EDIT` / `RUN` its way through the script.
   - Stop OBS, write `raw/<script>.manifest.txt`.
   - Call `FFMPEG.ComposeFromManifest$` → `final/<script>.mp4`.

That's it — one process, one MP4 out.

If you only want to re-compose without re-recording (you tweaked
the manifest, added cards, etc.), edit `compose.jdb` to point at
the right manifest and run it directly.

---

## Script DSL

Each lesson script is a plain `.txt` file under `scripts/`. The
director reads it line by line. Empty lines and `'`-comments are
ignored. Commands recognised:

| Command                   | What it does                                              |
|---------------------------|-----------------------------------------------------------|
| `TITLE: lesson \| title \| subtitle` | Pre-renders a title-card PNG; the composer overlays it on the recording for ~2.5 s. Pipe-separated. |
| `SAY: <text>`             | Pre-rendered as a WAV via edge-tts. Plays during the shoot via `ffplay`; muxed into the final video. |
| `TYPE: <text>`            | Types `<text>` into the foreground window followed by Enter. Backslash escapes: `\n` Enter, `\t` Tab, `\b` Backspace, `\\` literal backslash (use this for Windows paths — `..\\..\\build\\jdbasic` types `..\..\build\jdbasic`). A lone `\` followed by anything else is typed verbatim. Leading whitespace **after** `TYPE: ` is preserved (used for source indentation when `OPTION "NOAUTOIDENT"` is on). |
| `CMD: EDIT \| CTRL-Q \| RUN \| ENTER` | Sends the named keystroke chord. |
| `WAIT: <seconds>`         | `SLEEP` (floats OK). |
| `LOAD: <file.jdb>`        | Reads the file, types its content into the editor. |
| `SCENE: <name>`           | Switches OBS to the named scene. Names are case-sensitive and must match OBS exactly. |
| `WINMOVE: <window-title>` | Finds a window by exact title and snaps it to `0,0,1920×1080`. Used for SDL `SCREEN` windows so they land inside the OBS Desktop-scene crop region. Retries for up to 3 s. |
| `FOCUS: <window-title>`   | Brings a named window to the foreground. Use after an SDL demo to put the IDE back on top before the next `TYPE:`. |
| `RECORD: start \| stop`   | Manual OBS control inside the script (rarely needed — the director already does start/stop). |
| `CHAPTER: <title>`        | Marks a YouTube-chapter heading for the SAY that follows. Ignored during the shoot; consumed by `upload.jdb`. The first CHAPTER is force-rebased to `00:00`. Skip if you don't want a chapter list in the description. |

Pronunciation tips for `SAY:` lines:

- Quote short keywords like `"if"`, `"for"`, `"len"` — edge-tts otherwise
  swallows them. Quoted tokens get clearer prosody.
- **Avoid em dashes (`—`)** in spoken text — edge-tts mispronounces them.
  Use commas or periods instead.
- ALL-CAPS acronyms get spelled out letter-by-letter. Lowercase
  them in `SAY:` (e.g. `"as string"`, not `"AS STRING"`).

---

## OBS scene setup

Two scenes carry the show:

### `IDE` (default)
- Source: **Window Capture** of the `jdBasic` console.
- Canvas: `1920 × 1080`.

### `Desktop` (for Lesson 9 graphics demos)
- Source: **Display Capture** of your main monitor.
- Filter: **Crop/Pad** to chop the source down to the top-left
  `1920×1080` region.
  - On a 3840×2160 monitor that means `Right = 1920`, `Bottom = 1080`,
    `Left = 0`, `Top = 0`.
- Transform: **Fit to screen** so the crop fills the canvas.

The director resizes the jdBasic IDE to exactly `0,0,1920×1080` on
startup, and `WINMOVE:` snaps SDL `SCREEN` windows into the same
rectangle. Whatever scene OBS is currently on, the picture lines up.

Recording output path: set OBS → Settings → Output → Recording Path
to `<repo>/jdb/tv/raw/`. The pipeline expects raw files to land there.

---

## Composing manually

`compose.jdb` is the standalone post-prod entry point. Edit the
`MANIFEST$` and `OUT$` constants at the top, then:

```
jdbasic compose.jdb
```

Useful when you want to re-mux a take with new cards, swap a voice
clip, or experiment with `FFMPEG.ComposeFromManifest$`'s filter
graph without re-recording.

Manifest format (tab-separated, one row per asset):

```
RAW: <obs-output.mp4>
SCRIPT: <script-name>.txt
# offset_ms  dur_ms  wav_or_png_path
VOICE  0       3420    voice/script_03_arrays_000.wav
CARD   0       2500    cards/script_03_arrays_title_000.png
VOICE  3500    4180    voice/script_03_arrays_001.wav
...
```

`FFMPEG.ComposeFromManifest$` chains all `CARD` rows as ordered
overlays with `enable='between(t, start, end)'`, and `adelay`-pads
every `VOICE` row before `amix` to a single audio stream. Final
encode is `libx264` / `aac` on a muted-desktop base track.

---

## Known gotchas

- **Module file names are case-sensitive lower.** `mathx.jdb` is
  loaded by `IMPORT MATHX`; `MathX.jdb` won't be found.
- **AltGr characters** (`[`, `]`, `{`, `}`, `@`, `\`, `|`, `~`, `€`)
  are handled correctly thanks to the EXTENDEDKEY flag in
  `REMOTE.TypeChar`. If you add a new layout-specific char and it
  doesn't type, that's the first place to look.
- **`SLEEP` inside a SDL program** blocks the window-message pump.
  Use a render loop (`GFX.POLLEVENT()` + `SLEEP 16` over N frames)
  if you want `WINMOVE:` to take effect during the demo.
- **`OPTION "NOAUTOIDENT"`** is flipped on by `Intro.jsws`'s `INIT()`
  during the director's prelude. Every `TYPE: <code>` then preserves
  whatever leading whitespace you wrote — handy for hand-indenting
  block forms (`IF` / `FOR` / `FUNC` bodies). Don't `OPTION "AUTOIDENT"`
  again mid-script unless you really want the IDE to re-indent.
- **Empty `TYPE:` line** types just Enter — useful for visual
  blank lines inside the editor buffer. Doesn't affect the final
  output beyond appearance in `LIST`.

---

## Phases

- ✅ **Phase 1**: end-to-end pipeline (TTS + OBS + IDE-driver + FFmpeg).
- ✅ **Phase 2**: branded title-card overlays via Playwright + HTML.
- ✅ **Phase 3**: lessons 00 → 10 shot and rendered.
- ✅ **Phase 5**: YouTube upload via `upload.jdb` + `youtubeuploader.exe`.
- ⏳ **Phase 4**: virtual Jaydee Basica avatar (`avatar/JayDee.vrm`,
  VSeeFace lip-sync to the WAVs, OBS source).

---

## YouTube upload workflow

After a shoot, `final/<base>.mp4` is the muxed master.  `upload.jdb`
publishes it to YouTube **as `private`** with auto-generated chapters,
a dedicated 1280×720 thumbnail and tags pulled from a per-lesson
Markdown file.  You flip the privacy from `private` → `public` in
YouTube Studio when you're ready to publish.

### One-time setup

1. **Google Cloud Console** → create an OAuth client of type
   "Desktop App", download the JSON.  Drop it at
   `tv/yt/client_secret_*.json` (the exact filename Google generated
   is fine — `.gitignore` matches `client_secret*.json`).
2. **Update `tv/.env`** so `YT_CLIENT_SECRETS` points at that file.
3. **Install the Python deps** for the playlist-creation helper
   (the uploader itself is Go and needs nothing extra):

   ```
   pip install google-auth-oauthlib google-api-python-client
   ```

4. **Create the series playlist** (interactive — opens a browser):

   ```
   jdbasic yt/create_playlist.jdb
   ```

   First run opens the OAuth consent screen; subsequent runs are
   no-ops.  The new playlist ID is written into
   `yt/defaults.json`.

### Per-lesson — write the description

For each `scripts/<base>.txt`, drop a sibling `scripts/<base>.md`
with frontmatter + body:

```markdown
---
title: Train jdBasic — Lesson 11 — REPL Workflow
hook:  Tools that make you fast
tags:  repl, tooling, pretty, lint
---

Body paragraph — appears under the auto-generated chapter list in
the YouTube description.  Markdown is allowed; YouTube renders it
as plain text with links auto-linkified.
```

Mandatory keys: **`title`**.  Optional: `hook` (one-liner that goes
on the thumbnail), `tags` (comma-separated, merged with the series
defaults from `yt/defaults.json`).

### Optional — mark chapters in the script

Add `CHAPTER: <title>` lines to `scripts/<base>.txt` directly before
the SAY where each chapter should start.  The director ignores them
during the shoot; `upload.jdb` correlates them with the VOICE-row
offsets in the manifest to produce the YouTube chapter block.  No
markers → no chapter block (everything else works).

```
CHAPTER: Intro
SAY: Welcome back to Train jdBasic…

CHAPTER: PRETTY in 60 seconds
SAY: Watch what pretty does…
```

The first chapter is force-rebased to `00:00` (YouTube requirement).

### Upload

```
jdbasic upload.jdb script_11_repl_workflow      # one lesson
jdbasic upload.jdb --all                        # every final/*.mp4
                                                # missing a marker
```

After a successful upload, `final/<base>.uploaded.json` is written
(YouTube's metadata for the new video).  Re-running with the same
base skips with a SKIP message.  Delete the marker file to force
re-upload.

`final/<base>.thumb.png` is rendered on the first upload via
`CARDS.RenderThumbnail`; subsequent re-renders need a manual delete.

The first ever upload triggers the youtubeuploader's OAuth flow
(browser consent → `tv/yt/request.token`).  Every subsequent upload
runs unattended.
