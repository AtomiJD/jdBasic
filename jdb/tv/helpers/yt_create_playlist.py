#!/usr/bin/env python3
"""
yt_create_playlist.py — create the "Train jdBasic" YouTube playlist
via the YouTube Data API v3, using the same client_secret_*.json that
youtubeuploader.exe consumes for upload.  Stores its own token cache
(tv/yt/playlist.token) because the Go-side and Python-side OAuth
libraries serialise tokens incompatibly.

On success prints exactly one line to stdout: the new playlist ID
(prefixed PL...).  The jdBasic wrapper parses that and writes it
back into defaults.json.

Idempotency is the caller's job — we always create a new playlist;
the wrapper checks defaults.json first and skips if already set.

Usage:
    python yt_create_playlist.py "<title>" "<description>" {public|unlisted|private}
"""
import sys, pathlib, glob, json

if len(sys.argv) < 4:
    print("usage: yt_create_playlist.py <title> <description> <privacy>", file=sys.stderr)
    sys.exit(2)

title       = sys.argv[1]
description = sys.argv[2]
privacy     = sys.argv[3].lower()
if privacy not in ("public", "unlisted", "private"):
    print(f"invalid privacy: {privacy!r} (expected public/unlisted/private)", file=sys.stderr)
    sys.exit(2)

here = pathlib.Path(__file__).resolve().parent.parent  # tv/
yt_dir = here / "yt"
matches = list(yt_dir.glob("client_secret_*.json"))
if not matches:
    print(f"ERROR: no client_secret_*.json in {yt_dir}", file=sys.stderr)
    sys.exit(3)
client_secrets = matches[0]
token_path = yt_dir / "playlist.token"

try:
    from google_auth_oauthlib.flow import InstalledAppFlow
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials
    from googleapiclient.discovery import build
except ImportError:
    print("ERROR: pip install google-auth-oauthlib google-api-python-client", file=sys.stderr)
    sys.exit(4)

SCOPES = ["https://www.googleapis.com/auth/youtube"]

creds = None
if token_path.exists():
    try:
        creds = Credentials.from_authorized_user_file(str(token_path), SCOPES)
    except Exception as e:
        print(f"  (existing token unreadable, re-authing: {e})", file=sys.stderr)
        creds = None

if not creds or not creds.valid:
    if creds and creds.expired and creds.refresh_token:
        creds.refresh(Request())
    else:
        flow = InstalledAppFlow.from_client_secrets_file(str(client_secrets), SCOPES)
        creds = flow.run_local_server(port=8081)  # 8080 reserved by youtubeuploader
    token_path.write_text(creds.to_json(), encoding="utf-8")

youtube = build("youtube", "v3", credentials=creds)
req = youtube.playlists().insert(
    part="snippet,status",
    body={
        "snippet": {"title": title, "description": description},
        "status":  {"privacyStatus": privacy},
    },
)
resp = req.execute()
pid = resp["id"]
print(pid)
