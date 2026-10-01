# Programming with an AI agent, and AI inside your program

[Docs home](../README.md) · [Tour](../tour.md) · [Language reference](../languages.md) · [Module library](../../lib/README.md)

This guide has two parts. Part 1 shows how an AI agent such as Claude Code
runs, inspects and patches your jdBasic program through the MCP server, while
the program keeps running. Part 2 shows how a jdBasic program calls a language
model itself: a local GGUF model, an ONNX model, or a hosted model over HTTP.

## Part 1: the agent works on your program

### 1. What the MCP server is

The jdBasic executable is also a [Model Context Protocol](https://modelcontextprotocol.io/)
server. Started with `--mcp`, it keeps one jdBasic VM alive and offers it to
the agent as a set of tools. Variables, `FUNC`s and loaded modules stay in
that VM from one tool call to the next, so the agent can load a program once
and then look at it and change it many times.

The server is part of the Windows release bundles. **core** has it, and
**mcp-native** adds the LLVM toolchain so that the agent can also compile
programs to a native `.exe` (see the [Getting started](../../README.md#getting-started)
section of the README). When you build from source, the flag is `MCPSERVER`.
Check your binary with `jdBasic.exe --version`: the `Features:` line must
list `MCP`, and `NativeC` if you want native compilation.

### 2. Connect Claude Code

Add an entry to `.mcp.json` in your project folder (this is the snippet from
[MCP.md](../MCP.md#2-wire-it-into-your-mcp-client)):

```json
{
  "mcpServers": {
    "jdbasic": {
      "command": "/absolute/path/to/jdbasic",
      "args": ["--mcp"]
    }
  }
}
```

On Windows the command is the full path to `jdBasic.exe`, with the
backslashes doubled inside the JSON string, for example
`"C:\\tools\\jdbasic\\jdBasic.exe"`. Restart Claude Code. The tools then
appear as `mcp__jdbasic__jdb_eval`, `mcp__jdbasic__jdb_load` and so on.
Cursor, Cline, Continue, Zed and Windsurf take the same `command` and `args`;
[MCP.md](../MCP.md#client-setup-notes) lists where each one keeps its config.

To check the connection, ask the agent to call `jdb_eval` with the arguments
`{"code": "PRINT SUM(IOTA(20))"}`. The answer is `210`. If the client reports "tool not found", the binary was
built without `MCPSERVER`.

The server runs in the folder the client started it in (for Claude Code, the
project folder). Relative paths in `jdb_load` and the workspace files of
`jdb_savews` are resolved against that folder.

### 3. A program to work on

The walkthrough uses a small window with a moving box. The program checks the
F6 key and executes `STOP` when it is pressed. `STOP` is the hand-off point:
the VM parks the running program, and the agent can then read and change
everything in it.

```basic
' bounce.jdb: press F6 to hand the program to the agent, Esc to quit
SCREEN 640, 360, "Bounce"
DIM x = 20
DIM speed = 4
DIM box_color = [80, 200, 255]
DIM f6_held = FALSE

SUB DRAW_FRAME()
    CLS 0, 0, 0
    RECT x, 160, 40, 40, TRUE, box_color[0], box_color[1], box_color[2]
    SCREENFLIP
ENDSUB

DO
    x = x + speed
    IF x < 0 OR x > 600 THEN speed = -speed
    DRAW_FRAME()
    IF GFX.KEYSTATE("F6") THEN
        IF NOT f6_held THEN STOP
        f6_held = TRUE
    ELSE
        f6_held = FALSE
    ENDIF
    SLEEP 16
LOOP UNTIL GFX.KEYSTATE("Escape")
GFX.CLOSE
```

`f6_held` makes the program stop once per key press, not on every frame
while the key is down. F6 is not built into the VM; it is a convention of the
demos. The space shooter in `jdb/demos/games/space_shooter/` does the same
with the helper module `claude_live.jdb` (`CLAUDE_LIVE.pause_pressed()`).

A program without such a key check can still be paused: the agent calls
`jdb_stop`, and the VM stops at the next check point, as if the program had
executed `STOP` itself.

### 4. Load and run it

Save the file in your project folder and ask the agent to load it with
`jdb_load` and the arguments `{"path": "bounce.jdb"}`. The window opens and the program runs in the server's worker. `jdb_load`
returns at once. `jdb_status` tells the agent what the VM is doing:
`running`, `stopped` (parked at a `STOP`) or `idle` (no program running).
While the program runs, most tools are rejected; `jdb_stop` and `jdb_status`
always answer.

### 5. Pause and inspect

Click into the window and press F6. `jdb_status` now reports `stopped`. The
agent can list the program's state:

| Tool | Arguments | Returns |
|---|---|---|
| `jdb_vars` | `{}` | every global with its value, for example `X = 292`, `SPEED = -4`, `BOX_COLOR = ARRAY [3] [80, 200, 255]` |
| `jdb_funcs` | `{}` | the user `FUNC`s and `SUB`s with their parameters, here `DRAW_FRAME()  SUB` |
| `jdb_status` | `{}` | `stopped, call jdb_resume to continue, or jdb_eval to inspect/mutate` |

`jdb_vars` shortens long values to 160 characters; pass `"max_chars"` to
change that (0 shows everything). When the program stopped inside a `SUB` or
`FUNC`, that frame's local variables are visible as globals until the program
resumes.

### 6. Change a value

`jdb_eval` runs statements in the same VM, so an assignment changes the
paused program directly:

| Tool | Arguments |
|---|---|
| `jdb_eval` | `{"code": "box_color = [255, 80, 80]\nspeed = speed * 2", "result": "[x, speed]"}` |

The optional `result` expression is evaluated after the code and comes back
as a separate block of plain JSON (here `[292,-8]`), which is easier for the
agent to read than `PRINT` output. `timeout_ms` (default 30000) stops a chunk
that runs too long, without losing the VM.

### 7. Edit the source and swap a SUB in

Some changes are not values but code. Say you want the box 60 pixels higher.
The agent edits `bounce.jdb` on disk (in `DRAW_FRAME`, `RECT x, 160, ...`
becomes `RECT x, 100, ...`) and then calls `jdb_recompile` with no
arguments. It reads the file again, compiles it and replaces the `FUNC`s and
`SUB`s with the same names in the live VM; new ones are added. It answers
with `added=0 updated=1`. Without `path` it uses the file of the last
`jdb_load`.

Top-level statements are not applied again: the paused program still runs its
old main loop. Code you want to change while the program runs therefore
belongs in a `SUB` or `FUNC`, as `DRAW_FRAME` does here.

### 8. Resume

The agent calls `jdb_resume` (no arguments). The program continues after
the `STOP`, with the red box at the new height and the doubled speed. Its position, direction and every other variable are
the values it had when you pressed F6.

### 9. Compile natively

`jdb_run_native` runs a command line in a child process and returns its
output and exit code. With the mcp-native bundle the agent uses it to compile
the program with `-c` and to start the result:

| Tool | Arguments |
|---|---|
| `jdb_run_native` | `{"command": "C:\\tools\\jdbasic\\jdBasic.exe -c bounce.jdb"}` |
| `jdb_run_native` | `{"command": ".\\bounce.exe", "timeout_ms": "10000"}` |

The compiler writes `bounce.exe` next to the source and copies `jdbrt.dll`
beside it. On Windows the command runs in `cmd`, so start the program as
`.\bounce.exe`. The native compiler is always strict: every variable needs a
`DIM` (see [the tour](../tour.md)). The default timeout is 120 seconds; a
program that does not end on its own, like `bounce.exe` here, runs into it.

### 10. Save and restore a workspace

| Tool | Arguments |
|---|---|
| `jdb_savews` | `{"name": "bounce"}` |
| `jdb_reset` | `{}` |
| `jdb_loadws` | `{"name": "bounce"}` |

`jdb_savews` writes `bounce.jsws` in the server's folder with the global
variables and the code entered through `jdb_eval`, including any `FUNC` or
`SUB` defined there. `jdb_loadws` clears the VM and restores that state.
`FUNC`s and `SUB`s that came from a `jdb_load` file are not in the
workspace; load the file again after `jdb_loadws` to get them back.
`jdb_reset` clears the VM and leaves workspace files alone.

### 11. Look things up

`jdb_doc` with `{"query": "GFX.KEYSTATE"}` searches `doc/languages.md` (shipped with the bundle) for the text
and returns up to eight matching entries. It is the agent's way to find out
whether a builtin exists and which arguments it takes. `jdb_check` parses
code without running it and returns `ok` or the parse error.

## Good habits

These come from the live sessions with the space shooter; its instructions
for the agent are in
[jdb/demos/games/space_shooter/CLAUDE.md](../../jdb/demos/games/space_shooter/CLAUDE.md).

- **Tell the agent the state of the VM.** If you press F6 before you type,
  say so (or put it in your project's `CLAUDE.md`). Then the agent does not
  call `jdb_stop`, and it does not call `jdb_load`, which would start the
  program again and lose its state.
- **Prefer `jdb_eval` for values.** A value change is one call. Edit the
  source and use `jdb_recompile` only when the logic of a `SUB` or `FUNC` has
  to change, and keep that edit small.
- **Keep the knobs in variables.** The shooter keeps every gameplay setting in
  one `MAP` (`g_config`) and its colours in a few more, so a request like
  "faster bullets" becomes `g_config{"bullet_speed"} = -20.0`. A table of
  such names in `CLAUDE.md` saves the agent from reading the whole program.
- **Avoid `CONST` for things you want to change live.** A `CONST` cannot be
  assigned through `jdb_eval`; declare it with `DIM` instead.
- **End each change with `jdb_resume`.** The order is: optionally one
  `jdb_eval` to read a value, then the change, then `jdb_resume`.
- **Let the agent check names with `jdb_doc`** when it is unsure whether a
  builtin exists, before it sends code to `jdb_eval`.
- **Save milestones with `jdb_savews`**, so the next session can start with
  `jdb_loadws`.

## Security

`jdb_eval` and `jdb_run_native` run arbitrary code on your machine, with
access to files, processes, the network and native DLLs. Treat the server like
a local shell: run it under your own user account, not as administrator or
root, and do not make the HTTP demo server (`jdb/demos/mcp_server`) reachable from the internet.
See [MCP.md](../MCP.md#security).

## Part 2: AI inside your program

### 12. Local language models with AI.*

The `AI.*` functions for local models need a build with the `LLM` flag
(llama.cpp) and a model file in GGUF format. `OS.FEATURE("LLM")` returns
`TRUE` when the build has it. The functions and their arguments are listed in
[Local LLMs (llama.cpp)](../languages.md#local-llms-llamacpp).

Load a model, set a system prompt and ask a question:

```basic
DIM llm = AI.LOAD_LLM("models/Phi-3-mini-4k-instruct-q4.gguf", 2048, 99)
AI.SET llm, "system", "You are a concise assistant. Answer in 2 sentences."
AI.SET llm, "temperature", 0.5
PRINT AI.CHAT(llm, "Why is BASIC a good language to teach with?")
```

`AI.LOAD_LLM(path$, [n_ctx], [n_gpu_layers])` puts all layers on the GPU with
`99`; use `0` for the CPU. `AI.CHAT` keeps the conversation history inside
the model, so a second call continues the same chat. `AI.CLEAR_HISTORY(llm)`
starts a new one, and `AI.FREE_LLM(llm)` releases the model.

**Streaming.** `AI.CHAT_STREAM` calls a function of yours for each token.
Return `TRUE` to continue and `FALSE` to stop:

```basic
FUNC ON_TOKEN(t)
    PRINT t;
    RETURN TRUE
ENDFUNC

DIM answer = AI.CHAT_STREAM(llm, "Name three rivers.", ON_TOKEN@)
PRINT
```

`AI.CHAT_TOKENS(llm, prompt$, [capacity])` returns a channel instead, which
you read with `CHAN.RECV` (see
[LLM Streaming via Channel](../languages.md#llm-streaming-via-channel-sugar)).

**JSON output.** `AI.CHAT_JSON` forces the model to answer with valid JSON
and returns the parsed object:

```basic
DIM obj = AI.CHAT_JSON(llm, "Give me Berlin as a JSON with name, country, population.")
PRINT obj{"name"}
```

`AI.SET_JSON_MODE(llm)` switches JSON output on for all following calls, and
`AI.SET_GRAMMAR(llm, gbnf$)` constrains the output to any GBNF grammar.

**Tools.** Register a jdBasic function as a tool. When the model asks for
it, `AI.TOOL_CHAT` calls the function and passes the result back to the
model:

```basic
FUNC GET_WEATHER(city)
    IF city = "Berlin" THEN RETURN "15C, cloudy"
    RETURN "Unknown city"
ENDFUNC

AI.TOOL_ADD llm, "WEATHER", "city_name", "Get current weather for a city", GET_WEATHER@
PRINT AI.TOOL_CHAT(llm, "What is the weather in Berlin right now?")
```

The arguments are `AI.TOOL_ADD(id, name$, params$, description$, funcref)`
and `AI.TOOL_CHAT(id, prompt$, [max_rounds=5])`.

### 13. Embeddings and RAG

An embedding model turns text into a vector; similar texts get similar
vectors.

```basic
DIM emb = AI.LOAD_EMBEDDINGS("models/bge-m3-Q4_K_M.gguf", 2048, 99)
DIM v1 = AI.EMBED_LLM(emb, "Berlin is the capital of Germany")
DIM v2 = AI.EMBED_LLM(emb, "Paris is the capital of France")
PRINT AI.COSINE_SIM(v1, v2)
```

A RAG store splits documents into chunks, embeds them and answers questions
with the best-matching chunks as context. The arguments are
`AI.RAG_CREATE(llm_id, [chunk_size], [overlap], [embed_llm_id])`:

```basic
DIM rag = AI.RAG_CREATE(llm, 500, 50, emb)
DIM stats = AI.RAG_ADD_DIR(rag, "docs", "*.md", 1)
DIM r = AI.RAG_QUERY_FULL(rag, "How do I open a window?")
PRINT r{"answer"}
AI.RAG_SAVE rag, "docs_index.idx"
```

Without an embedding model (`AI.RAG_CREATE(0, 400, 40)`) the store uses
TF-IDF, and `AI.RAG_SEARCH` returns the matching chunks without generating an
answer; `jdb/demos/ai/mini_rag.jdb` shows that. The full list, with the HNSW
index and the k-NN text classifier, is in
[RAG](../languages.md#rag-retrieval-augmented-generation).

### 14. ONNX models

With the `ONNX` flag, `AI.LOAD(path$)` loads an `.onnx` file and
`AI.RUN(id, input)` runs it. `AI.INFO(id)` shows the inputs the model
expects:

```basic
DIM m = AI.LOAD("models/mnist.onnx")
PRINT AI.INFO(m){"inputs"}
DIM pixels = IOTA(784) * 0.0
DIM probs = AI.SOFTMAX(AI.RUN(m, pixels))
PRINT "Predicted digit: "; AI.ARGMAX(probs)
AI.FREE m
```

A model with several inputs takes one argument per input, or one array of
inputs (`jdb/demos/ai/mini_onnx.jdb` passes `[image, kernel]`). See
[ONNX Runtime](../languages.md#onnx-runtime-classical-ml).

### 15. A hosted model over HTTP: LLMAPI

The module `LLMAPI` (`lib/llmapi.jdb`) talks to OpenAI, Anthropic and any
server with the OpenAI chat API (llama-server, vLLM, Ollama). It needs the
`HTTP` flag, not `LLM`. The key comes from `OPENAI_API_KEY` or
`ANTHROPIC_API_KEY`, or from the third argument of `LLMAPI.NEW`:

```basic
IMPORT LLMAPI

DIM ai = LLMAPI.NEW("openai", "gpt-4o-mini")
LLMAPI.SYSTEM(ai, "You answer in one sentence.")
PRINT LLMAPI.ASK$(ai, "Why is the sky blue?")
```

`LLMAPI.CHAT` returns a map with `text`, `tool_calls` and `usage`,
`LLMAPI.TOOL` declares tools and `LLMAPI.JSON` returns a structured answer
checked against a `SCHEMA`. The fourth argument of `NEW` points an
OpenAI-style call at another server, and the provider `"local"` uses a GGUF
model through `AI.*`. Details: [llmapi_lib_readme.md](../../lib/llmapi_lib_readme.md).

## Where next

- [MCP server setup and tool reference](../MCP.md)
- [An MCP server written in jdBasic](../../jdb/demos/mcp_server/README.md), the HTTP variant of the same idea
- [AI & Machine Learning](../languages.md#ai--machine-learning) in the language reference
- The demos in [jdb/demos/ai/](../../jdb/demos/ai/), starting with `mini_llm.jdb`, `mini_rag.jdb` and `mini_onnx.jdb`
- [Blog post: an AI agent edits a running game](https://www.atomijd.onl/blog-post-19.html)
