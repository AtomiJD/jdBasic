# jdBasic Node Bus: plan

jdBasic programs that know each other: on one machine and across the farm,
they find each other by name, send each other values, publish streams that
others subscribe to, call each other, and agree on one clock. Everything that
arrives surfaces as an ordinary **channel handle**, so `CHAN.RECV`,
`CHAN.TRY_RECV` and `CHAN.SELECT` work across processes the way they already
work across ASYNC FUNCs.

Working name: **Node Bus**. Namespaces: `NODE.*` (identity, peers, messages,
calls, clock) and `TOPIC.*` (publish/subscribe).

First customer: `songgen --live` plays and mixes in one window while
`stage.jdb` draws the neon show in a second process (on the same machine or on
another farm machine at a projector), frame-synchronous with what is heard.

---

## 1. Use cases

| # | Case | Shape | Volume |
|---|------|-------|--------|
| U1 | songgen desk + stage show | 1 publisher, 1..n viewers, live frames | ~1 MB/s, 60 Hz, latest-wins |
| U2 | stage on the projector laptop / mac | same as U1 over LAN | same, + clock offset |
| U3 | render farm jobs (tv cards, songgen renders, LoRA evals) | work queue, many workers | small messages, lossless |
| U4 | LLM service on cortex/render serving other programs | request/reply | KB per call, seconds latency |
| U5 | dashboards (F1, WM, jdTrakr widgets) fed by one collector | 1 publisher, retained state | small, low rate |
| U6 | Claude / MCP watching a running program | observer subscribes to `*` | whatever is published |
| U7 | game/tool split: game process + editor/inspector process | both directions, state + commands | medium |
| U8 | big numeric arrays between processes (APL pipelines, audio buffers, images) | bulk | 10..1000 MB/s |

U1, U3, U4 and U8 set the requirements: latest-wins frames, lossless queues,
calls with timeouts, and a fast path for large numeric arrays.

## 2. Principles

1. **Channels are the one abstraction for receiving.** Every inbox,
   subscription and reply stream is a channel handle. No second waiting model,
   no callbacks on foreign threads.
2. **Brokerless.** No daemon to start, nothing to install. The first program
   that joins is a full node; peers find each other through a directory
   (local) and beacons (LAN).
3. **One API, transport chosen underneath.** Same machine -> shared memory /
   local sockets; other machine -> TCP. The program never names a transport.
4. **Both backends.** Natives live in the VM (`src/vm_builtins_node.cpp`) and
   reach `-c` programs through `jdbrt.dll`, like `CHAN.*` today. Every
   map-returning native gets its `builtin_sigs.h` entry.
5. **No new value type.** Handles are integers, messages are ordinary values.
6. **Local is safe by default.** Nothing listens on the LAN until a program
   asks for it, and the LAN needs a shared key.
7. **Measure first.** Shared memory is built only if the P0 spike shows local
   sockets fall short for U8.

## 3. Concepts

- **Node**: a named participant, normally one per program. Address
  `name` (this machine) or `name@host` (host = hostname or IP).
  Internally a 64-bit random id + name + host + pid.
- **Inbox**: each node's receiving channel for direct messages and calls.
- **Topic**: a named stream, `"song/clock"`, `"farm/jobs"`. Hierarchical
  with `/`; subscriptions take patterns (`"song/*"`, `"*"`).
- **Publisher handle**: a channel handle; `CHAN.SEND pub, v` publishes.
- **Subscription**: a channel handle with a **policy**:
  - `"queue:N"` lossless, at most N buffered, flow control back to the
    publisher (credits).
  - `"latest"` holds only the newest value; every read gets the most recent
    frame. The policy for scopes, meters, clocks.
  - `"ring:N"` keeps the newest N, drops the oldest.
- **Retained value**: a topic can keep its last value; a subscriber that joins
  later gets it first (song structure, settings, dashboard state).
- **Envelope**: optional delivery form `{"DATA","FROM","TOPIC","SEQ","T"}`
  instead of the bare value; `T` is the send time converted to the receiver's
  clock.
- **Call**: request/reply on top of the inbox, with timeout.
- **Node time**: one nanosecond timebase that all nodes share (exact on one
  machine, estimated across machines).

## 4. API

### 4.1 Identity and peers

