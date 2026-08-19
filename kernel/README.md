# jdBasic on bare metal

A small operating system written in jdBasic. It boots without anything
underneath it: no libc, no C runtime, no host OS. Screen driver, keyboard,
editor, RAM disk, an interpreter and a JIT that emits x86-64 are all jdBasic
source compiled with `--target=kernel`.

## Two languages, and they are not the same

This trips people up, so it comes first.

**jdBasic** is what the OS itself is written in: `repl.jdb`, compiled on the
host into a freestanding object. You never type jdBasic once the machine has
booted.

**The language inside the OS** is a small one of its own, in the spirit of
lallang. It has integers, one namespace of variables, `if`, `while`, functions,
and direct access to memory and ports. It is what you type at the prompt and
what you write in the editor.

## Building and booting

Two steps, because the compiler runs on Windows and the linker and emulator run
under WSL.

```sh
# once: a compiler that knows the kernel target
build.bat HTTP GFX IMGUI NATIVEC MCPSERVER KERNEL

# Windows side: jdBasic source to a freestanding ELF object
build/jdBasic.exe --target=kernel -o kernel/repl.o kernel/repl.jdb

# WSL side: link the image and boot it
cd /mnt/d/usr/dev/cc/kernel
./build_kernel.sh repl
./run.sh repl
```

`./run.sh -s repl` boots headless with the output on the terminal instead of a
window. `./run.sh -l` lists the images that are built.

## At the prompt

```
> ? 6*7
42
> ?? 6*7
42   [40 bytes of x86]
```

`?` interprets the rest of the line. `??` compiles it to machine code, runs it,
and reports the result together with how many bytes it emitted.

Anything else on a line is a statement: `x = 5` assigns, `if` and `while` do
what they look like, `;` separates several statements on one line.

A line whose first character is an uppercase letter is a command:

| Command | Effect |
|---|---|
| `DIR` | list the programs on the RAM disk |
| `SAVE name` | store the editor buffer under a name |
| `LOAD name` | read a program back into the editor buffer |
| `RUN [name]` | interpret the buffer, or load a program and interpret it |
| `LIST` | print the buffer |
| `NEW` | empty the buffer |
| `COMP` | compile the whole buffer to machine code |
| `CALL` | run what `COMP` produced |
| `CLS` | clear the screen |
| `HELP` | the same table, shorter |

`F2` opens the editor. `ESC` at the prompt halts the machine.

## The editor

`F2` opens it on the current buffer, `ESC` goes back to the prompt.

| Key | Effect |
|---|---|
| arrows, `Home`, `End`, `PgUp`, `PgDn` | move |
| `Enter` | split the line |
| `Backspace` | delete left, joining lines at column 0 |
| `Delete` | delete right, joining the next line at the end |
| `Tab` | four spaces |
| `F5` | interpret the buffer, then wait for a key |
| `ESC` | back to the prompt |

The bottom two rows show line and column, and the key hints. A blue `~` marks
rows past the end of the buffer. Keywords are magenta, numbers yellow,
identifiers green, `'` comments grey.

Selection and the clipboard are not implemented.

## Writing and running a program

Write it in the editor, leave with `ESC`, then pick a path:

```
> COMP
compiled 244 bytes
> CALL
55
```

or, for the interpreter, `F5` inside the editor or `RUN` at the prompt.

`SAVE name` keeps it on the RAM disk, which survives until the machine is
switched off. Sixteen slots.

## The language

The core both paths share:

```
x = 12                     assignment, one global namespace
? expr                     print (interpreter only)
if cond { ... }
while cond { ... }
a ; b                      statement separator
+ - * / %                  arithmetic, integer division
== <> < <= > >=            comparison, answering 1 or 0
( )                        grouping
- x                        negation
' anything                 comment to end of line
```

There is no `else`. Write a second `if`.

### What only the interpreter has

`?` to print. That is the only way to get a number onto the screen without
writing to video memory yourself.

### What only the compiled path has

Functions, and the hardware:

```
func name(a, b) { ... return expr ... }
```

Up to 16 functions of at most 4 parameters. Recursion works, including a
function calling one defined later, and two calling each other. A name inside a
body that is not a parameter is the same global everything else uses, so a
recursive function cannot keep local state.

| Builtin | Meaning |
|---|---|
| `peekb(a)` `peekw(a)` `peek(a)` | read 8, 16 or 32 bits |
| `pokeb(a,v)` `pokew(a,v)` `poke(a,v)` | write 8, 16 or 32 bits |
| `inb(port)` | read a byte from an I/O port |
| `outb(port,v)` | write a byte to an I/O port |

Useful addresses: the text screen starts at 753664 (`$B8000`), two bytes a cell,
character plus attribute times 256. Row `r` column `c` is
`753664 + (r*80 + c)*2`. The keyboard controller's data port is 96 (`$60`).

### The consequence

A program that uses `func` only runs through `COMP` and `CALL`. `RUN` and `F5`
go through the interpreter, which does not know the word. A program that uses
`?` only runs through the interpreter, because the compiled path has no print.
Compiled programs put things on screen by writing to video memory, which is
what the game does.

Variables are shared between both. Set one with `?` at the prompt and compiled
code sees it; run a compiled loop and the interpreter sees what it left.

## The game

A paddle-and-ball game is on the RAM disk when the machine boots.

```
> LOAD pong
loaded pong
> COMP
compiled 3006 bytes
> CALL
```

`a` moves left, `d` moves right, `ESC` leaves and answers your score. Three
lives. The ball bounces off the top and the sides; missing it costs a life.

It is written in the language above, in the editor, and it drives the screen
and the keyboard itself: `pokew` for every character cell, `inb(96)` for the
keys. `LOAD pong` then `LIST` shows the source, and you can change it.

The frame delay is a counting loop of 35 million, which is roughly 35 ms on
real hardware. Under QEMU it is slower, because the emulator interprets every
instruction.

## Limits

- 64 variables, 200 lines in the editor, 16 disk slots, 16 functions.
- Integers only in compiled code; the interpreter carries fractions, so
  `? 10/4` gives 2.5 while `?? 10/4` gives 2.
- The RAM disk is RAM. Nothing survives a reboot except the game, which is
  baked into the image.
- No `else`, no arrays, no strings in the OS's own language.
