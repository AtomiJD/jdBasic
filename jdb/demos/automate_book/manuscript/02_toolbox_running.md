## Running a Program on a Schedule

An automation that you have to remember to start is half an automation.
Windows has a scheduler built in, the Task Scheduler, and the setup
wizard uses it: for every recipe you switch on it adds one task that
starts `jdbasic` with the recipe's program at the time you chose.

### What the wizard puts in the Task Scheduler

Open the Start menu and type *Task Scheduler*. On the left, under *Task
Scheduler Library*, you find a folder called *AutomateWork* with one
task per recipe, such as *E01 Downloads Butler*. Each task runs a
command like this one:

```
"C:\Users\mia\jdBasic\jdbasic.exe"
    "C:\Users\mia\Documents\AutomateWork\recipes\
     E01_downloads_butler\downloads_butler.jdb"
    --config "C:\Users\mia\Documents\AutomateWork\config\work.conf"
```

It is one line in the Task Scheduler; it is broken here to fit the page.
The `--config` part tells the recipe which settings file to read, so a
recipe started by the scheduler finds exactly the settings the wizard
wrote.

You can look at the same list from a command prompt:

```
schtasks /Query /TN "AutomateWork\E01 Downloads Butler"
```

and start a task right away, which is the quickest way to see whether
it works:

```
schtasks /Run /TN "AutomateWork\E01 Downloads Butler"
```

> **Watch out**
> A scheduled task runs only while you are logged in, and a laptop that
> sleeps at 18:30 misses the Downloads Butler that evening. The tasks
> the wizard creates do not catch up later; for the easy recipes that
> does no harm, since the next run does the work of both. For anything
> that must never be missed, Chapter 5 has a job server that catches up
> on its own.

### Changing a schedule

Run the wizard again, pick the recipe on the third page, change the
schedule and set up. The wizard replaces the old task with the new one.
To stop a recipe, untick *Use this recipe* and set up again, or remove
all tasks at once with the console wizard:

```
jdbasic wizard\setup_console.jdb --uninstall
```

Your work folder and your settings stay where they are, so switching a
recipe back on later costs one click.

### When a schedule is not enough

The Task Scheduler starts a program at a time. Some jobs need more: run
every five minutes but never twice at the same time, try again after a
failure, write down what happened. The SCHED library does that inside
jdBasic, and Chapter 5 builds a personal job server on it that runs all
your automations from one place. For the easy recipes, the Task
Scheduler is all you need.

## When Something Goes Wrong

Programs fail. A folder was renamed, a file is open in another program,
a setting has a typing mistake. jdBasic tells you what happened and
where; the trick is to read the message calmly.

### Reading an error message

Here is a small program with a mistake. The list has two entries, at the
positions 0 and 1, and the program asks for position 2:

```basic
DIM files = ["a.pdf", "b.pdf"]
PRINT files[2]
```

```text
Error #13: Array index out of bounds: 2 at line 2
```

The message has three parts. *Error #13* is the kind of error; the
number helps when you search the documentation. *Array index out of
bounds: 2* says what went wrong in plain words: there is no entry 2.
*at line 2* says where. Open the file, go to that line, and the mistake
is usually right there or one line above.

### Checking a program without running it

`jdbasic --lint file.jdb` reads a program and reports what it can find
without running a single line. That is useful before you schedule a
changed recipe:

```
jdbasic --lint downloads_butler.jdb
```

When everything is fine, it says `LINT: Parsed OK.` and lists what it
found. When a block is not closed, for example an `IF` without its
`ENDIF`, it reports the line where it reached the end of the file still
waiting, and words it as an `END` it expected:

```
LINT error: Parse error at line 4: expected 'END', got ''
```

The line number points to the end of the program, not to the `IF`. Look
upwards for the block that is missing its end.

### Dry runs

Every recipe of this book that changes files has a switch
`--dry-run`. With it, the recipe prints what it would do and does
nothing. Use it after every change of a setting:

```
jdbasic downloads_butler.jdb --dry-run
```

It is the same habit the recipes follow on the inside: first make a
plan, then show it, then carry it out.

### The doctor

When a scheduled recipe stops working and you do not know why, ask the
wizard:

```
jdbasic wizard\setup_console.jdb --doctor
```

It checks that the settings file exists, that `jdbasic` is where the
settings say, that every recipe you switched on is in the work folder,
and that its task is in the Task Scheduler. Every problem it finds is
one line, such as:

```
E01: no scheduled task AutomateWork\E01 Downloads Butler
```

Running the wizard again repairs most of what the doctor finds.

> **Try this**
> Keep a text file `problems.txt` in your work folder. Every time a
> recipe fails, write down the date, the message and what fixed it.
> After a few months it is the most useful page of this book.

## From Script to Program

A `.jdb` file needs `jdbasic` to run. jdBasic can also turn a program
into an `.exe` of its own, which runs on another computer without
jdBasic installed:

```
jdbasic -c downloads_butler.jdb
```

This writes `downloads_butler.exe` and the libraries it needs next to
it. Copy the folder to a colleague's computer and the program runs
there with a double click. The compiled program is also faster, which
matters for the large jobs of Chapter 5.

Compiling needs the release of jdBasic with the native compiler, and the
Microsoft C++ build tools on the computer that compiles (not on the one
that runs the result). The forms release this book starts with does not
contain the compiler; the professional edition of the book does. Every
recipe in this book was compiled and tested as an `.exe` as well, so
nothing changes when you switch.

> **Watch out**
> A program you hand to a colleague runs with that colleague's rights
> and on that colleague's files. Recipe X07 in Chapter 5 shows how to
> give a tool settings of its own, a version number and an easy way to
> update it, so that you do not become everybody's help desk.