| Call | Returns | Notes |
|------|---------|-------|
| `NODE.JOIN(name$, [scope$="local"])` | node handle | `scope$` `"local"` or `"lan"`. Error when the name is taken on this machine. The first JOIN becomes the default node for the calls below. |
| `NODE.OPEN(name$, [scope$])` | node handle | A further node in the same process (tests, role split). |
| `NODE.LEAVE [node]` | - | Sends BYE, removes the directory entry. Also runs at program exit. |
| `NODE.NAME$([node])` | string | Full address `name@host`. |
| `NODE.PEERS([pattern$="*"])` | array of maps | `{"NAME","HOST","PID","ADDR","SCOPE","TOPICS","SINCE"}` for every known node. |
| `NODE.FIND(addr$, [timeout_ms])` | bool | Waits until the node is known (startup ordering). |
| `NODE.EVENTS()` | channel | `{"EVENT":"JOIN"/"LEAVE","ADDR"}` as peers come and go. Same as subscribing `$sys/peers`. |

### 4.2 Direct messages and calls

| Call | Returns | Notes |
|------|---------|-------|
| `NODE.INBOX([node])` | channel | Direct messages and incoming calls. |
| `NODE.SEND(addr$, value)` | bool | Into the peer's inbox. FALSE when unknown/unreachable. Arrives as the bare value. |
| `NODE.CALL(addr$, method$, arg, [timeout_ms=5000])` | value | Blocks for the reply; on timeout the CHAN timeout marker (`CHAN.IS_TIMEOUT`). Remote error -> raised locally with the remote message. |
| `NODE.CALL_ASYNC(addr$, method$, arg)` | channel | The reply arrives on the returned channel (one value, then closed). Fits `CHAN.SELECT`. |
| `NODE.IS_CALL(msg)` | bool | The inbox value is a call: a map `{"METHOD","ARG","FROM","__call__"}`. |
| `NODE.REPLY msg, value` | - | Answers a call. `NODE.FAIL msg, text$` answers with an error. |
| `NODE.SERVE method$, func$` | - | Registers a handler; `NODE.POLL([timeout_ms])` runs pending calls for registered methods on the calling thread and returns how many ran. |

Calls run on the receiver's thread, never on the I/O thread: either the program
reads its inbox, or it calls `NODE.POLL` in its frame loop. Handlers see the
program's globals, which ASYNC FUNCs cannot.

### 4.3 Topics

| Call | Returns | Notes |
|------|---------|-------|
| `TOPIC.OPEN(name$, [mode$="drop"])` | publisher channel | `"drop"`: SEND never blocks, a full lossless subscriber stalls only itself (values for it are dropped and counted). `"block"`: SEND waits for the slowest `queue` subscriber (work queues, U3). |
| `TOPIC.SUBSCRIBE(pattern$, [policy$="queue:256"], [envelope=FALSE])` | channel | Subscribes on every current and future publisher whose topic matches. |
| `TOPIC.UNSUBSCRIBE ch` | - | Also `CHAN.CLOSE ch`. |
| `TOPIC.RETAIN pub, flag` | - | Keep the last value for late subscribers. |
| `TOPIC.LIST([pattern$])` | array of maps | `{"TOPIC","ADDR","SUBSCRIBERS","RATE","BYTES"}` across known nodes. |
| `TOPIC.STATS(ch)` | map | `{"SENT","RECEIVED","DROPPED","BYTES","LAST_T"}` for one handle. |
| `TOPIC.SHARE(name$)` | channel | Work-queue mode: every value goes to exactly **one** of the subscribers that share the name (round robin by free credit). U3. |

### 4.4 Clock

| Call | Returns | Notes |
|------|---------|-------|
| `NODE.TIME()` | i64 ns | The shared timebase: `steady_clock` of this machine. |
| `NODE.OFFSET(addr$)` | map | `{"OFFSET_NS","RTT_NS","ERROR_NS","SAMPLES"}`, the estimate for a remote peer. Zero on this machine. |
| `NODE.TIME_OF(addr$, t_remote)` | i64 ns | A peer's timestamp on this machine's timebase. |

### 4.5 Codec (useful without any network)

| Call | Returns | Notes |
|------|---------|-------|
| `NODE.PACK$(value)` | string (binary) | The JDV encoding of section 5. Also usable for files and caches. |
| `NODE.UNPACK(s$)` | value | Inverse. Throws on malformed input with the byte offset. |

### 4.6 Channels gain a latest mode

`CHAN.OPEN(capacity, [policy$])` with `"block"` (today, default), `"latest"`
(capacity 1, SEND overwrites) and `"ring"` (SEND drops the oldest when full).
Useful on its own between ASYNC FUNCs, and the subscription policies are built
on it.

### 4.7 A small example

