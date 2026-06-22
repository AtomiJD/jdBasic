# jdBasic TV - Lesson Recording Pipeline

End-to-end production rig for the *Train jdBasic* video series. A
single `jdbasic director.jdb <script>.txt` does the whole take:
renders all voice clips and title cards up-front, starts OBS, drives
the jdBasic IDE via simulated keystrokes, writes a manifest, then
runs FFmpeg to mux the recording, voice tracks and title-card
overlays into a finished MP4.

Persona: **Jaydee Basica** - `en-US-JennyNeural` at `+20 Hz` pitch.

---

## What's in this folder

```
director.jdb           Walks a script.txt, drives the recording.
compose.jdb            Standalone re-composer (manifest → MP4).
upload.jdb             YouTube uploader (per-lesson .md → metaJSON → API).
scripts/               Lesson scripts (script_NN_*.txt + sibling .md descriptions).
Intro.jsws             Workspace with the ASCII banner INIT().
CARDS.jdb              Title-card / thumbnail renderer (Playwright shim).
FFMPEG.jdb             ComposeFromManifest$ - overlays + adelay + amix.
OBS.jdb                obs-websocket bridge (start/stop/scene).
REMOTE.jdb             Win32 keyboard + window FFI helpers.
TTS.jdb                edge-tts → WAV shim.
helpers/               Python shims (obs_cmd.py, tts_render.py, render_card.py,
                       yt_create_playlist.py, yt_add_to_playlist.py).
yt/                    YouTube config - defaults.json (gitignored, per-user),
                       defaults.example.json (template), client_secret*.json
                       (gitignored), request.token / playlist.token (gitignored).
cards/                 HTML templates + rendered PNGs (gitignored).
voice/                 Pre-rendered WAVs per script (gitignored).
raw/                   OBS recordings + manifest (gitignored).
final/                 Muxed MP4s + per-lesson thumbnail PNGs + uploaded-marker
                       JSONs (gitignored).
avatar/                JayDee.vrm (gitignored, future Phase 4 work).
.env                   Local config (OBS, voice, YouTube paths). Gitignored.
```

---

## Prereqs

External tools the pipeline shells out to - install once, then forget:

| Tool          | Why                                   |
|---------------|---------------------------------------|
| **OBS Studio**| Records the IDE window. v28+ ships with `obs-websocket` v5 built in. |
| **Python 3**  | Drives `obs_cmd.py`, `tts_render.py`, `render_card.py`, and the YouTube helpers. |
| `pip install obsws-python edge-tts playwright` | Recording, voice, title cards. |
| `pip install google-auth-oauthlib google-api-python-client` | Only needed if you'll upload to YouTube. |
| `playwright install chromium` | Headless browser for HTML → PNG title cards. |
| **FFmpeg**    | `ffplay` for voice monitoring during the shoot; `ffmpeg` for the final mux. Both must be on `PATH`. |
| **youtubeuploader.exe** | Go binary that does the actual MP4 push to YouTube. Grab a release from https://github.com/porjo/youtubeuploader - drop the `.exe` anywhere on disk and point `YT_UPLOADER` in `.env` at it. Only needed for the YouTube workflow. |

`.env` (not committed, you create it from the template below) holds
everything machine- or account-specific:

```
# OBS WebSocket (Settings → WebSocket Server → Generate Password)
OBS_HOST=127.0.0.1
OBS_PORT=4455
OBS_PASS=<your-obs-websocket-password>

# edge-tts voice preset (en-US-* catalogue, see `edge-tts --list-voices`)
TTS_VOICE=en-US-JennyNeural
TTS_PITCH=+20Hz
TTS_RATE=+0%

# YouTube workflow - paths only, no secrets in here. See the
# "YouTube upload workflow" section below for the one-time setup.
YT_UPLOADER=C:\path\to\youtubeuploader.exe
YT_CLIENT_SECRETS=yt\client_secret_<your-google-id>.json
YT_TOKEN_CACHE=yt\request.token
YT_DEFAULTS=yt\defaults.json
```

