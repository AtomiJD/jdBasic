#!/usr/bin/env python3
"""
yt_find_recent.py - search the authenticated channel for the most recent
uploads matching a title substring. Lets the replace_videos.jdb rescue
flow find a video that uploaded successfully but whose local marker was
never written.

Usage:
    python yt_find_recent.py "<title substring>" [count]
"""
import sys, pathlib

if len(sys.argv) < 2 or len(sys.argv) > 3:
    print("usage: yt_find_recent.py <title substring> [count]", file=sys.stderr)
    sys.exit(2)

needle = sys.argv[1].lower()
count  = int(sys.argv[2]) if len(sys.argv) == 3 else 25

here = pathlib.Path(__file__).resolve().parent.parent
yt_dir = here / "yt"
token_path = yt_dir / "playlist.token"
if not token_path.exists():
    print(f"ERROR: {token_path} not found", file=sys.stderr)
    sys.exit(3)

try:
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials
    from googleapiclient.discovery import build
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

# Find the channel's 'uploads' playlist (always one per channel).
chan = youtube.channels().list(part="contentDetails", mine=True).execute()
uploads_pid = chan["items"][0]["contentDetails"]["relatedPlaylists"]["uploads"]

resp = youtube.playlistItems().list(
    part="snippet,contentDetails",
    playlistId=uploads_pid,
    maxResults=count,
).execute()

hits = 0
for it in resp.get("items", []):
    title = it["snippet"]["title"]
    if needle in title.lower():
        vid = it["contentDetails"]["videoId"]
        published = it["contentDetails"].get("videoPublishedAt", "?")
        print(f"  {vid}  {published}  {title}")
        hits += 1

if hits == 0:
    print(f"(no upload matches '{needle}' in last {count})")
    sys.exit(1)