```basic
' desk.jdb
NODE.JOIN "desk"
DIM clock = TOPIC.OPEN("song/clock")
DIM inbox = NODE.INBOX()
DO
    CHAN.SEND clock, {"CYCLE": SOUND.CYCLE(), "T": NODE.TIME()}
    DIM msg = CHAN.TRY_RECV(inbox)
    IF NOT CHAN.IS_TIMEOUT(msg) ANDALSO NODE.IS_CALL(msg) THEN
        IF msg{"METHOD"} = "JUMP" THEN REBUILD msg{"ARG"}
        NODE.REPLY msg, TRUE
    ENDIF
    SLEEP 16
LOOP

' show.jdb
NODE.JOIN "show"
DIM clock = TOPIC.SUBSCRIBE("song/clock", "latest")
DO
    DIM c = CHAN.TRY_RECV(clock)
    IF NOT CHAN.IS_TIMEOUT(c) THEN DRAW_AT c{"CYCLE"}
    IF KEY_HIT(32) THEN DIM ok = NODE.CALL("desk", "JUMP", 64)
LOOP
```

## 5. Wire format

### 5.1 JDV1 value encoding

Implemented in P1: `src/jdv.h` is the reference. `"JDV"` + version byte 1,
then one value. Little-endian, varints are LEB128, signed ints zigzag.

| Tag | Value | Payload |
|-----|-------|---------|
| 0x00 | NONE | - |
| 0x01 / 0x02 | FALSE / TRUE | - |
| 0x03 | INT64 | zigzag varint |
| 0x04 | FLOAT64 | 8 bytes |
| 0x05 | FLOAT32 | 4 bytes |
| 0x06 | STRING | varint length + bytes (binary safe) |
| 0x07 | ARRAY, mixed | varint count + values |
| 0x08 | ARRAY of one numeric kind | kind byte (BOOLEAN, BYTE, INT16/32/64, FLOAT16/32/64), varint count, raw elements |
| 0x09 | MAP | varint count, then varint key length + key bytes + value |
| 0x0A | DATE | FLOAT64 epoch seconds |
| 0x0B | TENSOR | varint rank, varint dims, varint count, FLOAT64 data |
| 0x10 | BYTE | 1 byte |
| 0x11 / 0x12 | INT16 / INT32 | zigzag varint |
| 0x13 | FLOAT16 | 2 bytes |

Rules:
- Arrays are nested in jdBasic, so a grid is an ARRAY of packed rows; each
  row is still one `memcpy`.
- A UDT instance is a map with `"__TYPE__"` and travels as a MAP. Function
  references are strings, handles integers; both travel as such. A COM object
  raises.
- Decode limits: depth 64, every count checked against the remaining bytes
  (an element needs at least one byte), varints at most 10 bytes, no trailing
  bytes. Every prefix of a valid message raises.
- Compiled programs hand integers and dates to the VM as FLOAT64 and a bare
  `NONE` argument as 0, so those kinds arrive as FLOAT64 from a `-c` sender.
  A UDT argument is refused at compile time.
- JDV is documented so a Python client (devrig) or an ESP32 can speak it
  later.

### 5.2 Frames

Every link carries frames:

```
offset size field
0      2    magic "JB"
2      1    version (1)
3      1    kind
4      2    flags     (bit0 has timestamp, bit1 envelope wanted, bit2 last chunk)
6      2    reserved
8      4    stream id (topic id, call id, 0 = inbox)
12     4    payload length
16     8    sequence number
24     8    timestamp ns (only with flag bit0)
..          payload (JDV or control map)
```

Kinds: `HELLO`, `WELCOME`, `AUTH`, `BYE`, `ADVERT` (my topics changed),
`SUB`, `UNSUB`, `CREDIT`, `PUB`, `MSG` (inbox), `CALL`, `REPLY`, `FAIL`,
`PING`, `PONG`, `RETAINED`.

Topic names are exchanged once (`ADVERT` maps name -> stream id per link), so a
frame carries 4 bytes instead of the name. Messages over 1 MB are chunked
(flag bit2 marks the last chunk) so a large array does not hold up a clock
frame on the same link.

## 6. Transports

One link per pair of nodes, bidirectional, all topics multiplexed. Two queues
per link inside the node: control/small (clock, calls) first, bulk second.

### 6.1 Same machine

**Step 1 (P2): local stream socket.** AF_UNIX on Linux, macOS and Windows 10
1803+ (`%LOCALAPPDATA%\jdBasic\nodes\<name>.sock`,
`$XDG_RUNTIME_DIR/jdbasic/<name>.sock`, macOS `$TMPDIR`). Fallback TCP on
`127.0.0.1` with an ephemeral port when AF_UNIX is not available. Expected:
single-digit microseconds per small message, hundreds of MB/s to a few GB/s.

