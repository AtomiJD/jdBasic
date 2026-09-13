# PARSEC - parser combinators and PEG grammars

`lib/parsec.jdb` parses small languages without a hand-written lexer.
Combinators build a parser from literals, character sets, regular
expressions, sequences, choices, repetition, look-ahead and separated
lists; a PEG grammar text compiles into the same parsers. A parse answers
a tree of maps with type, text, line and column; actions compute values
on it bottom-up. A failed parse names the line, the column and everything
that was expected there.

Stands in for: pyparsing, lark, parsimonious.

## Quick start

```basic
IMPORT PARSEC

FUNC Num(tree)
    RETURN VAL(tree{"text"})
ENDFUNC

FUNC Chain(tree)
    DIM kids = tree{"children"}
    DIM first = kids[0]
    DIM acc = first{"value"}
    DIM k = 1
    DO WHILE k < LEN(kids)
        DIM op = kids[k]
        DIM rhs = kids[k + 1]
        IF op{"text"} = "+" THEN acc = acc + rhs{"value"} ELSE acc = acc - rhs{"value"}
        k = k + 2
    LOOP
    RETURN acc
ENDFUNC

DIM calc = PARSEC.GRAMMAR(TXTREADER$("sum.peg"))
PARSEC.ACTION(calc, "NUMBER", Num@)
PARSEC.ACTION(calc, "sum", Chain@)
DIM tree = PARSEC.PARSE(calc, "12 + 30 - 2")
PRINT tree{"value"}                          ' 40

IF NOT PARSEC.MATCHES(calc, "12 + - 2") THEN
    PRINT PARSEC.ERRCONTEXT$()                ' 12 + - 2
                                              '      ^
    PRINT PARSEC.ERROR$()
    ' PARSEC: line 1, column 6: expected NUMBER, found "-"
ENDIF
```

with `sum.peg`:

```
sum     <- NUMBER (OP NUMBER)*
NUMBER  <- [0-9]+
OP      <- "+" / "-"
_WS     <- [ \t]+
%ignore _WS
```

The same parser from combinators:

```basic
DIM number = PARSEC.TOKEN("NUMBER", PARSEC.MANY1(PARSEC.CHARSET("0-9")))
DIM op = PARSEC.TOKEN("OP", PARSEC.CHOICE([PARSEC.LIT("+"), PARSEC.LIT("-")]))
DIM sum = PARSEC.NAMED("sum", PARSEC.SEQUENCE([number, PARSEC.MANY(PARSEC.SEQUENCE([op, number]))]))
PARSEC.IGNORE(sum, PARSEC.CHARSET(" \t"))
```

## Grammars

| Notation | Means |
|----------|-------|
| `name <- e` | a rule; `name: e` and `name = e` work too |
| `e1 e2` | sequence |
| `e1 / e2`, `e1 \| e2` | ordered choice: the first that matches |
| `e*` `e+` `e?` | zero or more, one or more, optional |
| `&e` `!e` | look ahead: e matches here / does not, nothing consumed |
| `( e )` | grouping |
| `"text"` `'text'` | a literal; `"text"i` in any case |
| `[a-z_]` `[^\n]` | a character class, `^` negates |
| `/regex/` | a regular expression (ECMAScript, as REGEX.MATCH) |
| `.` | any character |
| `# ...` `// ...` | comments |
| `%ignore NAME` | skip that rule's text between the pieces of rules |
| `%start name` | the rule to parse with; the first rule by default |

