# The Node Bus

jdBasic programs that know each other: a program joins the bus under a
name, and then other programs find it, send it messages, call it, and
subscribe to the topics it publishes. On one machine the links are local
sockets; with the `"lan"` scope they reach the other machines of the
network.

This page is a set of recipes. The reference for every `NODE.*` and
`TOPIC.*` call is in [languages.md](languages.md), section *Node Bus*; the
design is in [design/node_bus_plan.md](design/node_bus_plan.md).

## The pieces

| Piece | What it is |
|---|---|
| node | a program on the bus, `NODE.JOIN "name"`. Its address is `name@host`. |
| message | a value put into a node's inbox, `NODE.SEND addr$, value` |
| call | a request with an answer, `NODE.CALL(addr$, method$, arg)` |
| topic | a named stream a node publishes, `TOPIC.OPEN("song/clock")` |
| subscription | a channel that receives every topic matching a pattern |

Any value travels: numbers, strings, maps, arrays and nested ones. An array
of one numeric kind goes as one block of raw numbers.

## Watching the bus

```
jdBasic --nodes                              the nodes of this machine and their topics
jdBasic tools/nodespy.jdb                    every topic, live: rate, KB/s, last value
jdBasic tools/nodespy.jdb --dump song/clock  every value of one topic
```

The MCP server offers the same to Claude: `jdb_nodes` lists the nodes and
their topics, `jdb_peek topic` shows the next value of a topic. Both work
while a program runs on the MCP's VM. The MCP joins as node `mcp` on first
use, in the lan scope when `JDB_NODE_KEY` is set.

## U1: one program draws what another one plays

The publisher sends its state once per frame; a viewer only wants the
newest value, so it subscribes with `"latest"`.

```basic
' the player
NODE.JOIN "songgen"
DIM clock = TOPIC.OPEN("song/clock")
DO
    CHAN.SEND clock, {"CYCLE": SOUND.CYCLE(), "T": NODE.TIME()}
    SLEEP 20
LOOP
```

```basic
' the viewer
NODE.JOIN "show"
DIM clock = TOPIC.SUBSCRIBE("song/clock", "latest")
DO
    DIM c = CHAN.TRY_RECV(clock)
    IF NOT CHAN.IS_TIMEOUT(c) THEN PRINT c{"CYCLE"}
    SLEEP 16
LOOP
```

A viewer that starts later still finds the publisher, and a publisher that
starts again is found by the same subscription. `songgen.jdb --live` and
`stage.jdb --follow` work this way.

## U2: the same across the network

Both sides join the lan scope with the same key, given as the third
argument or in `JDB_NODE_KEY`. Nodes announce themselves once a second, and
links prove the key before anything else crosses them.

```basic
NODE.JOIN "show", "lan"                      ' key from JDB_NODE_KEY
```

A time taken on the other machine is put on this machine's clock with
`NODE.TIME_OF(addr$, t)`; the links measure the offset. `NODE.PEERS` shows
it as `"OFFSET"` and `"ERROR"`.

## U3: a work queue for several workers

The producer opens the topic with `"block"`, so it waits when every worker
is busy. Each worker takes values with `TOPIC.SHARE`: every value goes to
exactly one of them.

```basic
' the producer
NODE.JOIN "jobs"
DIM queue = TOPIC.OPEN("render/jobs", "block")
DIM i AS INTEGER
FOR i = 1 TO 100
    CHAN.SEND queue, {"FRAME": i}
NEXT i
```

```basic
' each worker
NODE.JOIN "worker-" + STR$(OS.PID())
DIM jobs = TOPIC.SHARE("render/jobs")
FOR EACH job IN jobs
    PRINT "frame "; job{"FRAME"}
NEXT job
```

## U4: a service that answers requests

A service serves a method with a `FUNC` and runs the waiting calls with
`NODE.POLL`. A caller waits for the answer with `NODE.CALL`, or gets a
channel with `NODE.CALL_ASYNC` and goes on.

```basic
FUNC H_ASK(c)
    RETURN "you asked: " + c{"ARG"}
ENDFUNC

NODE.JOIN "llm"
NODE.SERVE "ASK", H_ASK@
DO
    DIM ran = NODE.POLL(100)
LOOP
```

```basic
NODE.JOIN "client"
PRINT NODE.CALL("llm", "ASK", "the weather", 30000)
```

A handler that fails answers the caller with the error; `NODE.FAIL` does
the same on purpose.

## U5: a dashboard fed by one collector

The collector retains its topic, so a dashboard that starts later gets the
current state first.

```basic
NODE.JOIN "collector"
DIM state = TOPIC.OPEN("f1/standings")
TOPIC.RETAIN state, TRUE
CHAN.SEND state, {"LEADER": "VER", "LAP": 31}
```

```basic
DIM standings = TOPIC.SUBSCRIBE("f1/standings", "latest")
DIM s = CHAN.RECV(standings, 2000)
```

## U6: watching a running program from outside

Subscribe to `"*"` with envelopes: each value arrives as a map with
`"DATA"`, `"FROM"`, `"TOPIC"`, `"SEQ"` and `"T"`. That is what
`tools/nodespy.jdb` does, and what `jdb_peek` does for one topic.

```basic
NODE.JOIN "spy"
DIM all = TOPIC.SUBSCRIBE("*", "queue:4096", TRUE)
DO
    DIM env = CHAN.RECV(all)
    PRINT env{"TOPIC"}; " from "; env{"FROM"}
LOOP
```

## U7: a game and its editor

Two programs, each publishing its own state and serving the commands of the
other. The game publishes `"game/state"` and serves `"SPAWN"`; the editor
subscribes to the state and calls the game.

```basic
' the game
FUNC H_SPAWN(c)
    ' c{"ARG"} is the map the editor sent
    RETURN TRUE
ENDFUNC

NODE.JOIN "game"
NODE.SERVE "SPAWN", H_SPAWN@
DIM state = TOPIC.OPEN("game/state")
DO
    DIM ran = NODE.POLL()
    CHAN.SEND state, {"PLAYER_X": 10, "PLAYER_Y": 4}
    SLEEP 16
LOOP
```

```basic
' the editor
NODE.JOIN "editor"
DIM state = TOPIC.SUBSCRIBE("game/state", "latest")
DIM ok = NODE.CALL("game", "SPAWN", {"KIND": "slime", "X": 3, "Y": 7})
```

## U8: big arrays between processes

Send the array; a numeric array is packed as one block, so it costs a copy,
not a conversion per element. For frames that only the newest counts, use
`"latest"`; for a stream where every block counts, `"queue:N"` with an N
that covers the reader's slowest moment.

```basic
NODE.JOIN "producer"
DIM blocks = TOPIC.OPEN("audio/blocks")
DO
    CHAN.SEND blocks, SOUND.GET_WAVE()
    SLEEP 20
LOOP
```

## When something does not arrive

- `jdBasic --nodes` shows whether the node joined and which topics it
  publishes.
- `NODE.TRACE TRUE` writes every frame that goes in or out to standard
  error: direction, peer, kind, stream and size.
- `TOPIC.STATS(ch)` counts what was sent, received and dropped.
- In the lan scope both sides need the same key.