**Step 2 (P3, only if P0 says so): shared memory for bulk and latest.**
- Segment per link and direction, created by the sender:
  Windows `CreateFileMappingW("Local\\jdb.<id>.<link>")`, POSIX
  `shm_open("/jdb.<id>.<link>")` + `mmap`.
- Layout: header (magic, version, capacity, `write_idx` and `read_idx` each on
  its own 64-byte cache line), then a byte ring. Single producer, single
  consumer, atomics with acquire/release, frames as in 5.2, a wrap marker at
  the end.
- Wake-up: the existing local socket carries one doorbell byte when the
  consumer had declared itself asleep (flag in the header), so there is no
  platform-specific futex/event code and an idle link costs nothing.
- **Latest slots** for `"latest"` subscriptions: a seqlock triple buffer in
  the segment. The writer fills a free slot, publishes its index with a
  sequence number; the reader takes the newest without a queue. A 12 KB scope
  frame is a `memcpy` on both ends.
- Messages bigger than the ring go into a one-shot segment whose name travels
  over the ring.
- Crash safety: segments are unlinked by the creator after the peer has
  mapped them (POSIX), named mappings on Windows vanish with the last handle.

### 6.2 Other machine

TCP, one connection per pair, `TCP_NODELAY`, keepalive. The node listens on an
ephemeral port only in `"lan"` scope and announces it in the beacon. Large
packed arrays go out with `writev` / `WSASend` straight from the array's
storage, no intermediate copy.

### 6.3 Choosing

Same host (same machine id, see 7.1) -> local transport; else TCP. A peer seen
both ways uses the local one. The program never sees which.

## 7. Discovery

### 7.1 Same machine: the node directory

A directory of small files, one per node: `<runtime dir>/nodes/<name>.json` with
`{"id","name","pid","started","sock","tcp_port","scope","version","machine"}`.
- JOIN creates the file exclusively (that is the name lock); a stale file whose
  pid is gone (or whose socket refuses) is taken over.
- `machine` is a stable machine id (Windows `MachineGuid`, Linux
  `/etc/machine-id`, macOS `IOPlatformUUID`), so "same host" does not depend on
  hostnames or IPs.
- Watching: the node rescans the directory once per second and on any HELLO;
  a new file -> connect lazily when something needs the peer.
- The files are readable by hand, `jdBasic --nodes` just lists them.

### 7.2 LAN: beacons

- UDP multicast `239.255.74.66:47474` (TTL 1), one beacon per second per
  `"lan"` node: id, name, host, machine id, tcp port, version, key
  fingerprint (first 8 bytes of SHA-256 of the key, so nodes with different
  keys ignore each other without trying).
- A peer not heard for 3.5 s is presumed gone (confirmed by the TCP link if one
  is open).
- Networks without multicast (VPN, some Wi-Fi): `JDB_NODE_SEEDS=192.168.0.30,
  192.168.0.113` sends the same beacon by unicast to each seed, and seeds pass
  their peer lists on in `WELCOME`.
- Broadcast fallback (`255.255.255.255`) when the multicast join fails.

### 7.3 Topic routing

No broker: each node advertises its published topic names to every peer it is
connected to (`ADVERT`), and peers ask each other for adverts on connect. A
subscription pattern is matched locally against all known adverts; for every
match the subscriber sends `SUB` (with policy and credits) to that publisher.
New adverts later are matched against existing subscriptions, so a subscriber
that started first gets the publisher when it appears, and a restarted
publisher is picked up again without the subscriber doing anything.

Links are opened lazily: a node connects to a peer only for a SEND, CALL or a
matching subscription. A subscriber with `"*"` (spy, MCP) connects to everyone.

## 8. Runtime internals

- **One I/O thread per process** (`node_io`), started at the first JOIN. It
  owns all sockets, segments, the directory watch, beacons, pings and timers
  (poll/epoll/kqueue/WSAPoll, one wake-up pipe/socket for the VM threads).
- **Delivery = `Channel` objects.** Inboxes, subscriptions and call replies are
  entries in the existing `g_channels` registry (`src/channels.h`). The I/O
  thread decodes JDV into fresh `Value`s (refcounts are atomic) and pushes them
  with the channel's policy. CHAN.RECV/SELECT need no change apart from the
  new policies of 4.6.
- **Sending** from any VM thread: encode on the calling thread (it owns the
  values), push the bytes into the link's outgoing queue, poke the I/O thread.
  Topics with no subscribers cost only a lookup: nothing is encoded.
