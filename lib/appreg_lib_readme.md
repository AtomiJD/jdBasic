# APPREG: an app under Open with and in Default apps

`lib/appreg.jdb` registers a program with Windows as an app that opens some
file types. It then shows under **Open with** in Explorer and in
**Settings > Apps > Default apps**, where the user can make it the default
for each type. Everything goes under `HKEY_CURRENT_USER`: no administrator
rights, nothing for the other users of the machine.

Stands in for: the registry part of an installer (Inno Setup's
`[Registry]` section, WiX `RegistryValue`).

## Quick start

```basic
IMPORT APPREG

DIM exts = [".jpg", ".jpeg", ".png"]
IF APPREG.REGISTER("jdPix", "jdPix.Picture", "A picture viewer", "jdPix Picture", exts) = 0 THEN
    APPREG.OPEN_SETTINGS("jdPix")       ' the user picks jdPix for each type
ENDIF

' and to take it back
DIM rc = APPREG.UNREGISTER("jdPix", "jdPix.Picture", exts)
```

jdPix and jdTexti answer `--register` and `--unregister` on the command
line and have both under Help.

## API

| Call | What it does |
|---|---|
| `REGISTER(app$, prog_id$, description$, type_name$, exts)` | Registers the running program as `app$` for the extensions in `exts` (`".jpg"`, with the dot). Writes the ProgID `prog_id$` with `type_name$`, the program's icon and its open command; the capabilities under `Software\<app$>\Capabilities`; the ProgID under each extension's `OpenWithProgids`; and the entry in `RegisteredApplications`. A compiled program also gets its own entry under `Applications\<name>.exe`. Answers the number of values that could not be written, 0 when all went in, -1 off Windows. |
| `UNREGISTER(app$, prog_id$, exts)` | Takes back what REGISTER wrote. The program's own settings under `Software\<app$>` stay. Answers 0, or -1 off Windows. |
| `OPEN_SETTINGS(app$)` | Opens Settings on the Default apps page of `app$`. |
| `COMMAND$()` | The command line Windows runs to open a file: `"<exe>" "%1"`, interpreted `"<jdBasic.exe>" "<script>" "%1"`. |

## Notes

- Windows does not let a program make itself the default. Since Windows 8
  the choice is stored with a hash only Settings and the Open with dialog
  write, so the user confirms it there; OPEN_SETTINGS opens the page.
- The registration names the program where it is when REGISTER runs. A
  program moved to another folder registers again from there.
- Run in the interpreter, the open command is jdBasic with the script and
  the icon is jdBasic's. Compile the program with `-c` for its own icon.
- After both calls Explorer is told that associations changed
  (`SHChangeNotify`), so Open with shows the change at once.

Self test: `tests/jdlibs/appreg_selftest.jdb`, registers a test app for two
extensions of its own, reads back every key Windows reads and checks that
UNREGISTER removes them and keeps the app's settings. 20 assertions
interpreted, 22 compiled.
