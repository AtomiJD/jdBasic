# Desktop applications: forms, ImGui and the terminal

[Docs home](../README.md) · [Tour](../tour.md) · [Language reference](../languages.md) · [Module library](../../lib/README.md)

This guide is for building a tool with buttons, lists and dialogs. jdBasic has
three ways to do that. The guide shows how to pick one, then builds small
programs step by step with native Windows forms and with Dear ImGui, and ends
with a short look at the terminal UI.

Every example opens a window, so none of them has printed output to compare.
Run them with `jdbasic file.jdb` and close the window (or press Esc in the
ImGui examples) to end them.

## 1. Which toolkit for what

| | Windows forms (`FORM.*`) | Dear ImGui (`GUI.*`) | Terminal UI (`TUI.*`) |
|---|---|---|---|
| What you get | Real Win32 windows and controls | Widgets drawn inside a `SCREEN` graphics window | Widgets drawn in the console |
| Programming style | Event driven: create controls once, handlers run on events | Immediate mode: rebuild the whole UI every frame in a loop | Immediate mode, like ImGui |
| Platform | Windows only | Windows, Linux, macOS | Any console (TUI builds only) |
| Build flag | `FORMS` | `IMGUI` | `TUI` |
| Compiles with `-c` | Yes | Yes | Not covered here |
| Visual designer | VS Code designer for `.jdform` files | No | No |
| Good for | Classic business tools, dialogs, VB6 ports | Tools, dashboards, editors next to graphics or audio | Tools that run over SSH or in a terminal |

Pick forms when the program should look and behave like any other Windows
program. Pick ImGui when you want live values, plots and sliders next to
graphics, or when the program must run on more than Windows. Pick the terminal
UI when there is no desktop.

## 2. Windows forms