- **Process-global state**, like the channel registry: ASYNC FUNCs (which get
  fresh VMs, see the async-globals note) can use any publisher or subscription
  handle they are given as an argument. NODE.JOIN itself belongs on the main
  thread.
- **Flow control** for `queue:N`: the subscriber grants N credits; the
  publisher sends while credit is left; each RECV returns one credit (batched,
  every N/4). Without credit: `"drop"` publishers count a drop for that
  subscriber, `"block"` publishers wait.
- **Shutdown**: program exit / `END` / VM teardown -> BYE on every link, close
  channels with EOF, remove the directory file. `chan_shutdown_all` already
  exists for the channel half.
- **Embedding**: `jdbrt.dll` hosts (Godot) get the same, the I/O thread is
  inside the DLL. The MCP server (one VM, one worker thread) gets a node too,
  see P6.
- **Code layout**: `src/node.h/.cpp` (directory, links, I/O thread, routing),
  `src/jdv.h/.cpp` (codec, also used by tests), `src/node_shm.cpp` (P3),
  `src/vm_builtins_node.cpp` (natives). Under the existing **NET** flag; the
  codec (`NODE.PACK$`/`UNPACK`) and the CHAN policies are always built.

## 9. Clock

- On one machine `steady_clock` is system-wide (QPC on Windows,
  `CLOCK_MONOTONIC` on Linux, `mach_continuous_time` on macOS), so
  `NODE.TIME()` is the same timebase in every process: offset 0, exact.
- Across machines: per link, `PING`/`PONG` with four timestamps (NTP style).
  Keep the last 16 samples, take the offset of the sample with the smallest
  round trip, error = half that round trip. Ping every 2 s, faster (every
  100 ms) for the first second of a link. On a quiet LAN expect an error below
  0.5 ms; drift between two crystals (~20 ppm, 1.2 ms per minute) is tracked by
  the repeated pings.
- `NODE.TIME_OF` and envelopes apply the offset, so a receiver works only with
  its own clock.

### 9.1 Musical sync on top

The sender publishes **anchors** on a `"latest"` topic, a few per second and
immediately on every jump or tempo change:

```
{"CYCLE": c, "T": t_audible, "CPS": cycles_per_second, "EPOCH": n}
```

`T` is when cycle `c` leaves the loudspeaker, not when it was mixed. That
needs one new sound native: **`SOUND.CLOCK()`** -> `{"CYCLE","T","CPS"}` where
`T` = `NODE.TIME()` of the next mixed sample plus the device latency the audio
backend reports. The receiver converts the anchor's `T` to its own clock once
and then computes the cycle for each frame:

```
T_local   = NODE.TIME_OF(sender, T)
cycle_now = CYCLE + (NODE.TIME() - T_local) / 1e9 * CPS
```

Between anchors the show runs on its own clock, so the
frame rate does not depend on the network. `EPOCH` changes on SEEK/REBUILD, so
the show drops its section cache and jumps without easing.

Wave data (scope, buses) is published with the time of its last sample. The
show draws the window that ends at the audible time; the sender keeps a
somewhat longer history (4096 samples) so the receiver can pick the aligned
1024.

## 10. Security

- `"local"` scope: AF_UNIX socket files and shared memory in the user's
  runtime directory, so only the same user reaches them; TCP fallback binds
  `127.0.0.1` only.
- `"lan"` scope requires `JDB_NODE_KEY` (or `NODE.JOIN name$, "lan", key$`).
  HELLO/AUTH is a challenge-response with HMAC-SHA256 over both nonces; links
  without a valid answer are closed before any payload is decoded.
  `src/jdb_crypto.h` already carries SHA-256 and PBKDF2-HMAC-SHA256, the HMAC
  comes from there.
- No encryption in v1: the farm LAN is trusted. TLS is a later option on the
  same frames.
- Decode limits of 5.1 apply to every link, authenticated or not.
- Calls only reach methods the program registered with `NODE.SERVE` or reads
  itself from its inbox. There is no remote `EXECUTE`.

## 11. Failure semantics

| Situation | Behaviour |
|-----------|-----------|
| Publisher process dies | Its subscribers stay open and silent; `$sys/peers` gets LEAVE. When a node with the topic appears again, the subscription reattaches. |
| Subscriber dies | Publisher drops its link; no blocking, no growth. |
| Peer unreachable for SEND | `NODE.SEND` returns FALSE, `NODE.ERROR$()` says why. |
| CALL target gone or slow | Timeout marker after `timeout_ms`; a reply that arrives later is discarded. |
| Remote handler raises | `NODE.CALL` raises locally: `remote <addr>: <message>`. |
| Name already in use | `NODE.JOIN` raises `name "x" is in use by pid 1234`. |
| Malformed or oversized frame | Link closed, error logged, process continues. |
| Message ordering | FIFO per publisher and topic, and per sender for inbox messages. No order across different publishers. |
| Delivery guarantee | At most once. `queue` loses nothing while both sides live; nothing is persisted across a crash. |
| Network split on the farm | Peers expire after 3.5 s, reattach on the next beacon. |