---

## Quick start - shoot one lesson

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

That's it - one process, one MP4 out.

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
| `TYPE: <text>`            | Types `<text>` into the foreground window followed by Enter. Backslash escapes: `\n` Enter, `\t` Tab, `\b` Backspace, `\\` literal backslash (use this for Windows paths - `..\\..\\build\\jdbasic` types `..\..\build\jdbasic`). A lone `\` followed by anything else is typed verbatim. Leading whitespace **after** `TYPE: ` is preserved (used for source indentation when `OPTION "NOAUTOIDENT"` is on). |
| `CMD: EDIT \| CTRL-Q \| RUN \| ENTER` | Sends the named keystroke chord. |
| `WAIT: <seconds>`         | `SLEEP` (floats OK). |
| `LOAD: <file.jdb>`        | Reads the file, types its content into the editor. |
| `SCENE: <name>`           | Switches OBS to the named scene. Names are case-sensitive and must match OBS exactly. |
| `WINMOVE: <window-title>` | Finds a window by exact title and snaps it to `0,0,1920×1080`. Used for SDL `SCREEN` windows so they land inside the OBS Desktop-scene crop region. Retries for up to 3 s. |
| `FOCUS: <window-title>`   | Brings a named window to the foreground. Use after an SDL demo to put the IDE back on top before the next `TYPE:`. |
| `RECORD: start \| stop`   | Manual OBS control inside the script (rarely needed - the director already does start/stop). |
| `CHAPTER: <title>`        | Marks a YouTube-chapter heading for the SAY that follows. Ignored during the shoot; consumed by `upload.jdb`. The first CHAPTER is force-rebased to `00:00`. Skip if you don't want a chapter list in the description. |

Pronunciation tips for `SAY:` lines:

- Quote short keywords like `"if"`, `"for"`, `"len"` - edge-tts otherwise
  swallows them. Quoted tokens get clearer prosody.
- **Avoid em dashes (`-`)** in spoken text - edge-tts mispronounces them.
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
  whatever leading whitespace you wrote - handy for hand-indenting
  block forms (`IF` / `FOR` / `FUNC` bodies). Don't `OPTION "AUTOIDENT"`
  again mid-script unless you really want the IDE to re-indent.
- **Empty `TYPE:` line** types just Enter - useful for visual
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

After a shoot, `final/<base>.mp4` is the muxed master. `upload.jdb`
publishes it to YouTube **as `private`** with auto-generated chapters,
a dedicated 1280×720 thumbnail, and tags + description pulled from a
per-lesson Markdown sidecar. You flip privacy `private` → `public` in
YouTube Studio when you're ready to ship.

The whole thing is split into a **one-time YouTube setup** (an
afternoon, mostly in the Google Cloud Console), a **one-time pipeline
setup** (five minutes, copy a config and run one helper), and a tiny
**per-lesson loop** (write a `.md`, mark chapters, run `upload.jdb`).

### 1. One-time YouTube + Google Cloud setup

You'll do this once per Google account / channel.

**1.1 - Create a Google Cloud project.** Go to
https://console.cloud.google.com/, pick a name like "jdBasic-TV" (any
name works, it's a private identifier).

**1.2 - Enable the YouTube Data API v3.** APIs & Services → Library →
search "YouTube Data API v3" → **Enable**. Without this the playlist
helper returns a cryptic 403 `accessNotConfigured`.

**1.3 - Configure the OAuth consent screen.** APIs & Services → OAuth
consent screen → **External** → fill app name, user-support email,
developer contact. Add scope **`https://www.googleapis.com/auth/youtube`**.
Save.

**1.4 - Add yourself as a Test User.** Same screen → **Test users** →
add the Google address of the channel you'll upload to. Without this
the consent flow returns `Error 403: access_denied`. Note: Google
canonicalises addresses - your account may show as `@gmail.com` even
if you signed up under `@googlemail.com`. If "Ineligible accounts not
added" fires, check https://myaccount.google.com/ for the canonical
form.

