#!/usr/bin/env python3
"""
yt_sort_playlist.py - re-order a playlist into lesson-number order based
on the videos' titles ("Train jdBasic - Lesson NN - ...").

Called by replace_videos.jdb after a bulk delete-and-reupload run: the
fresh uploads land at the end of the playlist; this pass moves them to
their lesson-number position via playlistItems().update().

Usage:
    python yt_sort_playlist.py <playlist_id> [--dry-run]
"""
import re, sys, pathlib

if len(sys.argv) < 2 or len(sys.argv) > 3:
    print("usage: yt_sort_playlist.py <playlist_id> [--dry-run]", file=sys.stderr)
    sys.exit(2)
playlist_id = sys.argv[1]
dry_run     = (len(sys.argv) == 3 and sys.argv[2] == "--dry-run")

here = pathlib.Path(__file__).resolve().parent.parent  # tv/
yt_dir = here / "yt"
token_path = yt_dir / "playlist.token"
if not token_path.exists():
    print(f"ERROR: {token_path} not found", file=sys.stderr)
    sys.exit(3)

try:
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials
    from googleapiclient.discovery import build
    from googleapiclient.errors import HttpError
except ImportError:
    print("ERROR: pip install google-auth-oauthlib google-api-python-client",
          file=sys.stderr)
    sys.exit(4)

SCOPES = ["https://www.googleapis.com/auth/youtube"]
creds = Credentials.from_authorized_user_file(str(token_path), SCOPES)
if not creds.valid and creds.expired and creds.refresh_token:
    creds.refresh(Request())
    token_path.write_text(creds.to_json(), encoding="utf-8")

youtube = build("youtube", "v3", credentials=creds)

# Fetch all playlist items (paginated).
items = []
page_token = None
while True:
    resp = youtube.playlistItems().list(
        part="snippet",
        playlistId=playlist_id,
        maxResults=50,
        pageToken=page_token,
    ).execute()
    items.extend(resp.get("items", []))
    page_token = resp.get("nextPageToken")
    if not page_token:
        break

# Extract lesson number from title; titles without "Lesson NN" sort last.
LESSON_RE = re.compile(r"Lesson\s+(\d+)", re.IGNORECASE)
def lesson_num(item):
    title = item["snippet"]["title"]
    m = LESSON_RE.search(title)
    return int(m.group(1)) if m else 9999

current = [(i["snippet"]["position"],
            i["snippet"]["title"],
            i["snippet"]["resourceId"]["videoId"],
            i["id"]) for i in items]
desired = sorted(items, key=lesson_num)

print(f"playlist has {len(items)} items")
moves = 0
for new_pos, item in enumerate(desired):
    cur_pos = item["snippet"]["position"]
    title   = item["snippet"]["title"]
    if cur_pos == new_pos:
        continue
    moves += 1
    print(f"  move '{title[:60]}'  {cur_pos} -> {new_pos}")
    if dry_run:
        continue
    youtube.playlistItems().update(
        part="snippet",
        body={
            "id": item["id"],
            "snippet": {
                "playlistId": playlist_id,
                "resourceId": item["snippet"]["resourceId"],
                "position": new_pos,
            },
        },
    ).execute()

if moves == 0:
    print("OK - playlist already in order")
elif dry_run:
    print(f"[dry-run] {moves} moves planned")
else:
    print(f"OK - {moves} items reordered")
