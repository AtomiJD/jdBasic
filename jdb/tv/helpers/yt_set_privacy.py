#!/usr/bin/env python3
"""
yt_set_privacy.py - flip privacyStatus on an uploaded video. Used after
the upload pipeline (which defaults to 'private' per defaults.json) to
move freshly remade lessons to 'public' without disturbing the default.

Usage:
    python yt_set_privacy.py <video_id> {public|unlisted|private}
"""
import sys, pathlib

if len(sys.argv) != 3:
    print("usage: yt_set_privacy.py <video_id> {public|unlisted|private}",
          file=sys.stderr)
    sys.exit(2)

video_id = sys.argv[1]
privacy  = sys.argv[2].lower()
if privacy not in ("public", "unlisted", "private"):
    print(f"ERROR: privacy must be public|unlisted|private (got '{privacy}')",
          file=sys.stderr)
    sys.exit(2)

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

try:
    youtube.videos().update(
        part="status",
        body={"id": video_id, "status": {"privacyStatus": privacy}},
    ).execute()
    print(f"OK - {video_id} -> {privacy}")
except HttpError as e:
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(5)
