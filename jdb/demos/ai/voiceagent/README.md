# Voice agent (Twilio + jdBasic)

A proactive phone assistant written in jdBasic. It places outbound Twilio calls
and serves a turn-based spoken dialog (Twilio TTS/STT), so the phone itself is
the interface - no browser needed.

## Pieces

- **`voiceagent_call.jdb`** - one-shot announcement. Places a call with inline
  TwiML (`<Say>`); no public tunnel needed. Good for a pure "call me and read
  this out" notification.
- **`voiceagent_server.jdb`** - the dialog server (`HTTP.SERVER` on `:5005`).
  `/voice` returns a greeting plus a `<Gather input="speech">`; `/gather` reads
  the recognized speech from `request{"PARAMS"}{"SpeechResult"}`, asks the LLM
  (`brainReply$`) for a short reply with the call's history as context, and
  loops.
- **`prompts.json`** - all on-screen text (system prompt, greeting, prompts,
  fallbacks, hang-up words), one section per locale keyed by `twilio_lang`.
  Pick the locale with the `VA_LOCALE` env var (default `de`).
- **`conversations.db`** - SQLite, written at runtime. One row per turn
  (`call_sid, seq, role, content, ts`); `brainReply$` loads the last turns of
  the current `CallSid` so the model has real conversation context, and the
  transcript survives a restart.
- **`voiceagent_dial.jdb`** - places an *interactive* call. It auto-detects the
  public tunnel URL from the local ngrok API, writes the announcement to
  `/tmp/va_pending.txt`, and points Twilio's `Url` at `<tunnel>/voice`.

## Setup

1. Credentials in `~/.voiceagent.env` (chmod 600, never commit):

   ```
   TWILIO_SID=AC...
   TWILIO_TOKEN=...
   TWILIO_FROM=+49...
   CALL_TO=+49...
   ```

2. Start the dialog server:

   ```
   jdbasic voiceagent_server.jdb
   ```

3. Expose it. Twilio is in region `us1`, so pin the tunnel to `us` to avoid
   cross-region latency:

   ```
   ngrok http 5005 --region us
   ```

4. Place a call:

   ```
   jdbasic voiceagent_dial.jdb "Neue Mail von der Bank."
   ```

## Note on reverse proxies

`HTTP.SERVER` behind a connection-pooling proxy (ngrok, cloudflared, a real LB)
needs keep-alive off, or the proxy reuses a socket the server has already closed
and the client sees an incomplete response (ngrok error 3004, Twilio alert
11200, or silent TwiML). The runtime now closes each connection after one
request, which fixes this for every reverse-proxy deployment.
