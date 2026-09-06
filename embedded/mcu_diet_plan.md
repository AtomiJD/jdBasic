# jdBasic on a microcontroller: what could go, and what it would buy

A thought experiment, measured before it is planned. Numbers from the
PicoCalc image of 2026-09-05 (`build/jdbasic_repl.elf`, arm-none-eabi
size and nm) and the ESP32 native tally of the same day.

## Where the image is

    text   789 KB     rodata  370 KB     bss (static RAM)  92 KB

Flash, by origin:

    225 KB   the CYW43 WiFi firmware blob (PicoCalc only; the Fruit Jam has none)
    293 KB   vm.cpp          56 KB parser.cpp   31 KB compiler.cpp
     35 KB   jdb_embed_api   11 KB lexer         10 KB pcode        23 KB sprites
    219 KB   libstdc++ locale facets (money_get, time_get, num_get, ctype, codecvt ...),
             of which 102 KB are the wchar_t twins nothing here ever uses
    154 KB   libstdc++ streams (basic_istream/ostream, streambuf, stringbuf, ios_base),
             overlapping with the facets above
     28 KB   std::regex proper
     33 KB   the C++ name demangler, dragged in by the verbose terminate handler
     44 KB   newlib printf, scanf, dtoa, strtod (8 KB of it the wide printf)
     18 KB   the 580 message strings ("Parse error at line", "expected ')'", ...)
      7 KB   the 496 builtin name strings

Static RAM, the big ones:

     37 KB   lwIP packet buffer pool
      8 KB   ram_heap (lwIP)         7.7 KB  the LCD row buffer (PicoCalc)
      4 KB   the prompt's history

Heap at the prompt, measured on the ESP32: the native registry costs
about 158 bytes per builtin (std::function, an unordered_map node, the
name twice, the slot vectors), 54 KB for 342 natives.

So the two things the question named come last. The message texts are
18 KB of flash and no RAM at all; the language features are a few
kilobytes each. What weighs is the C++ library the interpreter drags in
without meaning to, and the shape of the native table in RAM.

## The list, by what it buys

Savings are estimates until a build proves them; the first two need a
build each to separate their overlap.

### 1. std::regex out of the board build

`REGEX.MATCH`, `REGEX.REPLACE`, `REGEX.SPLIT` are the only users of
`<regex>`, and `std::regex` is what instantiates the locale machinery:
ctype, collate, the wchar_t twins. Behind a flag the three verbs go and
most of the 219 KB of facets with them.

- flash: -28 KB for regex itself, likely -150 to -200 KB with the facets
- RAM: a few hundred bytes
- complexity: low - one `#ifdef` around three registrations and the include
- loss: regex on the boards; a program that needs it fails at load with a clear name

### 2. Streams out of the interpreter core

`std::ostringstream` and `istringstream` sit in vm.cpp (7 sites),
jdb_embed_api.cpp (6) and compiler.cpp (1), mostly for number
formatting and parsing. Replaced by snprintf and strtod the stream
classes and their share of the facets go too.

- flash: -100 to -150 KB after 1, less without it (the facets stay for regex)
- RAM: the iostream init objects, a kilobyte or two
- complexity: medium - 14 sites, and every number must print exactly as
  before (the parity sweep is the judge)
- loss: none

### 3. The verbose terminate handler

libstdc++ prints a demangled type name when an exception escapes; that
is the 33 KB demangler plus its tables. Defining a plain
`__gnu_cxx::__verbose_terminate_handler` in the port overrides the
library's.

- flash: -33 KB
- RAM: 0
- complexity: trivial
- loss: an uncaught exception says "terminate" instead of naming its type

### 4. The native registry as a flash table

Today every builtin costs a std::function, an unordered_map node, and
its name in two places. A sorted table of {name, function pointer,
min, max} in flash, looked up by binary search, keeps the same
`register_native` interface for the ports and costs a few bytes a
builtin.

- flash: -20 to -30 KB (the std::function thunks)
- RAM: -45 to -50 KB at 342 natives
- complexity: medium to high - 869 registrations use lambdas, most of
  them captureless (a function pointer), some capturing `this` (they
  need a context argument); a cheaper first step drops only the
  unordered_map and the duplicate name copies for about -20 KB RAM