**1.5 - Create an OAuth Client.** Credentials → **+ Create credentials**
→ **OAuth client ID** → type **Desktop app** → name it → Create.
Download the JSON, drop it at `jdb/tv/yt/client_secret_*.json`
(`.gitignore` matches `client_secret*.json` so the secret stays local).

**1.6 - Patch `redirect_uris` for port 8080.** The Google-issued
client_secret JSON ships with `"redirect_uris": ["http://localhost"]`
(no port). `youtubeuploader.exe` reads that string verbatim and tries
to listen on port 80, which fails. Open the JSON and replace that
field with **both** entries so the port-8080 variant comes first:

```json
"redirect_uris": ["http://localhost:8080", "http://localhost"]
```

Save. Google Desktop OAuth allows any `127.0.0.1` / `localhost` port
without separate registration, so the local edit is enough.

**1.7 - Verify your YouTube channel for custom thumbnails (optional
but recommended).** YouTube → https://youtube.com/verify → phone
verification. Without this, custom thumbnail uploads fail with
HTTP 403 (the video still goes up, just without the rendered card).

### 2. One-time pipeline setup

**2.1 - Install the Python deps** (uploader itself is Go, no
dependencies):

```
pip install google-auth-oauthlib google-api-python-client
```

**2.2 - Grab youtubeuploader.exe.** Latest release at
https://github.com/porjo/youtubeuploader. Drop the binary anywhere on
disk; it doesn't need to be on PATH.

**2.3 - Create `tv/.env`.** Use the template in the Prereqs section
above. The four `YT_*` lines must all be set - point `YT_UPLOADER` at
the binary you just downloaded and `YT_CLIENT_SECRETS` at the JSON
from step 1.5.

**2.4 - Copy and edit `yt/defaults.json`.**

```
cd jdb/tv/yt
cp defaults.example.json defaults.json
```