## 12. Performance targets

Measured by `tests/node/bench_node.jdb` (P0 for the C++ spike, then for real):

| Path | Small message latency | Throughput, packed f64 arrays |
|------|-----------------------|-------------------------------|
| Same process, two nodes | under 20 us | over 2 GB/s |
| Same machine, local socket | under 50 us | over 500 MB/s |
| Same machine, shared memory (P3) | under 10 us | over 3 GB/s |
| LAN 1 GbE | under 500 us | over 100 MB/s (line rate) |

The interpreter's own cost (building the map, encoding) should dominate, not
the transport.

### 12.1 P0 results (2026-10-19, `tools/node_spike`)

Two processes on one machine, raw bytes without encoding. Round trip of a
64-byte message (median / p99, us), then one-way throughput in MB/s for 1 KB,
16 KB, 256 KB and 8 MB messages. The shm ring polls with a spin, so its
latency is the floor of a core burning, not what a sleeping receiver gets.

| Machine | Path | RTT med | RTT p99 | 1K | 16K | 256K | 8M |
|---------|------|---------|---------|----|-----|------|----|
| Windows dev box | tcp | 40.0 | 66 | 115 | 1450 | 5100 | 3600 |
| | unix | 25.8 | 42 | 850 | 8100 | 34000 | 5400 |
| | shm | 0.2 | 3.8 | 7600 | 11900 | 12300 | 12000 |
| render (Linux) | tcp | 5.3 | 10.6 | 600 | 6500 | 14000 | 13100 |
| | unix | 2.5 | 4.8 | 2600 | 15500 | 18200 | 16800 |
| | shm | 0.2 | 3.0 | 12800 | 20800 | 21400 | 17700 |
| cortex (Linux) | tcp | 3.7 | 7.6 | 810 | 8800 | 16200 | 14700 |
| | unix | 3.6 | 11.4 | 3000 | 19000 | 20100 | 16600 |
| | shm | 0.7 | 5.0 | 13300 | 28900 | 21300 | 22300 |
| mac mini (M1) | tcp | 15.8 | 22 | 540 | 6550 | 15800 | 6300 |
| | unix | 3.6 | 6.3 | 1760 | 14250 | 26600 | 7200 |
| | shm | 0.17 | 0.21 | 5300 | 23000 | 25100 | 13900 |

Decisions:
- **AF_UNIX is the local transport** on all four machines. It beats loopback
  TCP everywhere; for 1 KB messages by 7 times on Windows, 3 to 4 times on
  Linux and the mac.
  Loopback TCP stays only as the fallback.
- **P3 (shared memory) is parked.** AF_UNIX moves at least 5 GB/s for large
  messages on every machine, which is 1000 times U1's budget and more than an
  interpreter can fill. Shared memory wins only on latency with a spinning
  receiver; with a doorbell for a sleeping receiver it falls back to socket
  latency. Revisit when a measured program is bound by the transport.
- The local socket row of the targets holds with a wide margin (26 us RTT on
  Windows, under 4 us on Linux and the mac).

**Budget for U1:** per frame master scope 4096 f32 (16 KB) + 2 bus scopes
(32 KB) + 16 levels + loudness + anchor, about 50 KB at 60 Hz = 3 MB/s. Local:
negligible. LAN: 2.4 % of 1 GbE, and Wi-Fi 5 still has headroom.

## 13. Tooling

- `jdBasic --nodes`: table of all local nodes (directory) and LAN nodes heard
  in 2 s, with their topics, rates and subscriber counts.
- `tools/nodespy.jdb`: subscribes `"*"` with envelopes, prints a live table
  (topic, rate, bytes/s, last value shortened) and can dump one topic.
- MCP: the jdBasic MCP server joins as node `mcp`; new tools `jdb_nodes`
  (peers + topics) and `jdb_peek topic` (latest value). Then Claude can see
  what a running songgen or game publishes, without stopping it.
- `NODE.TRACE flag`: logs every frame header to stderr (kind, stream, bytes),
  for debugging routing.

## 14. Applying it: songgen and stage

### 14.1 What songgen publishes (`--live`, node `songgen`)