- loss: none

### 5. Fewer builtins on the boards

The vm.cpp registrations that are desktop conveniences: `OS.*` (9),
`PATH.*` (5), `CODEC.*` (4), `CLIPBOARD.*` (2), `THREAD.*` (2),
`DEBUG.*` (2), `JDB.*` (3), `OUTPUT.*` (3), the `VB*` constants, and
about twenty more of that kind. Say 50.

- flash: -20 to -40 KB
- RAM: -8 KB at today's registry, -0.3 KB after 4
- complexity: low - `#ifndef JDB_MCU` around the registrations
- loss: each one is a difference between desktop and board; p-code
  files that use one are refused at load by name, which is the right
  failure

### 6. Reactive variables

`REACT`, `UNREACT` and the parser's formula recording.

- flash: -3 to -5 KB
- RAM: nothing unless used
- complexity: low for the natives, medium for the parser (the recording
  mode is woven into the token window)
- verdict: not worth a difference between the two builds

### 7. A smaller APL

The array verbs (`IOTA`, `RESHAPE`, `GRADE`, `REDUCE`, `SCAN`, `OUTER`,
`CONVOLVE`, `TRANSPOSE`, the numerics) are about 60 natives; the
vectorised operators live inside VM::run and stay in any case.

- flash: -40 to -60 KB for the verbs
- RAM: -10 KB at today's registry, -0.4 KB after 4
- complexity: medium - the verbs separate cleanly, the operators do not
- loss: the part of the language the article is about
- verdict: keep; if anything, gate the exotic ones (`OUTER`, `CONVOLVE`,
  the matrix numerics) and keep the everyday set

### 8. Shorter messages

580 strings, 18 KB. Terse codes would bring them to perhaps 6 KB.

- flash: -10 to -12 KB
- RAM: 0
- complexity: medium - 580 sites, and the test suites match message text
- verdict: no; the messages are what makes the board a computer rather
  than a thing that says ERROR 7

### 9. Smaller odds and ends

- `sprites.cpp` behind a flag where no demo uses it: -23 KB flash, low
- lwIP `PBUF_POOL_SIZE` halved in lwipopts.h: -18 KB RAM, low, costs throughput
- `keywords()` as a sorted static array instead of a map built at boot:
  -3 KB RAM, -3 KB flash, low
- newlib-nano printf with float support: -15 to -20 KB flash, medium,
  and formatting must stay identical

## In order

1, then a build, then 3, then 2, then a build; those three are pure
library weight and lose nothing. Then 4 and 5 for the RAM. 9 as it
suits.

Expected: the PicoCalc image from 1.25 MB to about 0.85 MB (0.6 MB
without the WiFi blob), which matters for a part with 4 MB of flash; and
about 70 KB more heap at the prompt from 4, 5 and the pool, which is
what decides whether a 20 KB program loads as source on a board without
PSRAM.

## RAM first

The flash list above is not the RAM list. What a board has left at the
prompt, and what a program costs while it loads, come from other places.
PicoCalc, 520 KB of SRAM, 211 KB free at the prompt:

    128 KB   the main stack, fixed in memmap_bigstack.ld (the ESP32: 96 KB task stack)
     92 KB   static data: lwIP pool 37, ram_heap 8, LCD row buffer 7.7, history 4, the rest small
    ~40 KB   the VM at rest, most of it the native registry (158 B a builtin)
    ~50 KB   stdio, USB, LittleFS caches, the radio's state, fragmentation

And while a program loads (bbs.jdb, 8 KB of source): the peak is 148 KB
above the resting heap, nearly all of it the syntax tree; after
compiling, the chunk keeps 20 KB. A 20 KB source fails on the PicoCalc
with 211 KB free, which is fragmentation as much as size.