Edit the new file: set `channelTitle`, default `tags`, and rewrite
`descriptionFooter` (the boilerplate that gets appended to every
upload's description). Leave `playlistID` empty - the next step
fills it in.

**2.5 - Create the series playlist.** Interactive (opens a browser
for OAuth consent):

```
jdbasic yt/create_playlist.jdb
```

First run pops the consent screen on `localhost:<random-port>`, asks
for the YouTube scope, then writes `tv/yt/playlist.token` and the new
playlist ID into `yt/defaults.json`. Re-runs are no-ops while the
playlist ID stays set.

You can pass a different title / description / privacy:

```
jdbasic yt/create_playlist.jdb "My Channel Series" "My description" unlisted
```

### 3. Per-lesson loop

**3.1 - Write a sidecar `.md`** next to your `.txt` script. Frontmatter
controls the upload metadata, body becomes the description.

```markdown
---
title: My Series - Lesson 03 - Arrays
hook: Drop the FOR loop, work on the whole array
tags: arrays, vector, broadcast, beginner, lesson-3
---

Lesson 03 - where the language really starts to feel different from
the BASICs you remember. Arrays are first-class citizens, and most
operations work on the whole array at once.

## What you'll learn
- ...

## Code from the lesson
... (a fenced code block - YouTube renders it as plain text)

## Next up
Lesson 04 - strings.
```

**Required:** `title`. **Optional:** `hook` (one-liner overlaid on the
thumbnail), `tags` (comma-separated, merged with the defaults from
`yt/defaults.json` - duplicates dropped case-insensitively).

**3.2 - Mark chapters in the script** (optional). Add `CHAPTER: <text>`
lines directly before the SAY where each chapter should start. The
director ignores them during the shoot; `upload.jdb` correlates them
with the VOICE-row offsets in the manifest to build the YouTube
chapter block. Skip entirely if you don't want chapters - everything
else still works.

```
CHAPTER: Intro
SAY: Welcome back to my series...

CHAPTER: Broadcasting in 60 seconds
SAY: Watch what scalar broadcast does...
```

The first `CHAPTER:` is force-rebased to `00:00` (YouTube requires
chapter one to start there).

**3.3 - Dry-run to inspect the metaJSON** before burning upload
bandwidth:

```
jdbasic upload.jdb --dry-run <script_base>
```

Writes `yt/_tmp_<script_base>.json` and prints the description preview
+ merged tags. Sanity-check the chapters, hashtags, and verify nothing
weird made it through.

**3.4 - Upload.**

```
jdbasic upload.jdb <script_base>      # one lesson
jdbasic upload.jdb --all              # every final/*.mp4 missing a marker
```

The first upload ever triggers a **second** OAuth flow (youtubeuploader
has its own token cache, `tv/yt/request.token`, separate from the
playlist-creation token because Go and Python serialise OAuth state
differently). After that, all uploads run unattended.

After success, `final/<base>.uploaded.json` is written (YouTube's API
response - captures the video ID and final metadata). Re-running on
the same base SKIPs with a message. Delete the marker to force a
re-upload. The thumbnail is rendered to `final/<base>.thumb.png` on
first upload; delete that file to force re-rendering.

### Operator reference

**`upload.jdb` modes:**

| Command | Effect |
|---|---|
| `upload.jdb <base>` | Render thumb + upload + append to playlist |
| `upload.jdb --all` | Walk `final/*.mp4`, upload anything without a `.uploaded.json` marker |
| `upload.jdb --dry-run <base>` | Build the metaJSON, write it to `yt/_tmp_*`, print the description; no API calls |
| `upload.jdb --render-thumb <base>` | (Re-)render the 1280×720 thumbnail only - no upload |

**Helper scripts:**

| Script | Purpose |
|---|---|
| `yt/create_playlist.jdb` | One-time playlist setup |
| `helpers/yt_create_playlist.py` | Python OAuth + Data API call behind the above |
| `helpers/yt_add_to_playlist.py` | Post-upload retry-loop for `playlistItems.insert` (handles the 5-30s propagation window before YouTube's playlist endpoint sees a fresh upload) |

**Text rules YouTube enforces (the upload would otherwise 400):**

- **No `<` or `>` in title or description.** YouTube treats them as
  XSS-blocked, not HTML-decode. `upload.jdb` pre-flights the metaJSON
  and aborts with a clear message before the upload starts - the
  cure is to rephrase your `.md`. Code blocks count too: descriptions
  are plain text to the API.
- **No em dash (`-`, U+2014).** The pipeline doesn't strictly require
  this, but every TV file in this repo uses plain `-`. edge-tts
  mispronounces the character, the CARDS renderer historically had
  Windows-CP1252 encoding pitfalls around it, and YouTube descriptions
  with em-dashes have rendered as `?` characters in past runs. Plain
  ASCII hyphen, always.

**Common failures + fixes:**

| Symptom | Fix |
|---|---|
| `accessNotConfigured` (HTTP 403) | YouTube Data API v3 not enabled in your GCP project - see step 1.2 |
| `access_denied` (HTTP 403) on the consent screen | Your Google account isn't on the OAuth consent screen's **Test users** list - step 1.4 |
| Browser shows `localhost refused to connect` after consent | `redirect_uris[0]` in `client_secret_*.json` is `http://localhost` (no port). Edit it to `http://localhost:8080` first - step 1.6 |
| Upload succeeds, thumbnail fails with `forbidden` | Channel isn't phone-verified - step 1.7 |
| `invalidDescription` (HTTP 400) | Title or description contains `<` or `>` - rephrase the `.md`. Pre-flight should catch this before upload |
| Marker JSON written but exit code != 0 | Likely the playlist-add race; `upload.jdb` retries via `yt_add_to_playlist.py`. If that also fails, drag the video into the playlist manually in Studio |
| 7-day token expiry (`invalid_grant`) | Unverified apps in Testing mode expire refresh tokens after 7 days. Delete `tv/yt/request.token` and/or `tv/yt/playlist.token` and re-run; OAuth consent flow restarts |