Strings and classes know `\n`, `\t`, `\r`, `\xHH` and `\` before any other
character. Put spaces around a `/` that means choice: a `/` followed
directly by a character starts a regular expression.

Rule names decide what a rule leaves in the tree:

| Name | Node |
|------|------|
| `sum` | a node with the nodes of its pieces as children |
| `NUMBER` (upper case) | a token: a node with its text and no children; nothing is skipped inside it, and an error names it rather than its characters |
| `_items`, `_WS` | no node of its own; its children go to the rule that uses it |
| `?term` | stands for its child when it has exactly one |
| `!assign` | also keeps the literals it matches as nodes of type `""` |

Literals, classes, regular expressions and `.` in a rule leave no nodes
unless the rule is a `!rule`; name what you need as a token.

Example grammars with their demo: `jdb/demos/jdlibs/grammars/arith.peg`
(arithmetic with precedence, sign and right-associative power),
`json.peg` (RFC 8259) and `ini.peg` (sections, keys, comments, CRLF).

## API

### Parsing

| Call | What it does |
|------|--------------|
| `GRAMMAR(source$)` | Compiles a grammar; answers a handle. A fault in the grammar throws with its line. |
| `RULE(grammar, rule$)` | A handle that parses with another rule of the grammar, with its actions and %ignore. |
| `PARSE(handle, text$)` | The tree of the whole text; throws the error message when it does not match. |
| `MATCHES(handle, text$)` | Whether the whole text matches. |
| `ACTION(handle, type$, fn)` | `fn(node)` runs on every node of that type once its children are built; its answer becomes the node's `"value"`. |
| `IGNORE(handle, parser)` | Text the parser matches is skipped before every piece, as `%ignore`. |

A node is a map:

| Key | Holds |
|-----|-------|
| `type` | the rule or token, `""` for a kept literal |
| `text` | the text it matched |
| `line`, `column` | where it starts, from 1, counted in characters |
| `start`, `end` | character offsets, `end` exclusive |
| `children` | its child nodes |
| `value` | what its action answered, when it has one |

When the start rule leaves more than one node (an `_rule`), the tree is a
node of type `""` holding them.

### Errors

| Call | What it does |
|------|--------------|
| `ERROR$()` | `PARSEC: line L, column C: expected A, B or C, found "x"` |
| `ERRLINE()` / `ERRCOL()` | where the failure is |
| `EXPECTED()` | what was expected there, in the order it was tried |
| `ERRCONTEXT$()` | the line of the failure and a caret under the column |

The position is the farthest one any piece of the parser reached, which is
where the text stops making sense. A token is expected by its name, a
literal in quotes, a class in brackets, the end as `end of input`. Names of
`_rules` are left out while anything else was expected at the same place.

### Combinators

| Call | Matches |
|------|---------|
| `LIT(text$, [nocase])` | the text; with nocase (1) in any case |
| `CHARSET(spec$)` | one character of a class, written as between brackets: `"a-z0-9_"`, `"^,\n"` |
| `ANYCHAR()` | any character |
| `PATTERN(re$)` | a regular expression at the position |
| `SEQUENCE(parsers)` | each in turn |
| `CHOICE(parsers)` | the first that matches |
| `MANY(p)` / `MANY1(p)` / `OPT(p)` | zero or more, one or more, optional |
| `SEPBY(item, sep)` / `SEPBY1(item, sep)` | items with separators between them |
| `AHEAD(p)` / `NOTAHEAD(p)` | where p matches / does not, consuming nothing |
| `HIDE(p)` | p, leaving no nodes |
| `NAMED(type$, p)` | a node of that type over p's nodes |
| `TOKEN(type$, p)` | a token node with p's text |
| `DEFINE(name$, p)` / `REF(name$)` | a parser used before it is defined, for recursion |

Terminals from combinators (LIT, CHARSET, ANYCHAR, PATTERN) leave nodes of
type `""`; wrap them in HIDE or TOKEN to change that.

### Trees

| Call | What it does |
|------|--------------|
| `OUTLINE$(tree)` | the tree as indented lines, leaves with their text |
| `CHILDREN(tree, [type$])` | the children, only those of a type when given |
| `COLLECT(tree, type$)` | every node of a type in the tree, in text order |

## Notes

- Left recursion (`expr <- expr "+" term`) is reported when it is reached
  rather than looped on; write the repetition instead
  (`expr <- term ("+" term)*`) and fold the values in an action.
- There is no memo of earlier attempts, so a grammar that tries the same
  long rule again after every failed choice does that work again.
- Every level of nesting in the text takes call frames: the arithmetic
  grammar reads 40 levels of parentheses and JSON 60 levels of arrays
  interpreted, 20 and 40 compiled.
- Everything works compiled with `-c`. Inside an action start a sum at
  `0.0`, not `0`, when the values are not whole numbers.

Self test: `tests/jdlibs/parsec_selftest.jdb`.
Demo: `jdb/demos/jdlibs/parsec_demo.jdb`, with the grammars in
`jdb/demos/jdlibs/grammars/`.
