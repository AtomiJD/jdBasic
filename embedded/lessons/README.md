# The lessons, on a board

The programs from the Train jdBasic video lessons, in the form a board
runs them. They sit in a `lessons` folder on every board's flash store:

```
> RUN lessons/lesson01_hello_a
> TYPE lessons/readme.txt
```

`readme.txt` is the index a board shows, forty columns wide.

## What is the same, what differs

Lessons 01 to 08 and 10 to 12 are the files from `jdb/tutorials/tv/`
unchanged. Four lessons have a board edition:

| lesson | board edition |
|---|---|
| 09 graphics | draws 320 by 240 on the board's own screen between `GFX.CONSOLE 0` and `GFX.CONSOLE 1`, and shows each picture for three seconds |
| 13 http_json | calls `WIFI.AUTO()` first, so `wifi.txt` on the store (ssid, then password) is what joins the network |
| 14 native | runs the typed benchmark interpreted and names the desktop commands, `jdbasic -c` and `jdbasic --pcode`; a board has no compiler |
| 10 modules | unchanged, `mathx.jdb` sits beside it in the folder and IMPORT finds it there |

Lesson 07 asks for input at the console. Lesson 08 writes `hello.txt` and
`todo.txt` on the store.

## Putting them on a board

Over the serial line, from the prompt: `MD lessons`, then one
`RECV lessons/name bytes` per file with the file's bytes after it. The
board answers `#` per 256 bytes stored. Any terminal program that sends
a file raw does; the `RECV` paragraph in `embedded/pico/README.md` has
the details.

## Where the lessons run

| | PicoCalc | Fruit Jam | ES3C28P |
|---|---|---|---|
| 01 to 08, 10 to 12, 14 | yes | yes | yes |
| 09 graphics | yes | yes | yes |
| 13 http_json | no radio | yes | yes |