The full reference is
[Native Windows Forms (FORM.*)](../languages.md#native-windows-forms-form).

### A window with a button

This is the example from the README. `FORM.CREATE` makes a window,
`FORM.BUTTON` puts a button on it and `FORM.RUN` shows the window and waits
until it is closed.

```basic
DIM frm = FORM.CREATE("Hello", 320, 200, "MAIN")
DIM btn = FORM.BUTTON(frm, "btnGo", "&Go", 110, 80, 100, 28)

SUB BTNGO_CLICK(e)
    MSGBOX("Hello from jdBasic.", 64, "Hello")
ENDSUB

FORM.RUN(frm)
```

### How events are named

Every control has a name, the second argument of `FORM.BUTTON`, `FORM.TEXTBOX`
and the others. The form's name is the fourth argument of `FORM.CREATE`. An
event handler is a `SUB` with one parameter whose name is the control name, an
underscore and the event name, in capitals: `BTNGO_CLICK`, `MAIN_LOAD`,
`TXTNAME_CHANGE`. It is bound automatically, no `ON` statement is needed.

The parameter is an array; `e[0]` is a map with the control's `"name"` and
event data such as `"text"` and `"index"` (list click) or `"width"` and
`"height"` (form resize).

The common events are `_LOAD`, `_UNLOAD` and `_RESIZE` on a form, `_CLICK` on
buttons, check boxes and radio buttons, `_CHANGE` on text boxes and combo
boxes, `_CLICK` and `_DBLCLICK` on list boxes, and `_TICK` on timers. Keyboard,
mouse and focus events exist for every input control.

### A text box and a list

`FORM.GET` reads a property, `FORM.SET` writes one. The list box takes its
items as an array through `ITEMS`, and `ADDITEM` appends one.

```basic
DIM frm = FORM.CREATE("Shopping", 360, 260, "MAIN")
DIM lbl = FORM.LABEL(frm, "lblItem", "Item:", 12, 14, 60, 18)
DIM txt = FORM.TEXTBOX(frm, "txtItem", "", 70, 10, 180, 24)
DIM btn = FORM.BUTTON(frm, "btnAdd", "&Add", 260, 9, 88, 26)
DIM lst = FORM.LISTBOX(frm, "lstItems", 12, 44, 238, 200)

FORM.SET(lst, "ITEMS", ["Bread", "Milk"])
FORM.SET(btn, "ENABLED", FALSE)

' add the typed item to the list
SUB BTNADD_CLICK(e)
    DIM item AS STRING = FORM.GET(txt, "TEXT")
    IF item = "" THEN RETURN
    FORM.SET(lst, "ADDITEM", item)
    FORM.SET(txt, "TEXT", "")
    FORM.SET(txt, "FOCUS")
ENDSUB

' enable the button only while there is text
SUB TXTITEM_CHANGE(e)
    FORM.SET(btn, "ENABLED", FORM.GET(txt, "TEXT") <> "")
ENDSUB

SUB LSTITEMS_CLICK(e)
    PRINT "Selected "; e[0]{"index"}; ": "; e[0]{"text"}
ENDSUB

FORM.RUN(frm)
```

To read the selected list entry outside an event, use `FORM.GET(lst, "SELINDEX")` (`-1` when
nothing is selected) and `FORM.GET(lst, "SELTEXT")`.

### Message boxes and dialogs

`MSGBOX(text$, flags, title$)` takes the Win32 flag values (`4` Yes/No, `32`
question icon, `48` warning, `64` information) and returns the button: `1` OK,
`2` Cancel, `6` Yes, `7` No. `INPUTBOX$` asks for a line of text,
`COLORDIALOG` returns a colour as `$RRGGBB` and `FONTDIALOG$` returns
`"face,size,bold,italic"`. On Cancel they return `""` (`COLORDIALOG`: `-1`).

```basic
DIM frm = FORM.CREATE("Dialogs", 300, 170, "MAIN")
DIM lbl = FORM.LABEL(frm, "lblName", "Hello", 12, 12, 276, 40)
DIM btnName = FORM.BUTTON(frm, "btnName", "&Name...", 12, 70, 88, 26)
DIM btnColor = FORM.BUTTON(frm, "btnColor", "&Colour...", 106, 70, 88, 26)

SUB BTNNAME_CLICK(e)
    DIM answer AS STRING = INPUTBOX$("Your name:", "Dialogs", "World")
    IF answer <> "" THEN FORM.SET(lbl, "TEXT", "Hello, " + answer)
ENDSUB

SUB BTNCOLOR_CLICK(e)
    DIM rgb AS INTEGER = COLORDIALOG($FFFFFF)
    IF rgb >= 0 THEN FORM.SET(frm, "BACKCOLOR", rgb)
ENDSUB

' ask before closing; setting cancel keeps the window open
SUB MAIN_UNLOAD(e)
    IF MSGBOX("Quit the program?", 4 + 32, "Dialogs") = 7 THEN e[0]{"cancel"} = TRUE
ENDSUB

FORM.RUN(frm)
```

### Menus, files and a status bar

`FORM.MENU` takes the whole menu bar as an array of maps. An item with a
`name` fires `NAME_CLICK` like a button; `"-"` is a separator and `key` adds a
keyboard shortcut. `FILEOPEN$` and `FILESAVE$` show the standard file dialogs
with a filter in the form `"Text files|*.txt|All files|*.*"`.

```basic
DIM frm = FORM.CREATE("Notes", 480, 320, "MAIN")
DIM txt = FORM.TEXTBOX(frm, "txtBody", "", 0, 0, 480, 296, TRUE)
DIM sbar = FORM.STATUSBAR(frm, "sbMain")

FORM.MENU(frm, [ _
    { "text": "&File", "items": [ _
        { "name": "mnuOpen", "text": "&Open...", "key": "Ctrl+O" }, _
        { "name": "mnuSave", "text": "&Save as...", "key": "Ctrl+S" }, _
        "-", _
        { "name": "mnuExit", "text": "E&xit" } _
    ]} _
])

SUB MNUOPEN_CLICK(e)
    DIM path AS STRING = FILEOPEN$("Text files|*.txt|All files|*.*", "Open a note")
    IF path = "" THEN RETURN
    FORM.SET(txt, "TEXT", TXTREADER$(path))
    FORM.SET(sbar, "TEXT", path)
ENDSUB

SUB MNUSAVE_CLICK(e)
    DIM path AS STRING = FILESAVE$("Text files|*.txt", "Save the note", "note.txt")
    IF path = "" THEN RETURN
    TXTWRITER path, FORM.GET(txt, "TEXT")
    FORM.SET(sbar, "TEXT", "Saved " + path)
ENDSUB

SUB MNUEXIT_CLICK(e)
    FORM.CLOSE(frm)
ENDSUB

' keep the text box filling the window above the status bar
SUB MAIN_RESIZE(e)
    FORM.SET(txt, "WIDTH", e[0]{"width"})
    FORM.SET(txt, "HEIGHT", e[0]{"height"} - FORM.GET(sbar, "HEIGHT"))
ENDSUB

FORM.SET(sbar, "TEXT", "Ready")
FORM.RUN(frm)
```

The last argument `TRUE` of `FORM.TEXTBOX` makes it multi-line. Toolbars
(`FORM.TOOLBAR`), context menus (`FORM.POPUP`), list views, tree views, tabs
and MDI windows work the same way; see the reference.

### A loop instead of FORM.RUN

`FORM.RUN` blocks until every form is closed. A program that does its own
work in a loop shows the form with `FORM.SHOW` and calls `FORM.DOEVENTS()`
once per pass. It handles pending window messages and returns `TRUE` while
any form is open.

```basic
DIM frm = FORM.CREATE("Counter", 260, 120, "MAIN")
DIM lbl = FORM.LABEL(frm, "lblCount", "0", 20, 20, 200, 20)
DIM bar = FORM.PROGRESS(frm, "prgWork", 20, 50, 220, 20)
DIM count AS INTEGER = 0

FORM.SHOW(frm)
WHILE FORM.DOEVENTS() AND count < 100
    count = count + 1
    FORM.SET(lbl, "TEXT", "Step " + STR$(count))
    FORM.SET(bar, "VALUE", count)
    SLEEP 30
WEND
IF count = 100 THEN FORM.CLOSE(frm)
PRINT "Stopped at step "; count
```

### Layout in a file: .jdform and the designer

A `.jdform` file holds the layout as JSON, and `FORM.LOAD` builds the form
from it. Handlers bind by name the same way. Save this as `hello.jdform`:

```json
{
  "version": 1,
  "form": { "name": "MAIN", "title": "Hello", "width": 320, "height": 140 },
  "controls": [
    { "type": "TEXTBOX", "name": "txtName", "text": "World", "x": 12, "y": 12, "w": 200, "h": 24 },
    { "type": "BUTTON",  "name": "btnGreet", "text": "&Greet", "x": 220, "y": 11, "w": 88, "h": 26 },
    { "type": "LABEL",   "name": "lblOut", "text": "", "x": 12, "y": 50, "w": 296, "h": 20 }
  ]
}
```

and this next to it as the code behind. `FORM.FIND` looks up a control by
name.

```basic
DIM frm = FORM.LOAD("hello.jdform")

SUB BTNGREET_CLICK(e)
    DIM name AS STRING = FORM.GET(FORM.FIND(frm, "txtName"), "TEXT")
    FORM.SET(FORM.FIND(frm, "lblOut"), "TEXT", "Hello, " + name)
ENDSUB

FORM.RUN(frm)
```

The VS Code extension opens a `.jdform` in a visual
[form designer](../../vscode_extension/vscode_readme.md#8-form-designer): drag
controls from the toolbox, move them and edit their properties. The file
format is described under
[The .jdform file format](../languages.md#the-jdform-file-format).

### Compile to an .exe

A forms program compiles like any other jdBasic program: `jdbasic -c notes.jdb`
writes `notes.exe` and copies `jdbrt.dll` next to it. The native compiler is always STRICT
and EXPLICIT, so every variable needs a `DIM`, as in the examples above. Both
the compiler and `jdbrt.dll` must be built with the `FORMS` flag.

### Forms demos

In [jdb/demos/forms](../../jdb/demos/forms/):
[forms_demo.jdb](../../jdb/demos/forms/forms_demo.jdb) is a task list built
in code, [tasklist.jdb](../../jdb/demos/forms/tasklist.jdb) is the same
program with a `.jdform`, [gallery.jdb](../../jdb/demos/forms/gallery.jdb)
shows every control, [mdi_demo.jdb](../../jdb/demos/forms/mdi_demo.jdb) has an
MDI frame with menu, toolbar and status bar, and
[context_demo.jdb](../../jdb/demos/forms/context_demo.jdb) adds a context
menu and a question before closing.

## 3. Dear ImGui

The full reference is [ImGui Functions](../languages.md#imgui-functions).

### The frame loop

ImGui has no controls that live on their own. Each pass of the loop clears
the screen, describes every window and widget again, and calls `SCREENFLIP`
to draw the frame. A widget function returns the new value (or `TRUE` for a
button that was clicked in this frame), and you assign it back to your
variable.

```basic
SCREEN 640, 400, "ImGui", 1

DIM name$ = "World"
DIM volume = 50.0
DIM clicks = 0
DIM key$ = ""

DO
    CLS
    GUI.BEGIN "Settings", 20, 20, 360, 200
        GUI.TEXT "Hello, " + name$
        name$ = GUI.INPUT("Name", name$)
        volume = GUI.SLIDER("Volume", volume, 0, 100)
        IF GUI.BUTTON("Click me") THEN clicks = clicks + 1
        GUI.SAME_LINE
        GUI.TEXT "Clicks: " + STR$(clicks)
    GUI.END
    SCREENFLIP
    SLEEP 10
    key$ = INKEY$()
LOOP UNTIL key$ = CHR$(27)
PRINT name$; " "; volume
```

`GUI.BEGIN` takes a title and an optional position and size; every
`GUI.BEGIN` needs a `GUI.END`. Press Esc to leave the loop. The values are
ordinary variables, so after the loop they hold what the user entered.

### Combo box, table and plot

`GUI.COMBO` returns the selected index. A table is opened with
`GUI.BEGIN_TABLE`, columns are declared with `GUI.TABLE_SETUP_COLUMN`, and
each row starts with `GUI.TABLE_NEXT_ROW`. `GUI.PLOT_LINES` draws an array of
numbers as a line chart.

```basic
SCREEN 800, 500, "Dashboard", 1
GUI.THEME "DARK"

DIM cities = ["Berlin", "Paris", "Rome"]
DIM temps = [12.5, 15.0, 19.5]
DIM pick = 0
DIM samples = []
DIM t = 0
DIM key$ = ""
DIM i = 0

DO
    CLS
    t = t + 1
    samples = APPEND(samples, SIN(t / 10))
    IF LEN(samples) > 100 THEN samples = DROP(1, samples)

    GUI.BEGIN "Dashboard", 10, 10, 420, 460
        pick = CINT(GUI.COMBO("City", pick, cities))
        GUI.TEXT cities[pick] + ": " + STR$(temps[pick]) + " C"
        GUI.SEPARATOR

        IF GUI.BEGIN_TABLE("cities", 2, 0) THEN
            GUI.TABLE_SETUP_COLUMN "City"
            GUI.TABLE_SETUP_COLUMN "Temp"
            GUI.TABLE_HEADERS_ROW
            FOR i = 0 TO LEN(cities) - 1
                GUI.TABLE_NEXT_ROW
                GUI.TABLE_SET_COLUMN_INDEX 0
                GUI.TEXT cities[i]
                GUI.TABLE_SET_COLUMN_INDEX 1
                GUI.TEXT STR$(temps[i])
            NEXT i
            GUI.END_TABLE
        ENDIF

        GUI.PLOT_LINES "Signal", samples, "", -1.0, 1.0
    GUI.END

    SCREENFLIP
    SLEEP 16
    key$ = INKEY$()
LOOP UNTIL key$ = CHR$(27)
```

Both ImGui examples also compile with `jdbasic -c`. The `CINT` around
`GUI.COMBO` and the `50.0` for the slider value keep the types fixed, which
the native compiler requires.

Menus, tabs, popups, tree nodes, colour pickers and themes are in the
reference. Inside a loop that creates several widgets with the same label,
wrap each one in `GUI.PUSH_ID` and `GUI.POP_ID`.

### ImGui demos

In [jdb/demos/gui](../../jdb/demos/gui/):
[gui_v1.jdb](../../jdb/demos/gui/gui_v1.jdb) is a short first window,
[gui_full.jdb](../../jdb/demos/gui/gui_full.jdb) tours the widgets with plots,
[spreadsheet.jdb](../../jdb/demos/gui/spreadsheet.jdb) is built on an ImGui
table and [app_master.jdb](../../jdb/demos/gui/app_master.jdb) is the
sequencer studio. [fx_rack.jdb](../../jdb/demos/audio/fx_rack.jdb) is an
effects rack with oscilloscope and spectrum.

## 4. Terminal UI

`TUI.*` follows the ImGui model in the console, built on the FTXUI library.
It is only in builds made with the `TUI` build flag; the standard Windows
build does not include it. Widgets take the current value and return the new
one:

```basic
DIM volume AS DOUBLE = 0.5
DO
    TUI.BEGIN "App"
    TUI.TEXT "Hello"
    IF TUI.BUTTON("OK") THEN TUI.EXIT
    volume = TUI.SLIDER("Volume", volume, 0.0, 1.0)
    TUI.END
    TUI.RENDER
    SLEEP 16
LOOP UNTIL TUI.QUIT()
```

Ctrl+Q ends the loop as well. `TUI.RENDER_HEADLESS$` returns a frame as plain
text, for tests. The full list is under
[Terminal UI (TUI.*)](../languages.md#terminal-ui-tui), and the suites in
[tests/tui](../../tests/tui/) are runnable references.

## Where next

- [Native Windows Forms (FORM.*)](../languages.md#native-windows-forms-form):
  every control, property and event.
- [ImGui Functions](../languages.md#imgui-functions) and
  [Graphics and Multimedia Functions](../languages.md#graphics-and-multimedia-functions):
  widgets, and the `SCREEN` window they draw in.
- [VS Code extension](../../vscode_extension/vscode_readme.md): the form
  designer and the debugger.
- [Tour, section 10](../tour.md#10-from-script-to-exe): compiling a script to
  an `.exe`.
- [Demos](../../jdb/README.md): more forms and ImGui programs.
