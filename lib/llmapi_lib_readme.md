# LLMAPI - one chat client for the remote and the local models

`lib/llmapi.jdb` talks to OpenAI, Anthropic, any server that speaks the
OpenAI chat API (llama-server, vLLM, Ollama) and the local `AI.*` model,
behind one message shape. Tools, structured output and token usage
work the same way on each.

Stands in for: the openai and anthropic SDKs.

## Quick start

```basic
IMPORT LLMAPI

DIM ai = LLMAPI.NEW("openai", "gpt-4o-mini")        ' key from OPENAI_API_KEY
LLMAPI.SYSTEM(ai, "You answer in one sentence.")
PRINT LLMAPI.ASK$(ai, "Why is the sky blue?")

DIM claude = LLMAPI.NEW("anthropic", "claude-sonnet-5")
DIM reply = LLMAPI.CHAT(claude, [LLMAPI.USER("Name three rivers.")])
PRINT reply{"text"}; " ("; reply{"usage"}{"output"}; " output tokens)"

DIM shape = SCHEMA.NEW({"city": "string!", "population": "integer!"})
DIM facts = LLMAPI.JSON(ai, "Berlin, please.", shape)
PRINT facts{"population"}
```

## Messages and replies

A message is a map with `role` and `content`. `USER(text$)` and
`ASSISTANT(text$)` build them. `CHAT` takes a prompt string or an array
of messages and returns a reply:

| Key | Meaning |
|-----|---------|
| `role` | `"assistant"`, so the reply can go straight back into the next call. |
| `text` | The text of the answer. |
| `tool_calls` | `[{id, name, arguments}]`, arguments already parsed. |
| `usage` | `{input, output}` token counts. |
| `finish` | The vendor's finish or stop reason. |
| `raw` | The vendor's response as parsed JSON. |

A tool answer is built with `TOOL_RESULT(call, content$)` and appended
after the reply that asked for it. The module converts the transcript
to each vendor's own format on the way out.

## API

| Call | What it does |
|------|--------------|
| `NEW(provider$, model$, [key$], [base_url$])` | `"openai"`, `"anthropic"` or `"local"`. The key comes from the call or from `OPENAI_API_KEY` / `ANTHROPIC_API_KEY`; the base URL points an OpenAI-style call at another server. For `"local"` the model is a GGUF path. |
| `SYSTEM(c, text$)` | The system prompt. |
| `SET(c, key$, value)` | `"temperature"`, `"max_tokens"` (1024), `"timeout"` (120 s). |
| `TOOL(c, name$, description$, params)` | Declares a tool; `params` is a SCHEMA declaration map, a schema from `SCHEMA.NEW`, or a JSON Schema object. |
| `CLEAR_TOOLS(c)` | Removes them. |
| `CHAT(c, messages)` | A reply map, see above. |
| `ASK$(c, prompt$)` | The text of `CHAT`. |
| `JSON(c, messages, shape)` | The answer parsed into a map, in the shape declared. |
| `USER(text$)` / `ASSISTANT(text$)` / `TOOL_RESULT(call, content$)` | Message builders. |

## How each vendor is driven

- **OpenAI**: `POST {base}/chat/completions` with a bearer key. Tools go
  out as functions. Structured output uses `response_format` with
  `json_schema` in strict mode; strict mode takes only `type`,
  `properties`, `required`, `items`, `enum` and `description` and wants
  every property required, so the schema is reduced to that and the
  limits it cannot carry are spelled out in the descriptions.
  `SCHEMA.CHECK` on the answer applies the full declaration.
- **Anthropic**: `POST {base}/v1/messages` with `x-api-key` and the
  version header. The system prompt is a top-level field, tools carry
  `input_schema`, structured output is a forced call of an `answer`
  tool whose input is the schema, tool answers are `tool_result` blocks.
- **Local**: `AI.LOAD_LLM` on first use, then `AI.CHAT`; JSON through
  `AI.CHAT_JSON`. The model keeps its own history, so only the last turn
  is sent and earlier turns are folded into the prompt. No tools.

## Notes

- A reply that failed at the HTTP level raises with the vendor, the
  status and the start of the body.
- Streaming is not part of this module. The HTTP builtins return whole
  bodies.
- The remote calls go through `REQ`, so `lib/req.jdb` must be reachable
  on the module path.

## Tests and demos

- `tests/jdlibs/llmapi_selftest.jdb` (both vendors stubbed by an in-process server)
- `jdb/demos/jdlibs/llm_facts.jdb` (a structured answer, checked with SCHEMA)
- `jdb/demos/jdlibs/llm_tools_demo.jdb` (a tool-calling loop, offline by default)
