# VB6 migration path - plan for review

Draft for a decision. Nothing here is built yet. It covers the two parked
phases of the jdForms epic, P4 (VB6 language layer) and P5 (legacy import),
and it is written against a **measured corpus**, not against an idea of what
VB6 code looks like.

## 1. What the corpus actually says

741 `.frm`, 63 `.vbp`, 313 `.cls` under `D:\usr\dev\Backups\...\VBA 6.0`.
Counting `Begin <Type>` over 400 of the form files, ~14 700 control instances:

| Control | Count | jdForms today |
|---|---|---|
| **VirtualOffice.jdText** | **4 680** | nothing |
| VB.Label | 4 011 | FORM.LABEL |
| VB.CommandButton | 1 687 | FORM.BUTTON |
| VB.Frame | 1 055 | FORM.FRAME |
| VB.ComboBox | 664 | FORM.COMBO |
| VB.CheckBox | 464 | FORM.CHECKBOX |
| VB.Form | 399 | FORM.CREATE |
| VB.TextBox | 224 | FORM.TEXTBOX |
| VB.Data | 183 | nothing (data binding) |
| MSDBGrid.DBGrid | 177 | nothing (bound grid) |
| MSComctlLib.TabStrip | 177 | FORM.TABS |
| VirtualOffice.BaseBar | 136 | nothing |
| VirtualOffice.ctlPopUp | 102 | nothing |
| VB.OptionButton | 96 | FORM.RADIO |
| MSComctlLib.ImageList | 94 | nothing |
| VB.PictureBox | 90 | FORM.PICTURE |
| DHCCtl.DesignerHost | 84 | nothing |
| VB.ListBox | 67 | FORM.LISTBOX |
| MSComDlg.CommonDialog | 65 | the four dialog functions |
| VB.Line / Image / Timer | 161 | FORM.LINE / - / FORM.TIMER |
| VirtualOffice.jdGrid | 41 | nothing |
| MSComctlLib.TreeView / ProgressBar | 40 | FORM.TREEVIEW / FORM.PROGRESS |
| ActiveBar, WebBrowser, SysInfo, eControls | 52 | nothing |

**Roughly 62 % of control instances map onto something jdForms has. 38 % do
not, and one single control is 32 % of the total.**

### The one control that decides everything

`jdText` is not a text box. Its properties across those 4 680 instances:

| Property | On | What it means |
|---|---|---|
| Width/Top/Left/Height/TabIndex | 4 680 | ordinary layout |
| Name/Size/Weight/Italic/Underline/Strikethrough/Charset | 4 416 | **its own font** |
| **Index** | **4 364** | **control arrays** |
| InputFormat | 3 966 | input mask |
| **QueryField / Query** | 3 952 / 3 907 | **bound to a query field** |
| StatusHelp | 3 256 | status-bar hint |
| Caption / CaptionWidth | 2 287 / 1 420 | it draws its own label |
| Browser | 688 | lookup button |

So it is a **data-bound, masked, self-labelling field with its own font**. It
is a small framework, and it is the substance of every VirtualOffice mask.

### The uncomfortable conclusion

This corpus is not "VB6 applications". It is **VirtualOffice applications**,
and VirtualOffice is a data-binding framework on custom OCX. A generic VB6
importer pointed at it would convert the chrome - labels, buttons, frames -
and leave every field on the floor. The result would look like the form and
do nothing.

That is worth knowing **before** deciding, not after.

## 2. Three things stand between here and there

Independent of which target is chosen, these are in the way.

### 2.1 The property surface is too thin (blocking)

What `FORM.SET`/`FORM.GET` know today: `TEXT ENABLED VISIBLE CHECKED VALUE
MIN MAX X Y WIDTH HEIGHT FOCUS TOPMOST MAXIMIZED INTERVAL COLOR(*) PICTURE
ITEMS COLUMNS ADDITEM CLEAR SELINDEX SELNODE SELTEXT EXPAND COUNT NAME KIND
HWND`.

(*) `COLOR` works on `SHAPE` only.

Missing, and every one of them appears in the corpus:

| Missing | Why it matters |
|---|---|
| **ForeColor / BackColor** | on nearly every VB6 control; today only a SHAPE has a colour |
| **Font** (name, size, bold, italic) | one global system font for the whole app; the corpus sets a font on 4 416 controls |
| **Tag** | VB6's per-control scratch value, used constantly |
| **ToolTipText** | exists for toolbar buttons only |
| **Alignment** | labels and numeric fields |
| **MaxLength, PasswordChar, Locked** | **a password field is not expressible at all** |
| **TabIndex / TabStop** | order comes from creation order and cannot be changed |
| **MousePointer** | hourglass during a long operation |

A converter that cannot emit `BackColor` has to emit a comment instead, and
then every converted form looks broken. **This is on the critical path and it
is worth doing whether or not the migration happens** - it is the difference
between a toolbox you can click and one you can design with.

### 2.2 Control arrays (blocking for this corpus)

`Index` is set on 4 364 instances. In VB6 `jdText(0)..jdText(40)` share one
handler:

```vb
Private Sub jdText_Change(Index As Integer)
```

jdForms has no concept of it: names are unique and a handler takes no index.
Options, in order of preference:

1. **Name mangling plus an index in the event.** `jdText(3)` becomes control
   `JDTEXT__3`; the binder routes every `JDTEXT__n` to `JDTEXT_CHANGE` and
   puts `e[0]{"index"} = n`. No new runtime concept, converter-side only.
