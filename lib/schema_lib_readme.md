# SCHEMA - validation for maps, and JSON Schema from the same declaration

`lib/schema.jdb` checks a map against a declaration and answers with
the errors, dotted paths included, and a value with defaults filled in.
The same declaration produces the JSON Schema that the structured
output modes of the LLM APIs take, which is what `LLMAPI.JSON` uses.

Stands in for: pydantic, jsonschema.

## Quick start

```basic
IMPORT SCHEMA

DIM s = SCHEMA.NEW({ _
    "name": "string!", _
    "age": {"type": "integer", "min": 0, "max": 150}, _
    "role": {"type": "string", "enum": ["admin", "user"], "default": "user"}, _
    "tags": "string[]", _
    "address": {"type": "map", "fields": {"city": "string!", "zip": "string"}} _
})

DIM r = SCHEMA.CHECK(s, payload)
IF NOT r{"ok"} THEN PRINT JOIN(r{"errors"}, CHR$(10))
PRINT r{"value"}{"role"}          ' "user" when the payload had none
PRINT SCHEMA.JSONSCHEMA$(s)
```

## Declaring a field

The short form is a string: `"string"`, `"number"`, `"integer"`,
`"boolean"`, `"array"`, `"map"` or `"any"`, with a trailing `!` for
required and a trailing `[]` for an array of that type (`"string[]"`,
`"number[]!"`).

The map form takes:

| Key | Meaning |
|-----|---------|
| `type` | One of the seven type names; `any` when absent. |
| `required` | `TRUE` to complain when the key is missing. |
| `default` | Filled into the value when the key is missing. |
| `enum` | The allowed values. |
| `min` / `max` | The value for numbers, the length for strings and arrays. |
| `pattern` | A regular expression the whole string must match. |
| `items` | The declaration of an array's elements. |
| `fields` | The field map of a nested map, or a schema from `NEW`. |
| `description` | Carried into the JSON Schema. |

A schema built with `NEW` can stand as a field or as `items` directly.

## API

| Call | What it does |
|------|--------------|
| `NEW(fields)` | A schema from a map of declarations. |
| `CHECK(s, payload, [opts])` | `{ok, errors, value}`. `opts` may carry `coerce` and `strict`. |
| `VALID(s, payload)` | The boolean form of `CHECK`. |
| `JSONSCHEMA(s)` / `JSONSCHEMA$(s)` | The JSON Schema as a map or as text. |

`errors` is an array of strings such as `name: required`,
`tags[1]: expected string, got number`, `address.zip: shorter than 4`,
`role: not one of admin, user`.

## Modes

- **Coercion** (`{"coerce": TRUE}`): a numeric string becomes the
  number, `"true"` / `"false"` become booleans, a number becomes the
  string where a string was declared. What a form or a CSV delivers can
  be checked as it is.
- **Strict** (`{"strict": TRUE}`): a key the schema does not declare is
  an error. Without it such keys are ignored and do not reach `value`.

## Notes

- A missing key is detected with `TYPEOF(v) = "NONE"`, since comparing
  a value to `NONE` is true for other values too.
- The JSON Schema output uses `required`, `enum`, `default`,
  `minimum` / `maximum`, `minLength` / `maxLength`, `minItems` /
  `maxItems`, `pattern` and `additionalProperties: false`. OpenAI's
  strict mode takes a subset of that; `LLMAPI` reduces it before sending.

## Tests and demo

- `tests/jdlibs/schema_selftest.jdb`
- `jdb/demos/jdlibs/schema_demo.jdb` (an order declaration and six payloads)
- `jdb/demos/jdlibs/llm_facts.jdb` (the round trip to a language model)
