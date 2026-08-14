---
name: tvupload
description: Push a finished jdBasic-TV lesson to YouTube - pre-flight the metaJSON (no `<>` in title/description), invoke upload.jdb, verify marker, confirm playlist append. Triggered when Atomi says "upload lesson X", "publish video", "push to YouTube", or asks for `--all` batch uploads.
---

# Train jdBasic - Lesson Upload

Use this whenever **Atomi** wants to push one (or all) finished
`final/<base>.mp4` to YouTube. Assumes the lesson has already been
shot with `/tvshoot` and the `.md` description sidecar exists.

Working dir: `D:\usr\dev\cc\jdb\tv` (Bash: `/d/usr/dev/cc/jdb/tv`).

## Inputs you accept

| Form | Meaning |
|---|---|
| `tvupload <base>` | Upload one lesson (e.g. `script_15_async`) |
| `tvupload --all` | Walk `final/*.mp4`, upload anything without an `.uploaded.json` marker |
| `tvupload --dry-run <base>` | Build metaJSON, no API calls (sanity preview) |
| `tvupload --render-thumb <base>` | (Re-)render the 1280×720 thumbnail PNG only |

## Step 0 - Pre-flight checks

Before invoking `upload.jdb`, verify the file pre-requisites yourself.
Saves an OAuth round-trip + clear error if anything's missing.

```bash
ls -la final/<base>.mp4 \
       scripts/<base>.md \
       raw/<base>.manifest.txt \
       yt/defaults.json \
       2>&1
```

All four must exist. Common misses:
- `<base>.mp4` missing → `/tvshoot` wasn't run or the take failed
- `<base>.md` missing → the description sidecar wasn't written
- `<base>.manifest.txt` missing → director crashed before writing it
- `yt/defaults.json` missing → setup not done (see `tv/README.md`)

Also check the marker:

```bash
ls final/<base>.uploaded.json 2>&1
```

If it **already exists**, the video was uploaded before. Confirm with
Atomi whether to skip or force re-upload (delete the marker first).

## Step 1 - Dry-run sanity check (recommended for first uploads)

```bash
cd D:/usr/dev/cc/jdb/tv
../../build/jdBasic.exe upload.jdb --dry-run <base>
```

Writes `yt/_tmp_<base>.json` and prints the description preview +
merged tag list. Scan the output for:

- **`00:00 <chapter>`** at the start of the description - confirms
  chapter list was built. If missing, the script.txt has no `CHAPTER:`
  markers (upload will still work, just no chapters).
- **Tag count + spelling** - defaults from `yt/defaults.json` merged
  with `.md` frontmatter `tags:`, deduplicated case-insensitively.
- **No `<` or `>` warning** - upload.jdb pre-flights this and aborts
  with a clear message. If you see "FATAL: title contains '<' or '>'",
  rephrase the offending field in the `.md` (use "less than" / "greater
  than" / words) and re-run.

## Step 2 - The real upload

```bash
../../build/jdBasic.exe upload.jdb <base>
```

(or `--all` for batch.)

Sequence under the hood:
1. Renders `final/<base>.thumb.png` if missing (Playwright/Chromium,
   ~3s)
2. Builds `yt/_tmp_<base>.json` (cleaned up after success)
3. Shells out to `youtubeuploader.exe` with the metaJSON, thumbnail,
   secrets, and token cache from `tv/.env`
4. Writes `final/<base>.uploaded.json` (YouTube's API response)
5. Calls `helpers/yt_add_to_playlist.py` to append to the series
   playlist (3/5/10/15/20s backoff to wait through propagation 404)

**First upload ever** triggers the youtubeuploader OAuth consent flow
(browser tab on `localhost:8080`). After that, all uploads run
unattended.

## Step 3 - Report

After success, give Atomi:

```
✓ Uploaded - video ID: <id from final/<base>.uploaded.json id field>
✓ Marker: final/<base>.uploaded.json
✓ Playlist: appended (or: failed - add manually in Studio)
```

Read the video ID from the marker:

```bash
python3 -c "import json; print(json.load(open('final/<base>.uploaded.json'))['id'])"
```

Give Atomi the YouTube Studio link if useful:
`https://studio.youtube.com/video/<id>/edit`

## Common failures + fixes

| Symptom | Fix |
|---|---|
| `accessNotConfigured` (HTTP 403) | YouTube Data API v3 not enabled in your GCP project - Console: APIs → Library → enable |
| `access_denied` on consent | Your Google account isn't in the OAuth consent screen's Test users list |
| `localhost refused to connect` after consent | `redirect_uris[0]` in client_secret_*.json missing port :8080 |
| `invalidDescription` (HTTP 400) | Title or description contains `<` or `>`. Rephrase the .md, re-run |
| Thumbnail upload 403 | Channel not phone-verified. Video is up, manually drag the .thumb.png in Studio |
| Marker JSON written but exit code != 0 | Playlist-add race. `upload.jdb`'s `AddToPlaylist` retries with backoff. If that also fails, add manually in Studio |
| `invalid_grant` after a week of inactivity | Refresh tokens expire after 7d in Testing mode. Delete `tv/yt/request.token` and/or `tv/yt/playlist.token`, re-run, re-consent |

## Bookkeeping notes

- **Truth-source for "upload succeeded" is the marker file**, not the
  exit code. `youtubeuploader` returns 1 on any post-upload hiccup
  (e.g. playlist 404), but the video is already up. `upload.jdb`
  already encodes this and continues to the playlist-add step.
- **`_tmp_*.json` cleanup**: happens automatically after success. If
  you find leftover `yt/_tmp_*.json` files, the previous upload
  failed before cleanup - safe to delete manually.
- **Privacy stays `private`** by default. Flip to `public` in
  YouTube Studio when you're ready to publish; that's not part of
  this pipeline (would need a separate `videos.update` API call we
  intentionally don't ship).
- **Re-uploading a video**: delete `final/<base>.uploaded.json`
  AND `final/<base>.thumb.png` first, then re-run. The old YouTube
  video stays up (we don't delete) - manually retire it in Studio
  before publishing the new one, or you'll have duplicates.
