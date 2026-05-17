#!/usr/bin/env python3
"""
yt_add_to_playlist.py — append an already-uploaded video to a YouTube
playlist via the Data API v3.  Reuses tv/yt/playlist.token (broader
youtube scope than youtubeuploader's youtube.upload).

Exists because youtubeuploader.exe does upload + playlist-add atomically
and the playlist-add hits a propagation 404 (video not visible yet) for
fresh uploads.  Doing it as a separate post-upload call with retry sleeps
through that window.

Usage:
    python yt_add_to_playlist.py <video_id> <playlist_id>
"""
import sys, pathlib, glob, time

if len(sys.argv) != 3:
    print("usage: yt_add_to_playlist.py <video_id> <playlist_id>", file=sys.stderr)
    sys.exit(2)

video_id    = sys.argv[1]
playlist_id = sys.argv[2]

here = pathlib.Path(__file__).resolve().parent.parent  # tv/
yt_dir = here / "yt"
matches = list(yt_dir.glob("client_secret_*.json"))
if not matches:
    print(f"ERROR: no client_secret_*.json in {yt_dir}", file=sys.stderr)
    sys.exit(3)
client_secrets = matches[0]
token_path = yt_dir / "playlist.token"

if not token_path.exists():
    print(f"ERROR: {token_path} not found - run create_playlist.jdb first", file=sys.stderr)
    sys.exit(3)

try:
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials
    from googleapiclient.discovery import build
    from googleapiclient.errors import HttpError
except ImportError:
    print("ERROR: pip install google-auth-oauthlib google-api-python-client", file=sys.stderr)
    sys.exit(4)

SCOPES = ["https://www.googleapis.com/auth/youtube"]
creds = Credentials.from_authorized_user_file(str(token_path), SCOPES)
if not creds.valid and creds.expired and creds.refresh_token:
    creds.refresh(Request())
    token_path.write_text(creds.to_json(), encoding="utf-8")

youtube = build("youtube", "v3", credentials=creds)

# Fresh uploads can take 5-30s before the playlistItems endpoint sees the
# video — retry with backoff before giving up.
DELAYS = [3, 5, 10, 15, 20]
last_err = None
for i, wait in enumerate(DELAYS):
    if i > 0:
        print(f"  retrying in {wait}s (attempt {i+1}/{len(DELAYS)})", file=sys.stderr)
    time.sleep(wait)
    try:
        req = youtube.playlistItems().insert(
            part="snippet",
            body={
                "snippet": {
                    "playlistId": playlist_id,
                    "resourceId": {
                        "kind":    "youtube#video",
                        "videoId": video_id,
                    },
                },
            },
        )
        resp = req.execute()
        print(f"OK - added {video_id} to {playlist_id} (item id {resp.get('id')})")
        sys.exit(0)
    except HttpError as e:
        last_err = e
        # Only retry on 404 videoNotFound — anything else is structural.
        if e.resp.status != 404:
            print(f"ERROR (not retrying): {e}", file=sys.stderr)
            sys.exit(5)

print(f"ERROR: still 404 after {sum(DELAYS)}s of retries: {last_err}", file=sys.stderr)
sys.exit(5)
