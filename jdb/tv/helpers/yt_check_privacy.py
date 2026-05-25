#!/usr/bin/env python3
"""
yt_check_privacy.py - bulk-check privacyStatus on a list of videoIds.
Used to verify a replace_videos.jdb run actually finished the public-flip
step.

Usage:
    python yt_check_privacy.py <id1> [<id2> ...]
"""
import sys, pathlib

if len(sys.argv) < 2:
    print("usage: yt_check_privacy.py <id1> [<id2> ...]", file=sys.stderr)
    sys.exit(2)

ids = sys.argv[1:]

here = pathlib.Path(__file__).resolve().parent.parent
token_path = here / "yt" / "playlist.token"
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

# videos().list accepts up to 50 ids comma-separated.
resp = youtube.videos().list(
    part="snippet,status",
    id=",".join(ids),
    maxResults=50,
).execute()

found = {}
for item in resp.get("items", []):
    found[item["id"]] = (
        item["status"]["privacyStatus"],
        item["snippet"]["title"],
    )

for vid in ids:
    if vid in found:
        priv, title = found[vid]
        mark = "OK" if priv == "public" else "**"
        print(f"  {mark} {vid}  {priv:<8}  {title}")
    else:
        print(f"  ? {vid}  MISSING (deleted? wrong id?)")