In order of what it buys:

    A  main stack 128 KB -> 64 KB (ESP32 96 -> 64)      +64 KB    low     measure the high-water mark first; the parser recurses
    B  native registry as a flash table                 +45 KB    medium-high; the cheap first step (drop the map, one name copy) +20 KB
    C  lwIP PBUF_POOL_SIZE halved                        +18 KB    low     costs throughput on the radio
    D  the transient arena on every board                 0 KB    low-medium; a heap block taken at load and dropped after,
                                                                  no fragmentation: the practical difference between "20 KB loads" and bad_alloc
    E  compile one top-level unit at a time, drop its AST  -60..-70 % of the load peak   high; FUNC hoisting and forward
                                                                  references need a first pass over signatures
    F  Expr node diet, the way Stmt was done               -20..-30 % of the AST   medium
    G  small statics: keywords() table 3, history 2, row buffer 4, LittleFS cache   +10 KB   low
    H  desktop-only builtins out                           +8 KB before B, nothing after   low
    I  packed numeric arrays (8 B an element instead of 16)  halves every array   high; touches every array path in VM and runtime
    J  p-code from the desktop                              the load peak becomes the chunk size   done; a workflow, not a change

A, C, G and H are an afternoon and about 100 KB. B is the one real
piece of work with a sure gain. D decides more programs than its number
suggests. E and I are the big ones and the risky ones; E only after the
gate has a test for every construct that crosses a unit boundary.

## Done and measured, 2026-09-06

A, C, G and H, each behind a build knob with the old value as default:

    JDB_LEAN          desktop-only builtins and std::regex out    (build_pico.sh: fat to keep them)
    JDB_STACK_KB      the main stack                              stack=64
    JDB_MAX_FRAMES    deepest call nesting, ~130 B of stack each  frames=256
    JDB_HIST_N/LEN    the prompt's history                        hist=8 histlen=128
    JDB_PBUF_POOL     lwIP packet buffers                         pbuf=12
    FJ_ARENA_MB       the Fruit Jam's load arena                  arena=2
    SYS.STACK         [size, deepest use so far], both boards, the stack painted at boot

The keyword table moved into flash (a sorted array, binary search) on
every build; the desktop keeps its map for the tools.

PicoCalc, free at the prompt:

    211568   before
    220688   lean, keywords in flash, everything else at the old values     +9 KB
    307680   stack=64 hist=8 histlen=128 pbuf=12 frames=256                 +96 KB

The deepest the stack has gone in anything run so far is 29.6 KB
(recursion 200 deep, nested expressions, the demos); 600 levels stop
with "Call stack overflow (max 256 frames)" and the prompt answers.
Image: 2487808 -> 2370560 bytes of uf2, most of it the 25 builtins.

ESP32 (ES3C28P, interpreter in PSRAM), internal RAM free at the prompt:

    189255   before
    193775   lean, keywords in flash, everything else at the old values     +4.5 KB (and +6 KB of PSRAM)
    229615   stack=64 hist=8 histlen=128 frames=256                         +40 KB

Stack peak 30.9 KB of 64 with the same probes; the clock runs, 198 KB
left with the radio up. Image: 1897648 -> 1827040 bytes.

Still open from the RAM list: B (the registry as a flash table), D (the
load arena on every board), E, F, I.

The small values are the defaults since 2026-09-06 on all three boards.
Fruit Jam, free at the prompt: 88632 -> 166208 (395 natives, from 420).

### B, done 2026-09-06: names in flash

Measured first: the registry held 22389 bytes on the PicoCalc and 20607
on the ESP32 for about 400 builtins, 57 bytes each, not the 158 the
plan assumed (the boards never had the map). A table of function
pointers would have meant rewriting 870 registrations for the
remaining std::function, so the cut went elsewhere:

- a builtin registered with a literal keeps the pointer into flash; only
  a name built at run time by a host is copied (`register_native(const
  char*, ...)` next to the std::string overloads)
- the growth room of the slot tables goes back once every builtin is in
- `jdb_no_vectorize`, 629 names in an unordered_set built on the heap
  at first use, is a sorted table in flash with a binary search, the
  same way the keywords went

    registry bytes      PicoCalc 22389 -> 13068     ESP32 20607 -> 11814
    heap in use at the prompt, PicoCalc: 67104 -> 23124
    free at the prompt: PicoCalc 307680 -> 351720, ESP32 PSRAM 8121408 -> 8163176

The std::function per builtin (16 bytes) is what remains; the flash
table would take another 5 KB and is not worth the rewrite.
