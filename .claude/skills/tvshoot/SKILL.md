---
name: tvshoot
description: Author a jdBasic-TV lesson - write scripts/<base>.txt (TITLE/SAY/TYPE/CMD/WAIT/CHAPTER) + scripts/<base>.md description sidecar, then drive director.jdb to record + mux a final MP4. Triggered when Atomi asks for "a new lesson", "shoot episode X on Y", "record a Train jdBasic video about Z", etc.
---

# Train jdBasic - Lesson Shoot

Use this whenever **Atomi** asks for a new lesson video on a specific
topic, or to re-shoot an existing one. The skill covers everything
from the empty page to a finished `final/<base>.mp4` ready for
`/tvupload`. It does NOT push to YouTube - that's `/tvupload`'s job.

Working dir: `D:\usr\dev\cc\jdb\tv` (Bash: `/d/usr/dev/cc/jdb/tv`).

## What you produce per lesson

```
scripts/<base>.txt        - the recording script (director DSL)
scripts/<base>.md         - frontmatter (title/hook/tags) + body (YouTube description)
final/<base>.mp4          - muxed master (post-recording)
raw/<base>.manifest.txt   - voice + card asset timings (written by director.jdb)
```

`<base>` convention: `script_NN_short_topic` (e.g. `script_15_async`). Two-
digit lesson number + lowercase underscored topic. The number sets the
upload order and matches the title-card numbering.

## Step 1 - Pin down the topic with Atomi

If the request is vague ("make a lesson about strings"), ask one
clarifying question to set the scope:

- **What's the one thing the viewer should walk away with?** (one
  sentence - becomes the `hook:` in the `.md`)
- **Target runtime?** Default 5–7 minutes = ~10–12 SAY blocks + 3–6
  TYPE/RUN demos.

Skip the question if Atomi already gave you both pieces in the
request.

## Step 2 - Draft `scripts/<base>.txt` (the recording script)

Open with the lesson header + TITLE card:

```
' ──────────────────────────────────────────────────────────────────
'  Lesson NN - Topic
'  Topic: short summary of what's covered
'  Target runtime: ~N minutes
' ──────────────────────────────────────────────────────────────────

TITLE: NN | Short Title | Subtitle
```

Then alternate SAY (narration) + TYPE/CMD/WAIT (the actual demo):

| Directive | What it does |
|---|---|
| `TITLE: NN \| Title \| Subtitle` | Renders a 1920×1080 title card PNG, overlaid for ~2.5s |
| `SAY: <text>` | Pre-rendered as a WAV via edge-tts; plays during the take |
| `TYPE: <text>` | Types into the foreground window + Enter. Backslash escapes: `\n` `\t` `\b` `\\` |
| `CMD: EDIT \| CTRL-Q \| RUN \| ENTER` | Keystroke chord |
| `WAIT: <seconds>` | SLEEP (floats OK, used between demos) |
| `LOAD: <file>` | Types the file's content into the editor |
| `CHAPTER: <title>` | Marks a YouTube chapter heading for the NEXT SAY |
| `SCENE: <name>` | Switches OBS scene (case-sensitive: `IDE`, `Desktop`) |
| `WINMOVE: <title>` | Snaps a named window to 1920×1080 at 0,0 (for SDL demos) |
| `FOCUS: <title>` | Brings a window to the foreground (after SDL window closes) |

Drop a **`CHAPTER:`** marker before every SAY where a new topic
section begins. Target 6–9 chapters per lesson - they become the
YouTube chapter list.

### Pronunciation rules for SAY: lines

These are tribal knowledge - edge-tts misses things if you don't.

- **Quote short keywords**: `"if"`, `"for"`, `"len"`, `"sub"`. Without
  quotes they get swallowed into the previous word.
- **Spell out single-letter keywords**: `"as string"`, not `"AS STRING"`.
  All-caps acronyms get spelled letter-by-letter, which sounds wrong.
- **No em-dash `-`** anywhere in scripts/.txt or scripts/.md. Plain
  `-` only - em-dashes are mispronounced AND break cmd.exe arg paths
  in the cards renderer. See `feedback_no_em_dash.md` in memory.
- **Hyphenate spoken multiword commands**: `"end-if"`, `"draw-color"`,
  `"txt-writer"`. Reads naturally.
- **No `<` or `>`** anywhere in SAY content if it could end up in the
  YouTube description. Rephrase: "less than", "greater than", etc.
  See `feedback_youtube_no_angle_brackets.md`. The `.md` body
  inherits these constraints - pre-flight in upload.jdb catches it,
  but cheaper to write right the first time.

### SDL graphics demos

