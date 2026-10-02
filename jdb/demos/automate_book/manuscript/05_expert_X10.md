<!-- pagebreak -->

## X10 Let an AI Drive

### The chore

By now Lena's team runs twenty automations. Most days they work.
Some days one of them does not, and the question "what happened last
night?" means opening the logs folder, finding the right file and
reading through it. Lena already uses an AI agent, Claude Code, for her
spreadsheets and scripts. She would like to ask it: "Did the backup run
last night? What is waiting in the outbox? Try the invoice run for
next month and tell me what it would do." The agent can only answer if
it can reach the automations, and Lena wants to decide exactly how far
it may reach.

### What you get

Four tools an AI agent can call, served by the MCP server that is
built into jdBasic:

| Tool | What the agent gets |
|---|---|
| `list_recipes` | Your installed automations: id, name, summary, and whether it may run them for real |
| `recipe_log` | The last twenty lines of one automation's log |
| `outbox` | The mails waiting in the outbox, with recipients and subjects |
| `run_recipe` | The output of one automation, run as a dry run |

A real run is possible only for the recipes you name in `work.conf`.
Nothing is sent from the outbox; the tool only lists it. Every call the
agent makes is a line in `logs/ai_drive.jsonl`, with the tool, the
arguments and what happened, so you can read afterwards what it did.

MCP, the Model Context Protocol, is the way AI agents such as Claude
Code and Claude Desktop find and call tools. An MCP server is a
program the agent starts; it lists its tools and answers their calls.
jdBasic is such a server when you start it with `--mcp`. With
`--tools-only` it offers tools of your own and nothing else, each
described by a small JSON file that names a jdBasic function.

### Before you start

You need a jdBasic built with the MCP server, and an agent that speaks
MCP. The forms release this book starts with has the server;
`jdbasic --version` lists *MCP* among its features. The steps below use Claude
Code. The setup wizard must have run, because the tools read your
recipes and their settings from the work folder.

> **Watch out**
> Started with `--tools` instead of `--tools-only`, the jdBasic server
> also offers its own tools, such as `jdb_eval`, which runs any jdBasic
> code the agent writes. With that tool an agent can do whatever you can
> do on your computer, past every rule of this recipe. The command this
> recipe prints uses `--tools-only`; keep it that way.

### The program

The program writes the four tool files and prints the command that
registers the server:

<!-- include recipes/expert/X10_ai_drive/ai_drive.jdb -->

The module holds the tools themselves, the tool files and a check that
asks the server which tools it offers:

<!-- include recipes/expert/X10_ai_drive/aitools.jdb -->

### How it works

1. **How the server finds a tool.** For every `.json` file in the
   folder after `--tools`, the server reads the tool's name, its
   description, the arguments it takes and the jdBasic function that
   handles it. It runs the module file once at start, and for each
   call it hands the function the arguments as JSON text and passes
   the text it answers back to the agent.
2. **The tool files.** `MANIFESTS` builds the four descriptions.
   `Manifest` puts each one together: name, description, an
   `inputSchema` that tells the agent which arguments exist, the
   module and the handler. The descriptions are short on purpose. An
   agent reads them to decide which tool fits a question, so they say
   what the tool does in plain words.
3. **Fresh settings for every call.** Each tool reads `work.conf`
   again, so a change you make while the agent runs counts from the
   next call on. `RECIPES` reads the `recipe.toml` of every recipe the
   wizard installed in the work folder.
4. **Reading only.** `TOOL_LIST`, `TOOL_LOG` and `TOOL_OUTBOX` read and
   answer. `TOOL_OUTBOX` uses `OUTBOX.PENDING`, the same function the
   recipes of Chapter 4 use, and opens each mail only to read its
   recipients and subject.
5. **A dry run unless allowed.** `TOOL_RUN` starts the recipe's
   program with the `jdbasic` the wizard found, with `--config` and
   with `--dry-run`. Only when the agent asks for a real run and
   `ALLOWED` finds the recipe's id in `[ai_drive] allow` does it leave
   out `--dry-run`. Otherwise it refuses and tells the agent to ask
   you. The answer is the program's output, cut to 4000 characters.
