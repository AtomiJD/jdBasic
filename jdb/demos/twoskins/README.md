# Two skins, one program

The same task list twice: once as a Windows window with real Win32
controls, once as a page in a browser. Both are shells. Everything that
decides what a task is, when it counts as finished and how the list is
written to disk sits in one file that neither of them may bypass.

```
tasks.jdb        the list itself, no screen: OPEN ADD TOGGLE RENAME
                 REMOVE SWEEP ITEMS STATS LABEL$ FLUSH
tasks_win.jdb    Win32 controls, handlers bound by name
tasks_web.jdb    an HTTP server, the page below, a small JSON API
tasks_web.html   the page: asks for the list, posts every change back
```

Both shells run on the same `tasks.json`, so a task typed into the
window is there when the browser is opened, and the other way round.

## Running it

```
jdBasic tasks_win.jdb          a window (needs a FORMS build)
jdBasic -c tasks_win.jdb       the same window as an .exe
jdBasic tasks_web.jdb          http://localhost:8099
```

The web shell listens on every interface, so the phone on the same
network reaches it at the machine's address. Press a key in its console
window to stop it.

## What each shell is allowed to do

A shell reads controls, paints what the core hands it and calls the
core. It never touches the file and never decides anything:

```basic
' tasks_win.jdb
SUB BTNADD_CLICK(e)
    IF TASKS.ADD(gStore, FORM.GET(txtNew, "TEXT")) = 0 THEN ...
```

```basic
' tasks_web.jdb
FUNC HANDLEADD(request)
    TASKS.ADD(gStore, Field$(JSON.PARSE$(request{"BODY"}), "title"))
    RETURN Snapshot()
ENDFUNC
```

Both lines are the same call. That is the whole point: the choice
between a native window and a browser is a choice of shell, made per
deployment, not a rewrite.

## Where each one fits

The window is for a Windows desktop: no server, no port, no browser, and
it reaches the rest of the machine, COM automation into Excel and
Outlook included. The page is for everything else: another operating
system, a tablet, a phone, several people at once.

`tasks.json` is written at runtime and is not part of the repository.