If the lesson has a SDL `SCREEN` demo, wrap it with scene-flips:

```
SCENE: Desktop
CMD: RUN
WINMOVE: <window-title-from-SCREEN-call>
WAIT: 4.5
SCENE: IDE
FOCUS: jdBasic
```

And the demo itself needs a render-loop, NOT a flat `SLEEP`:

```
TYPE: SCREENFLIP
TYPE: DIM ev
TYPE: FOR f = 0 TO 220
TYPE:     ev = GFX.POLLEVENT()
TYPE:     SLEEP 16
TYPE: NEXT f
TYPE: GFX.CLOSE
```

A flat SLEEP blocks the SDL message pump and Windows refuses to
re-position the window, so WINMOVE silently fails.

## Step 3 - Draft `scripts/<base>.md` (the YouTube description sidecar)

Required `title:`, optional `hook:` + `tags:`. Body becomes the
YouTube description (rendered as plain text - Markdown is allowed
but not interpreted). Standard skeleton:

```markdown
---
title: Train jdBasic - Lesson NN - Topic
hook: Catchy one-liner overlaid on the thumbnail
tags: keyword1, keyword2, keyword3, lesson-NN
---

Lesson NN of Train jdBasic - one-paragraph framing.

## What you'll learn

- Bullet for each concept
- Keep API names in backticks
- AVOID `<` or `>` in body or code blocks - YouTube rejects with 400

## Code from the lesson

(a fenced ```basic code block - the killer demo from the script)

## Next up

Lesson NN+1 - teaser.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=<your-playlist-id>
```

If the script's code has `<` or `>` (lambda arrows, comparisons,
pipe operator), use a different code snippet in the `.md` or
rephrase. The script_05 and script_12 .md files in the repo show
how this was handled for FUNC and SELECT examples.

## Step 4 - Atomi opens OBS + the jdBasic IDE

Hand off to Atomi for the physical prep:

1. jdBasic IDE open (the window title must be exactly `jdBasic` -
   the director uses Win32 FindWindow for it).
2. OBS Studio running, **IDE** scene selected as default.
3. OBS recording-path set to `<repo>/jdb/tv/raw/`.
4. (If the lesson has graphics demos) a **Desktop** scene
   configured - Display Capture cropped to `0,0,1920×1080`.

The README's "OBS scene setup" section has the per-scene config.

## Step 5 - Drive the recording

```bash
cd D:/usr/dev/cc/jdb/tv
../../build/jdBasic.exe director.jdb <base>.txt
```

(`<base>.txt` is the bare script basename - director resolves it
inside `scripts/`.)

What happens, in order:
1. Pre-renders every `SAY:` as a WAV under `voice/<base>_NNN.wav`
2. Pre-renders every `TITLE:` as a PNG under `cards/<base>_title_NNN.png`
3. Reposition the IDE to `0,0,1920×1080`
4. OBS start-record
5. Walk the script: type into IDE, play voice clips, switch scenes
6. OBS stop-record
7. Write `raw/<base>.manifest.txt`
8. FFmpeg mux → `final/<base>.mp4`

Pre-render alone takes ~30s for a 10-SAY lesson. The recording itself
plays in real-time + voice durations. Budget ~10 min total for a 6 min
lesson.

## Step 6 - Verify outputs

```bash
ls -la jdb/tv/final/<base>.mp4 jdb/tv/raw/<base>.manifest.txt
```

Both must exist. If the MP4 is missing or the manifest has 0 VOICE
rows, the take crashed - check the director's stdout for the
failing step.

Then hand off to `/tvupload <base>` when Atomi is happy with the take.

## Common gotchas

- **AltGr characters** in TYPE lines (`{`, `}`, `[`, `]`, `@`, `\`, `|`)
  work via REMOTE.TypeChar's EXTENDEDKEY flag. If a new layout-specific
  char doesn't type correctly, that's the first place to look.
- **Empty `TYPE:` line** types just Enter - useful for visual blank
  lines in the editor. Doesn't change the program semantics.
- **Module file names lowercase**: `IMPORT MATHX` loads `mathx.jdb`,
  not `MathX.jdb`. Save with `SAVE "mathx"` for round-trip.
- **`OPTION "NOAUTOIDENT"`** is flipped on by the Intro workspace,
  so TYPE lines preserve leading whitespace. Don't `OPTION "AUTOIDENT"`
  again mid-script unless you want re-indenting.
- **Re-shoots**: delete the old `final/<base>.mp4`, `raw/<base>.manifest.txt`
  and `voice/<base>_*.wav` first. The director appends to OBS's output
  folder so stale takes can confuse compose.jdb.
