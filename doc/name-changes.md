# The name changes of jdBasic 1.2

The 1.2 refresh of October 2026 makes the language consistent in five places.
A program written for an earlier build runs as it is, with one exception
(`MAT4.ROTATE`, below): the old names and forms keep working, and `--lint`, `-c` and the
MCP tool `jdb_check` name every use together with its replacement.
`tools/jdb-fix.jdb` rewrites a file or a whole tree.

```text
jdbasic tools/jdb-fix.jdb --dry-run --keys myproject     # list the changes
jdbasic tools/jdb-fix.jdb --keys myproject               # write them
jdbasic --lint myproject/main.jdb                         # nothing old is left
```

## 1. A name ends in `$` exactly when it answers a string

| Old name | New name | Why |
|---|---|---|
| `STR`, `CSTR`, `TOSTR` | `STR$` | answers a string |
| `CHR`, `LEFT`, `RIGHT`, `MID`, `TRIM`, `UCASE`, `LCASE` | the `$` form | answers a string |
| `FORMAT_DATE` | `FORMAT_DATE$` | answers a string |
| `REGEX.REPLACE` | `REGEX.REPLACE$` | answers a string |
| `OS.GETOS`, `SYS.DF`, `SOUND.STATS`, `GFX.POLLEVENT` | the `$` form | answer a string |
| `GUI.INPUT`, `GUI.INPUT_MULTILINE`, `TUI.INPUT`, `TUI.WAIT_EVENT` | the `$` form | answer a string |
| `AI.CHAT`, `AI.CHAT_RAW`, `AI.CHAT_STREAM`, `AI.DETOKENIZE`, `AI.RAG_QUERY`, `AI.RAG_QUERY_STREAM`, `AI.TOOL_CHAT` | the `$` form | answer a string |
| `DIR$` | `FILE.LIST` | answers an array |
| `INSTR$` | `INSTR` | answers a number |
| `JSON.PARSE$` | `JSON.PARSE` | answers a map or an array |
| `TILED.LAYERS$` | `TILED.LAYERS` | answers an array |
| `HTTP.GET_ASYNC$`, `HTTP.POST_ASYNC$`, `HTTP.PUT_ASYNC$` | without the `$` | answer a task id |

`TYPEOF` and `JOIN` keep their names, and so do the date functions (`NOW`,
`CDATE`, `DATEADD`, ...), which answer a date, not a string. `JDB.ALIASES()`
returns the whole table.

**One behaviour changes with the names:** `LEFT`, `RIGHT` and `MID` are now
the same functions as `LEFT$`, `RIGHT$` and `MID$`. A number passed as the
text is turned into its string form, and `MID` with a start past the end
answers an empty string instead of raising an error. A `TRY` around `MID` that
expected the error no longer catches one.

`REGEX_REPLACE$(text$, pattern$, replacement$)` takes the text first, unlike
`REGEX.REPLACE$(pattern$, text$, replacement$)`. It is not an alias and stays
as it is.

## 2. Maps from builtins use lower-case keys

Every map a builtin returns now spells its keys in lower case:

| Builtin | Keys |
|---|---|
| HTTP server request | `path`, `method`, `body`, `headers`, `params` |
| `OS.EXEC` | `output`, `exit_code` |
| `SYS.MEMORY` | `total`, `free` |
| `SVD`, `QR`, `EIG` | `u`, `s`, `v`, `q`, `r`, `eigenvalues`, `eigenvectors` |
| `GFX.DISPLAYS` | `index`, `name`, `x`, `y`, `w`, `h`, `primary` |
| `SOUND.CLOCK` | `cycle`, `t`, `cps` |
| NODE messages, calls, events, peers, `TOPIC.LIST`, `TOPIC.STATS` | `data`, `from`, `topic`, `seq`, `t`, `method`, `arg`, `error`, `event`, `addr`, `name`, `host`, ... |

```basic
FUNC Echo(request)
    RETURN request{"body"}            ' was request{"BODY"}
ENDFUNC

DIM r = OS.EXEC("git status")
IF r{"exit_code"} = 0 THEN PRINT r{"output"}
```

For one release a read of the old upper-case key on such a map still finds
the value, and the first one prints a warning. A map the program builds keeps
its keys exactly as written, so `own{"BODY"}` and `own{"body"}` stay two
different keys. A copy of a builtin map is a map the program built: read the
new key from it. `jdb-fix --keys` rewrites the keys that only builtin maps use
(`BODY`, `HEADERS`, `PARAMS`, `METHOD`, `PATH`, `OUTPUT`, `EXIT_CODE` and a
few more); a key as common as `NAME` or `DATA` it leaves for you.

The NODE protocol between machines keeps its keys, so a node on this build and
a node on an earlier one still talk to each other.

## 3. Every handler is a funcref

```basic
HTTP.SERVER.ON_GET "/", Home@           ' was "Home"
NODE.SERVE "add", AddHandler@           ' was "AddHandler"
ON "KEYDOWN" CALL OnKey@                ' was CALL OnKey
```

The string forms still work; `--lint` names each one.

## 4. Two new exits

`EXITSUB` leaves a `SUB` (it is the same statement as `EXITFUNC`).
`EXITSWITCH` leaves the innermost `SWITCH`, also from a loop inside a `CASE`.
`EXITFOR`, `EXITDO` and `CONTINUE...` from inside a `SWITCH` no longer leave a
value behind on the VM stack.

## 5. Math in radians, graphics in degrees

`SIN`, `COS`, `TAN`, `ATAN2` and the other math builtins take and answer
radians. `SPRITE.ROTATE`, `GFX.DRAWIMAGE_EX`, `CIRCLE_SECTOR`, `TURTLE.*`,
`MAT4.PERSPECTIVE` and the hue of `HSV` take degrees. `RADIANS(deg)` and
`DEGREES(rad)` convert, element-wise on arrays.

**This is the one change that is not compatible:** `MAT4.ROTATE` now takes
degrees, like the other graphics calls. Code that passes radians to it turns
by a fraction of a degree. Call `MAT4.ROTATE_RAD` instead, which takes the
radians `MAT4.ROTATE` took before, or pass `DEGREES(angle)`. `jdb-fix` cannot
tell which unit a number is in, so it leaves these calls alone.

## Boards

The firmware for the RP2350 and ESP32 boards keeps the old names: its table
of builtins is fixed at build time, and it does not rewrite old names. Write
board programs with the names the board's `HELP` lists.