2. A real array type in the runtime. More faithful, much more work.
3. Refuse them and report. Kills 30 % of the corpus.

Option 1 is the one to plan for.

### 2.3 Data binding (out of scope, and must be said out loud)

`Query`/`QueryField` on ~3 900 fields, plus `VB.Data` 183 and `DBGrid` 177.
jdForms has nothing here and should not grow it as part of an importer. The
honest position: **converted forms come out unbound**, with the binding
recorded as a comment on each control, and wiring them up is application
work.

There is a second reason to leave it alone: `vo/` is already a hand-built
slim rebuild of VirtualOffice in jdBasic and has its own data layer. An
importer inventing a competing one would be work spent twice.

## 3. The plan

Four phases. Each has a gate that can only be answered with a number.

### P4a - Properties and design surface

Close 2.1. `FORECOLOR`, `BACKCOLOR`, `FONT` (a map: name, size, bold, italic,
underline), `TAG`, `TOOLTIP`, `ALIGN`, `MAXLENGTH`, `PASSWORD`, `LOCKED`,
`TABINDEX`, `TABSTOP`, `CURSOR`. Custom colours need `WM_CTLCOLOR*` handling
in the form WndProc and a per-control brush, which is also where per-control
fonts hang.

*Gate: a form built entirely from `FORM.SET` reproduces a screenshot of a VB6
form, colours and fonts included. No property in the list above is missing.*

Worth doing on its own merits. **Nothing else in this plan starts before it.**

### P5a - The form converter

`.frm` -> `.jdform` + a `.jdb` code-behind skeleton. Written in jdBasic, so
it is also a showcase.

- twips to logical units: `logical = twips / 15`
- `Begin VB.X` blocks, nested `Frame` membership, `Begin VB.Menu` trees
- control arrays by 2.2 option 1
- `.frx` companions detected and **reported**, never silently dropped
- everything unmappable emits a `' TODO VB6:` line carrying the original
  block verbatim, so nothing is lost, only marked

*Gate: over the 741 real `.frm` files - what fraction of control instances
converted, what fraction was marked, and how many files converted without a
single mark. Published as a table, re-runnable.*

### P5b - The code converter

`.bas`/`.cls`/form code-behind -> `.jdb`. The mechanical bulk is large and
dull: `Dim x As Integer`, `Set x =`, `End If`/`End Sub`, `&` concatenation,
`Private`/`Public`, `Option Explicit`.

The parts that are not mechanical, and where a converter earns its keep:

- **default properties**: `Text1 = "x"` means `Text1.Text = "x"`
- **`With` blocks**
- **`On Error Resume Next` / `GoTo`** - no equivalent; mark, do not fake
- **`Variant`** and implicit coercion
- **`Type`** (UDT), `Collection`, `Err`
- property access `Text1.BackColor` -> `FORM.SET(FORM.FIND(frm, "Text1"),
  "BACKCOLOR", ...)`, which is why P4a comes first

Event handlers are the one piece of luck: `Private Sub Command1_Click()`
already matches jdForms' `COMMAND1_CLICK` convention exactly.

*Gate: every converted file parses with `--lint`, and a stated percentage of
statements converted rather than marked, measured over the corpus.*

### P5c - The compat module

A jdBasic library for the VB runtime names: `Left$ Mid$ InStr Format$ CStr
Val IsNumeric Trim$ Replace Split Join Now DateDiff App.Path Err.Number`.
Cheap, pure jdBasic, no runtime work. Watch the traps: jdBasic's `DATEADD`
takes its arguments in a different order, `INSTR` is 0-based and returns -1,
`MID$` is 0-based.

*Gate: a compat smoke that runs the same expressions through both semantics
and asserts equality.*

## 4. What this is not

- **Not a VB6 emulator.** Nothing here runs a `.vbp`. It converts once and
  you maintain jdBasic afterwards.
- **Not OCX support.** `FORM.OCX` (#167) is a separate question and would
  drag the COM stack in behind it.
- **Not a data layer.**

## 5. The decision that comes first

The plan is different depending on the answer, so it should be answered
before anything starts.

**A. Migrate this codebase.** Then the corpus is the yardstick, and the work
is mostly *VirtualOffice-specific*: rebuild `jdText` as a jdForms control
(mask, caption, font, lookup), plus `jdGrid`, `BaseBar`, `ctlPopUp`, plus a
binding convention. Coverage goes from 62 % to ~94 % with one control. But it
overlaps with `vo/`, which is already being rebuilt by hand, and that overlap
has to be resolved rather than ignored.

**B. A generic VB6 importer as a product feature.** Then this corpus is the
wrong yardstick - it is dominated by one house framework - and the target is
a plain VB6 application using intrinsic controls. The work is P4a + P5a +
P5b + P5c and stops there. Smaller, cleaner, and it is the thing the blog
post implicitly promises.

**C. Neither, and P4a only.** Close the property gap because jdForms needs it
regardless, and leave the parked phases parked. Cheapest, and it removes the
most-felt limitation of the toolbox today.

My recommendation is **C now, B next, A only if the `vo/` rebuild is meant to
stop.** P4a is the common prefix of all three, it is useful on its own, and
after it we would know a lot more about how much a converter can actually
lean on the property surface.
