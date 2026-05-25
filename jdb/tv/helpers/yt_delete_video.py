#!/usr/bin/env python3
"""
yt_delete_video.py - delete an uploaded video from YouTube via the
Data API v3. Uses tv/yt/playlist.token (broader 'youtube' scope which
covers DELETE; youtubeuploader's youtube.upload scope does not).

Usage:
    python yt_delete_video.py <video_id>

Exits non-zero on any error. Deletion is irreversible at the YouTube
side; the caller (replace_videos.jdb) prints the videoId + title FIRST
and asks for go/no-go before invoking this.
"""
import sys, pathlib

if len(sys.argv) != 2:
    print("usage: yt_delete_video.py <video_id>", file=sys.stderr)
    sys.exit(2)

video_id = sys.argv[1]

here = pathlib.Path(__file__).resolve().parent.parent  # tv/
yt_dir = here / "yt"
matches = list(yt_dir.glob("client_secret_*.json"))
if not matches:
    print(f"ERROR: no client_secret_*.json in {yt_dir}", file=sys.stderr)
    sys.exit(3)
token_path = yt_dir / "playlist.token"
if not token_path.exists():
    print(f"ERROR: {token_path} not found - run create_playlist.jdb first",
          file=sys.stderr)
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

try:
    youtube.videos().delete(id=video_id).execute()
    print(f"OK - deleted {video_id}")
except HttpError as e:
    # 404 = already gone (idempotent), treat as success.
    if e.resp.status == 404:
        print(f"OK - {video_id} already absent")
        sys.exit(0)
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(5)