| Topic | Policy (receiver) | Retained | Content |
|-------|-------------------|----------|---------|
| `song/clock` | latest | yes | anchor of 9.1 |
| `song/score` | queue:4 | yes | full song structure after each BUILD: `SCORE.SNAPSHOT()` (sections, moods, sweeps, track names, bpm, end bar) + title, key, genre, seed |
| `song/scope` | latest | no | `{"T","MASTER","BUS0","BUS1"}` as f32 arrays (packed) |
| `song/levels` | latest | no | `SOUND.LEVELS()`, `SOUND.LOUDNESS()`, mutes |
| `song/log` | ring:64 | no | the SCORE log lines as they happen |

Calls songgen serves: `JUMP part`, `REROLL`, `SEED n`, `GENRE g$`, `MUTE t,f`
(so a second desk or a phone dashboard can steer the same song).

### 14.2 What stage needs

- **A source layer in stage.jdb**: `SRC.CYCLE()`, `SRC.WAVE()`,
  `SRC.BUS(i)`, `SRC.LIVE_AT(c)` with two implementations: local (today's
  direct SOUND/SCORE reads) and follow (the topics above). The drawing code
  stays as it is.
- **`SCORE.SNAPSHOT()` / `SCORE.LOAD(map)`** in score.jdb, so the follower
  holds the same structure the SCORE.* queries answer from, without a sound
  engine. `SCORE.LIVE_AT`, `SECTION_AT`, `SWEEP_*` work unchanged on the
  loaded data.
- **`stage.jdb --follow [songgen@host]`**: joins as `stage`, subscribes, waits
  for the retained score and clock, runs until songgen leaves (or Esc). A new
  `song/score` with a new EPOCH reloads the structure in place.
- **Choosing the display**: `SCREEN ..., [display]` or `GFX.DISPLAY n` (SDL3
  `SDL_GetDisplays`) so the show opens fullscreen on the second monitor or the
  projector. Small, separate ticket.
- songgen starts its own follower on request: `--live --stage` spawns
  `stage.jdb --follow` as a child process (needs `OS.SPAWN`, see 15).

Acceptance for U1: desk and show in two windows, a jump or reroll shows up in
the show within one frame of the audible change, the show can be closed and
started again while the song plays, and the desk's frame rate does not drop by
more than 5 % with the show attached.

## 15. Spawning processes

The bus is much easier to test and to use with a non-blocking spawn, which
jdBasic does not have (`OS.EXEC` waits):

- `OS.SPAWN(command$, [args_array$], [opts_map]) -> pid`: starts and returns.
  `opts`: `"CWD"`, `"ENV"`, `"HIDDEN"`.
- `OS.WAIT(pid, [timeout_ms]) -> exit code or NONE`, `OS.KILL(pid)`,
  `OS.ALIVE(pid)`.
- `OS.SELF$()` -> path of the running jdBasic (or the compiled exe), so a
  program can start another jdBasic script without guessing paths.

Separate ticket, useful far beyond the bus (gate tests, run_party, tv
pipeline).

## 16. Platforms

| Target | Plan |
|--------|------|
| Windows | Winsock, AF_UNIX (Win10 1803+), `CreateFileMapping` for P3, `MachineGuid`. Primary dev target. |
| Linux (cortex, render) | AF_UNIX, epoll, `shm_open`, `/etc/machine-id`. |
| macOS (mac mini) | AF_UNIX, kqueue, `shm_open` (names max 31 chars: use the short hex id), `IOPlatformUUID`. |
| WASM | Not built (no sockets). The codec is built. |
| Pico / ESP32 | Not in scope. JDV + frames are small enough for a later ESP32 TCP client (`"lan"` node without directory, beacons only). |

## 17. Testing

- **Codec** (`tests/node/jdv_test.jdb`): PACK/UNPACK round trip of every type,
  packed arrays of every dtype and rank, maps in arrays in maps, UDT -> map,
  refusal of funcrefs/handles, truncated and corrupted input (every prefix of a
  valid message must raise, never crash), size limits. Both backends.
- **CHAN policies** (`tests/async/chan_policy_test.jdb`): latest and ring
  semantics, with ASYNC producers.
- **In-process nodes** (`tests/node/node_local_test.jdb`): two `NODE.OPEN`
  nodes in one program: SEND, CALL/REPLY/FAIL, timeouts, topics with each
  policy, retained, patterns, reattach after `NODE.LEAVE` + JOIN, envelopes,
  name collision. Uses the real local transport, so it covers the link code.
