#!/usr/bin/env python3
"""
yt_reauth.py - force a fresh OAuth consent and overwrite tv/yt/playlist.token.

Use when the Python-side helpers (yt_add_to_playlist, yt_set_privacy,
yt_sort_playlist, ...) start failing with:

    google.auth.exceptions.RefreshError:
        ('invalid_grant: Token has been expired or revoked.', ...)

That happens about every 7 days while the OAuth app is in "Testing"
status in Google Cloud Console. This script deletes the cached token
and runs the local-server consent flow to mint a fresh one.

Usage:
    python yt_reauth.py
"""
import sys, pathlib

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
except ImportError:
    print("ERROR: pip install google-auth-oauthlib google-api-python-client",
          file=sys.stderr)
    sys.exit(4)

SCOPES = ["https://www.googleapis.com/auth/youtube"]

if token_path.exists():
    backup = token_path.with_suffix(".token.expired")
    token_path.replace(backup)
    print(f"  moved old token: {token_path.name} -> {backup.name}")

print("  opening browser for consent...")
flow = InstalledAppFlow.from_client_secrets_file(str(client_secrets), SCOPES)
creds = flow.run_local_server(port=0)
token_path.write_text(creds.to_json(), encoding="utf-8")
print(f"  OK - new token written: {token_path}")
