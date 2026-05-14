#!/usr/bin/env python3
"""
obs_cmd.py — thin wrapper around obsws-python so jdBasic can drive OBS
via shell calls. Reads connection settings from a tv/.env file (or
environment variables OBS_HOST / OBS_PORT / OBS_PASS as fallback).

Usage:
    python obs_cmd.py start_record
    python obs_cmd.py stop_record
    python obs_cmd.py scene <SceneName>
    python obs_cmd.py status

Exits 0 on success, non-zero on error. Prints any returned info to
stdout so the jdBasic caller can capture it with SHELL.
"""
import os, sys, pathlib

# Load .env file from the parent dir (tv/.env). Plain KEY=VALUE lines, '#' comments.
env_path = pathlib.Path(__file__).resolve().parent.parent / ".env"
if env_path.exists():
    for line in env_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        os.environ.setdefault(k.strip(), v.strip())

host = os.environ.get("OBS_HOST", "localhost")
port = int(os.environ.get("OBS_PORT", "4455"))
password = os.environ.get("OBS_PASS", "")

try:
    import obsws_python as obs
except ImportError:
    print("ERROR: pip install obsws-python", file=sys.stderr)
    sys.exit(2)

if len(sys.argv) < 2:
    print("usage: obs_cmd.py {start_record|stop_record|scene NAME|status}", file=sys.stderr)
    sys.exit(2)

cmd = sys.argv[1]
try:
    client = obs.ReqClient(host=host, port=port, password=password, timeout=5)
except Exception as e:
    print(f"ERROR: connect {host}:{port} — {e}", file=sys.stderr)
    sys.exit(3)

try:
    if cmd == "start_record":
        client.start_record()
        print("recording started")
    elif cmd == "stop_record":
        r = client.stop_record()
        # v5 stop_record returns output path in r.output_path
        path = getattr(r, "output_path", "?")
        print(f"recording stopped: {path}")
    elif cmd == "scene":
        if len(sys.argv) < 3:
            print("usage: obs_cmd.py scene <name>", file=sys.stderr); sys.exit(2)
        client.set_current_program_scene(sys.argv[2])
        print(f"scene -> {sys.argv[2]}")
    elif cmd == "status":
        r = client.get_record_status()
        print(f"active={r.output_active} paused={r.output_paused} timecode={r.output_timecode}")
    else:
        print(f"unknown cmd: {cmd}", file=sys.stderr); sys.exit(2)
except Exception as e:
    print(f"ERROR: {cmd} — {e}", file=sys.stderr)
    sys.exit(4)