- **Two processes** (`tests/node/node_proc_test.jdb` + `node_peer.jdb`): spawns
  the peer with `OS.SPAWN`, interp x native in all four combinations, kills
  the peer mid-stream and checks LEAVE + reattach.
- **Gate**: the codec and the in-process node test join the gate suites once
  stable (no network beyond loopback, so CI's clean checkout works).
  Multicast is never part of CI.
- **Farm** (`tools/node_farm_check.sh`, via jdbfarm): publisher on render,
  subscribers on Windows, cortex and mac; reports discovery time, rate,
  latency and clock offset error per pair. Manual, before each phase is
  closed.
- **Bench**: `tests/node/bench_node.jdb`, numbers recorded in the ticket so
  regressions are visible.
- **Fuzz** (optional): the codec decoder in a libFuzzer build on render.

## 18. Phases

Each phase is a trakr ticket (epic + one child per phase), committed and gated
on its own.

| Phase | Content | Acceptance | Size |
|-------|---------|------------|------|
| **P0 Spike** | C++ microbench outside jdBasic: AF_UNIX vs loopback TCP vs shm ring on Windows, Linux, mac; small-message latency and 16 KB / 8 MB throughput. | Table of numbers in the ticket; decision whether P3 is needed and for which sizes. | 1-2 days |
| **P1 Codec + CHAN policies** | `src/jdv.*`, `NODE.PACK$`/`UNPACK`, `CHAN.OPEN(n, policy$)`, docs, tests. | Codec test green in both backends, every corrupted prefix raises. | 2-3 days |
| **P1b OS.SPAWN** | `OS.SPAWN/WAIT/KILL/ALIVE/SELF$`, Windows + POSIX. | Spawn test green, no zombie processes on POSIX. | 1 day |
| **P2 Local nodes** | Directory, I/O thread, local socket links, JOIN/OPEN/LEAVE/PEERS/EVENTS, inbox, SEND, CALL family, SERVE/POLL, topics with policies, retained, patterns, credits, envelopes, `--nodes`, NODE.TRACE. | In-process and two-process tests green, interp x native; bench meets the local socket row. | 6-8 days |
| **P3 Shared memory** (if P0 says so) | Segments, SPSC ring, latest slots, one-shot bulk, doorbell. | Bench meets the shm row; kill tests leave no segments behind. | 3-5 days |
| **P4 songgen + stage** | `SOUND.CLOCK`, `SCORE.SNAPSHOT/LOAD`, stage source layer and `--follow`, songgen topics and calls, `--stage`, display choice. | Acceptance of 14.2 on Windows. | 3-4 days |
| **P5 LAN** | lan scope, TCP links, beacons, seeds, key auth, clock offset, TIME_OF, envelopes with converted time, farm check script. | Stage on mac follows songgen on Windows with offset error under 2 ms; farm check green on all three machines. | 4-6 days |
| **P6 Tooling + MCP** | nodespy, MCP node with `jdb_nodes` / `jdb_peek`, docs page `doc/NodeBus.md` with recipes for U1-U8. | Claude can list and peek a running songgen from the MCP. | 2 days |
| **P7 Later** | Channels inside messages (proxy channels), `TOPIC.SHARE` hardening for the farm job queue, Python client in devrig, ESP32 client, TLS. | Ticket per item. | - |

The order puts value early: after P2 two local programs can already work
together; after P4 the show runs in a second window; P5 brings the farm.

## 19. Decisions

1. **Naming**: `NODE.*` + `TOPIC.*`; receiving goes through `CHAN.*`.
2. **Flag**: under `NET`; the codec and the CHAN policies are always built.
3. **Several nodes per process** via `NODE.OPEN`: yes.
4. **LAN key**: mandatory for `"lan"` scope.
5. **Brokerless**: no daemon.
6. **Own implementation**, no nng.

## 20. Alternatives considered

- **nng / ZeroMQ**: proven transports and patterns (pub/sub, req/rep,
  pipeline). Against: another dependency in four build scripts and three
  package flows; the real work here (JDV codec, channel integration, latest
  slots, clock sync, discovery by name) is not what they give us; nng has no
  shared-memory transport. Could still replace the socket layer of P2/P5 if
  that turns out costly; the API above would not change.
- **MQTT with a broker** (mosquitto): needs a daemon, adds a hop and
  milliseconds of latency, JSON/text payloads.
- **HTTP / WebSocket on HTTP.SERVER**: one server per process, request
  oriented, text payloads, too slow for 60 Hz frames with arrays.
- **Shared memory only**: does not reach the farm.
- **One process with two windows** (the other route): sync for free, but no
  farm, no projector laptop, and it does not give programs a way to know each
  other.