6. **The audit trail.** `Audit` writes one JSON line per call into
   `logs/ai_drive.jsonl` through the LOGGER library: time, tool,
   arguments and outcome, the refusals included.
7. **The check.** `CHECK` starts the server the way an agent would,
   sends it the two requests an agent sends first, and reads the names
   in the answer. That is what `--check` prints.

### Run it

Write the tool files and get the command for Claude Code:

```
jdbasic ai_drive.jdb
wrote C:\Users\lena\Documents\AutomateWork\ai-tools\list_recipes.json
wrote C:\Users\lena\Documents\AutomateWork\ai-tools\recipe_log.json
wrote C:\Users\lena\Documents\AutomateWork\ai-tools\outbox.json
wrote C:\Users\lena\Documents\AutomateWork\ai-tools\run_recipe.json
Real runs allowed for: 0 recipe(s)
Tell Claude Code about the server once, in a command prompt:
  claude mcp add automate-work -e AUTOMATEWORK_CONF="C:\Users\..."
    -- "C:\Users\lena\jdBasic\jdbasic.exe" --mcp --tools-only "C:\..."
```

Type the two printed lines as one command. Then check that the server
starts and offers the tools:

```
jdbasic ai_drive.jdb --check
The server offers: list_recipes, outbox, recipe_log, run_recipe
```

Start Claude Code and ask, for example, "Which of my automations wrote
an error into its log this week?" The agent calls `list_recipes`, then
`recipe_log` for each recipe, and answers from what the logs say. Each
call appears in `logs/ai_drive.jsonl`:

```
{"time":"2026-10-02 07:20:28","level":"INFO","logger":"ai_drive",
 "message":"refused","tool":"run_recipe",
 "args":"{\"id\":\"X10\",\"for_real\":true}"}
```

### Schedule it

There is nothing to schedule. The agent starts the server when it
needs it and stops it when it is done. Run `ai_drive.jdb` again only
when you move the work folder, change the module or add a tool.

### Make it yours

The setting is in the part of `work.conf` that starts with
`[ai_drive]`:

```toml
[ai_drive]
allow = ["E11", "M08"]
```

- **Real runs.** `allow` names the recipes the agent may run for real.
  Start with recipes that only read and report, such as the Disk Space
  Report (E11) or the Personal Dashboard (M08). A recipe that moves
  files or writes mail is better left to a dry run, with you deciding.
- **Ask before every call.** Claude Code asks before it calls a tool.
  Allow `list_recipes`, `recipe_log` and `outbox` for good, since they
  only read, and keep answering each `run_recipe` request yourself.
- **A tool of your own.** Write a function in `aitools.jdb` that takes
  the arguments as text and answers text, such as `TOOL_HOURS` that
  reads this week's hours from the Time Tracker's log (E07). Add a
  `Manifest` line for it in `MANIFESTS`, run `ai_drive.jdb` again and
  restart the agent.
- **Another agent.** Claude Desktop and other MCP clients start the
  same server with the same command; only the place where you enter it
  differs. In Claude Desktop it goes into its configuration file under
  `mcpServers`.

### When it goes wrong

- **`--check` lists only the jdb tools**: the server could not load
  `aitools.jdb`. Run the server by hand with the variable
  `JDBASIC_MCP_LOG=1` set; it then reports on the error output which
  module it tried and why it failed. A missing module such as WORKCONF
  means the wizard's library folder `.jdbasic\lib` is missing.
- **"The server did not answer"**: `jdbasic` in `[paths]` of
  `work.conf` does not point to a jdBasic with the MCP server. Run
  `jdbasic --version` and look for *MCP* among the features.
- **"No recipe X"**: the agent used an id that is not installed. It
  usually asks `list_recipes` next and corrects itself.
- **A dry run that changes something**: a recipe without a dry run of
  its own ignores `--dry-run`. All recipes of this book have one; for a
  recipe of your own, check it before you let an agent run it.

> **Balance dividend**
> About 20 minutes a week of reading logs and checking runs, given to
> an agent that does it when you ask, inside the limits you set.
