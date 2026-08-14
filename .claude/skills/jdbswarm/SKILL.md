---
name: jdbswarm
description: Fan out parallel jdBasic+Python compute across many isolated worker processes. Use when a task splits into many INDEPENDENT jdBasic/Python legs - batch data analysis, per-dataset number-crunching, per-file extraction, parallel RAG/embedding, Monte-Carlo - that would flood the main context or wants real multi-core parallelism. Each leg runs as a `jdbasic-worker` (its own one-shot build/jdBasic.exe process: isolated VM + Python namespace + GIL, exits clean). Orchestrate non-trivial swarms deterministically with the Workflow tool, pinning `agentType: 'jdbasic-worker'`. NEVER fan jdBasic out through the shared workshop MCP - one VM on one worker thread serialises and hangs.
---

# /jdbswarm - swarm jdBasic+Python over worker processes

The complement to `agentco`: where agentco gives you ONE warm local brain
(semantic search + bulk digest, you stay the reasoner), jdbswarm gives you **N
parallel jdBasic processes** for independent compute. Big jobs use both - agentco
to FIND/triage, jdbswarm to PROCESS in parallel.

## Is a swarm appropriate? (decide first)

Swarm only when ALL three hold - otherwise don't:
1. **Many independent items** (datasets, files, parameter sets, shards) - the legs
   don't depend on each other's results mid-flight.
2. **Each leg does real jdBasic/Python compute** (numpy/scipy crunching, RAG/embed,
   parsing, simulation) - not just reading code.
3. **Volume or parallelism pays** - the work would flood the main context, or many
   cores would genuinely shorten wall-clock.

Route elsewhere when:
- One quick computation -> just run it inline (a single `./build/jdBasic.exe x.jdb`
  via Bash, or the shared workshop MCP `jdb_eval` in your own dialog). No swarm.
- The legs only **read/search code** -> `Explore` / `general-purpose` agents.
- You need **semantic search / digest over a codebase** -> `agentco` (one warm VM).

## The golden rule

Each leg is a **`jdbasic-worker`** - a Bash-one-shot process. NEVER fan jdBasic
work out through the shared workshop MCP (`jdbasic-stdio-win`): it is one VM on one
worker thread, so N agents calling `jdb_eval` serialise and head-of-line block.
That hung 4 of ~30 agents in a past review. `jdbasic-worker` has NO MCP tools by
design, so it physically cannot do this - the safety is built in.

Proven (2026-06-13): two `jdbasic-worker` legs returned different OS PIDs,
isolated Python state, ~13 s each, processes exited clean (no leak).

## Workflow-first recipe (the deterministic way)

For any non-trivial swarm, author a **Workflow** and PIN the agent type - that is
how you guarantee the legs are jdBasic workers, in code, not by my good intentions:

```js
export const meta = {
  name: 'jdb-batch-analyze',
  description: 'Run a jdBasic/Python analysis over each input in parallel',
  phases: [{ title: 'Analyze' }, { title: 'Synthesize' }],
}
const RESULT = { type:'object', properties:{ item:{type:'string'}, value:{type:'number'} }, required:['item','value'] }

// `args` is the work-list you pass in (e.g. dataset paths). Scout it inline first.
const results = await parallel(args.map((item, i) => () =>
  agent(
    `You are a jdbasic-worker. Write tmp/swarm_${i}.jdb that loads "${item}", ` +
    `computes <the metric> with jdBasic/Python, PRINTs it, then run ` +
    `./build/jdBasic.exe tmp/swarm_${i}.jdb and return {item, value}. Delete the script after.`,
    { agentType: 'jdbasic-worker', label: `analyze:${item}`, phase: 'Analyze', schema: RESULT }
  )
)).then(r => r.filter(Boolean))

// You stay the reasoner: synthesize the small results yourself, or one more agent().
return results
```

- **`pipeline(items, stage1, stage2, ...)`** when each item flows through stages
  (compute -> verify) with no barrier - the default for multi-stage.
- **`parallel(thunks)`** when you need ALL results together (a barrier) - e.g. to
  dedup/synthesize across the whole set.
- Concurrency is capped at ~`min(16, cores-2)`; pass up to 4096 items, they queue.
- Ad-hoc 2-3 legs don't need a Workflow - just call the Agent tool with
  `subagent_type: 'jdbasic-worker'` directly.

## The jdbasic-worker contract (what each leg does)

1. Write its program to a **uniquely-named** `tmp/swarm_<i>.jdb` (the index/label
   keeps concurrent legs from clobbering each other). `PRINT` the result.
2. Run `./build/jdBasic.exe tmp/swarm_<i>.jdb` from the repo root via Bash.
3. Read the stdout, return only the small result (the bulk stays in its process).
4. Delete its tmp script.

Python in the worker: `PYTHON$("...")` (lines joined with `CHR$(10)`, no string
escapes), `PY.EVAL("expr")`, `PY.SET`/`PY.GET`. numpy + matplotlib available,
pandas not. The first `import numpy` in each fresh process is cold (a few seconds)
- normal and unavoidable for a one-shot, so keep per-leg work meaty enough that the
cold start is amortised (don't swarm 500 trivial one-liners; batch them).

## Anti-patterns (the "go sure" negatives)

- **Shared workshop MCP for fan-out** -> head-of-line hang. Use `jdbasic-worker`.
- **Inline `mcpServers` per subagent** -> full cold start per leg by design
  (process + MCP handshake + tool discovery + cold numpy). Tested and rejected.
- **Asking a worker's local 3b model to "list/enumerate all X"** -> it under-counts.
  Enumerate with grep / `CO_SEARCH`; let the model JUDGE each item (agentco doctrine).
- **One worker per trivial item** -> the cold-numpy tax dominates. Batch items per
  worker so each leg does enough to be worth a process.

## Composition with agentco

| | agentco | jdbswarm |
|---|---|---|
| Shape | ONE warm co-processor VM | N parallel one-shot processes |
| For | find code by meaning, bulk-digest files | independent compute over many items |
| State | warm across `jdb_eval` calls | fresh per leg, exits clean |

Pattern for a large task: `agentco` `CO_SEARCH`/`CO_DIGEST` to locate + triage the
work-list, then **jdbswarm** to process the items in parallel, then you synthesize.
