# Voice assistant - status & session notes (2026-07-16)

A proactive phone assistant in jdBasic: it calls Atomi via Twilio and holds a
spoken German dialog (Twilio STT/TTS) driven by a local LLM. Epic **jdTrakr #96**
(phases #97-#102). This file is the running status so the work can be picked up
cleanly.

## TL;DR

- **The dialog code works.** Greeting, speech recognition, the LLM brain, the
  conversation memory and the nicer Polly voice were all verified end-to-end
  (both locally and, on several calls, over the real phone - e.g. an 81-second,
  6-turn conversation with working name recall).
- **Open problem: Twilio's webhook fetch to the ngrok URL intermittently fails**
  (`status 0` in the ngrok inspector, Twilio alert 11200 / "HTTP 502 or 503",
  the phone stays silent then says "application error"). It is intermittent -
  some calls run the full dialog, most fail at the first `/voice` fetch.
- **Atomi's note (important):** this same ngrok account ran a Twilio webhook
  24/7 reliably in the past. So the instability is very likely something in the
  *current setup*, not ngrok itself. See "Prime suspects" below.

## Architecture

```
voiceagent_dial.jdb  --REST call-->  Twilio (region US1)
                                        |  fetches TwiML from the Url
                                        v
                              ngrok tunnel (free)  --->  cortex :5005
                                                          |
                                              voiceagent_server.jdb (HTTP.SERVER)
                                                /voice  -> greeting + <Gather speech>
                                                /gather -> SpeechResult -> brain -> reply
                                                          |                    |
                                             SQLite conversations.db   llama-server :8082
                                                (per-CallSid history)   (Qwen2.5-32B-Instruct)
```

Turn-based (Path A): Twilio does the STT/TTS, jdBasic is the brain/glue. No
WebSocket. Media Streams + own whisper/Piper is the deferred Path B (#101).

## Where the code is

### Repo (Windows dev box, canonical) - `D:\usr\dev\cc\jdb\demos\ai\voiceagent\`
- **`voiceagent_server.jdb`** - the dialog server. `/voice`, `/gather`, `/health`.
  Prompts come from `prompts.json` and are copied into plain string globals at
  boot; the SQLite memory + LLM call live in `/gather`. **Look here first.**
- **`voiceagent_dial.jdb`** - places an interactive call (Url points at the
  tunnel `/voice`; announcement written to `/tmp/va_pending.txt`).
- **`voiceagent_call.jdb`** - one-shot inline-TwiML announcement (no tunnel).
  This inline path was confirmed *audible* - proves Twilio TTS + the number are
  fine; only the webhook fetch is flaky.
- **`prompts.json`** - all spoken text + system prompt + `voice` (Polly) per
  locale (`de` / `en`), selected by the `VA_LOCALE` env var.
- **`README.md`** - setup. **`repro_map.jdb`** - a minimal HTTP.SERVER used
  during debugging.

### The runtime HTTP.SERVER change - `D:\usr\dev\cc\src\http.cpp`
- `g_server->set_keep_alive_max_count(1)` (~line 587) - added this session to
  close each connection after one request. **This is a prime suspect to review**
  (see below). Committed + pushed.

### cortex (`atomi@192.168.0.113`) - runtime
- `~/voiceagent/` - deployed copies + `server.log`, `ngrok.log`, `conversations.db`.
- `~/.voiceagent.env` - Twilio creds (chmod 600, gitignored). **Rotate the
  Twilio auth token** - it was pasted in chat. The ngrok authtoken also appeared
  in a log line this session.
- `~/ft/start_voicebrain.sh` - serves Qwen2.5-32B-Instruct-Q4_K_M on :8082
  (distrobox llama-vulkan-radv, -ngl 99, ~1.5-2.5s per short reply).
- `screen` sessions: **voiceagent** (server), **ngrok** (tunnel), **voicebrain**
  (LLM). `screen -r <name>` to inspect. Note: never `pkill -f voiceagent_server`
  or `pkill -f "cloudflared tunnel"` - the pattern matches the ssh wrapper's own
  command line and kills the session (exit 255); use `screen -S <name> -X quit`.

### Models on cortex (`~/ft`)
- `Qwen2.5-32B-Instruct-Q4_K_M.gguf` (19 GB) - current brain.
- `Qwen2.5-7B-Instruct-Q6_K.gguf` (5.8 GB) - faster fallback (was the brain when
  the first fully-working calls happened yesterday).
- plus the jdBasic coder models (14b/32b/32b-v2).

## What is confirmed working

- Outbound call + inline announcement (`voiceagent_call.jdb`) - **audible**.
- The TwiML dialog server: `/voice` greeting + `<Gather>`, `/gather` parses
  `request{"PARAMS"}{"SpeechResult"}`, replies, loops; `<Hangup/>` on bye words.
- LLM brain (Qwen 32B) with **per-call memory** in SQLite (verified: "mein Name
  ist Atomi" -> later "Wie heisse ich?" -> "Du heisst Atomi").
- **Polly Neural voice** (`<Say voice="Polly.Vicki-Neural">`), configurable in
  prompts.json.
- Timestamp + brain-time logging in `server.log`.
- Every one of these was seen returning HTTP 200 from BOTH localhost and an
  external Windows curl through the tunnel.

## The open problem, and what was ruled out

Symptom: Twilio's `/voice` fetch returns `status 0` (server received the request
- it's in `server.log` - but no response reached Twilio) or `503`. Phone: silent
then "application error".

Ruled out by direct evidence:
- **Not the server code / not the handler logic** - localhost and external
  Windows curls to `/voice` return 200 in <0.2s, reliably, at the same moments
  Twilio fails.
- **Not the LLM / not SQLite** - `/voice` doesn't touch them, and it's `/voice`
  that fails.
- **Not the Twilio number / not TTS / not geo** - inline TwiML calls are
  audible; account is Full/active, $15 balance.
- **Not cloudflared vs ngrok** - both tunnels showed the same failure, so it is
  not one tunnel product's bug.
- **The "map access breaks /voice" theory was a red herring.** Reading a global
  map (`gP{...}`) or `request{"PARAMS"}` inside `HandleVoice` *seemed* to correlate
  with failures, but a minimal repro reads a global map fine under curl (incl.
  `Expect: 100-continue`), and a later `status 0` happened with a string-only
  `HandleVoice`. It was tunnel intermittency mis-attributed. (`HandleVoice` is
  nonetheless kept string-only for now.)

Observed but inconsistent:
- ngrok `heartbeat timeout, terminating session` / `read EOF from remote peer`
  drops, sometimes coinciding exactly with the failing call, sometimes not.
- cloudflared `no recent network activity` QUIC drops (switching cloudflared to
  `--protocol http2` stabilised the probe test but calls still failed).
- cortex network to 8.8.8.8 is clean (0% loss); ngrok EU edge 31 ms, US edge
  140 ms; ngrok v3 will NOT let you pin the region (`region` field rejected).
- Raising ngrok `heartbeat_tolerance` to 30s stopped the logged drop but a
  `/voice` still returned `status 0`.

## Prime suspects to review next (given "ngrok ran 24/7 before")

1. **`set_keep_alive_max_count(1)` in `src/http.cpp`.** This changed HTTP.SERVER
   to close every connection after one request (it was added to fix an ngrok
   3004 that turned out to be tangled with the region/latency issue). This is the
   biggest behavioural change from "before" and could interact badly with how
   ngrok reuses the upstream connection. **Try reverting it** (rebuild jdbasic on
   cortex without it) and re-test - this is the first experiment for next time.
2. The pre-call **warmup curl** in `voiceagent_dial.jdb`'s test flow could seed a
   connection in ngrok that the server then closes (with #1) - drop the warmup.
3. Whether an older/simpler working config (7B brain, the exact server that ran
   the 81s call yesterday) still works today - i.e. is it time-of-day / ISP /
   ngrok-edge dependent.
4. cpp-httplib `Expect: 100-continue` handling (Twilio sends it; curl mostly
   doesn't) combined with #1.

## How to run (cortex)

```
# brain
screen -dmS voicebrain ~/ft/start_voicebrain.sh              # Qwen 32B on :8082
# server
cd ~/voiceagent && screen -dmS voiceagent ~/dev/cc/build/jdbasic voiceagent_server.jdb
# tunnel
screen -dmS ngrok ~/bin/ngrok http 5005 --log stdout > ~/voiceagent/ngrok.log 2>&1
# place a call (URL auto-detected from the ngrok API, or pass it explicitly)
~/dev/cc/build/jdbasic voiceagent_dial.jdb "Ansagetext"
```

Diagnostics that proved most useful: ngrok inspector
`http://127.0.0.1:4040/api/requests/http` (per-request status + base64 raw
req/resp), Twilio Monitor Alerts `https://monitor.twilio.com/v1/Alerts`, and the
`server.log` timestamps.

## Commits this session (all on `main`, pushed)

- `set_keep_alive_max_count(1)` + the voiceagent demo (server/dial/call/README).
- LLM brain wiring; prompts.json + SQLite memory; Polly voice + timing logs;
  the string-globals refactor.

## Next steps

1. Revert suspect #1 (keep-alive) and re-test - most likely lead.
2. Once delivery is reliable: per-call memory is done; add P3 trigger layer
   (email/offer ingest -> importance filter -> call), quiet hours, and consider
   Path B (Media Streams) for lower latency + a non-mechanical voice pipeline.
