# jdBrush

Paint and screenshot annotation on native Windows forms, in place of Paint
and the Snipping Tool. No account, no cloud, no AI; a saved picture carries
no metadata. Needs a FORMS build.

```
jdbasic jdbrush.jdb [picture]
jdbasic -c jdbrush.jdb            (jdbrush.jdb.props gives the .exe its icon)
jdbrush --register | --unregister (Open with and Default apps)
```

## Starting a picture

- **New Screenshot** (Ctrl+Shift+N): the window steps aside, the whole
  screen is taken, and the Crop tool is ready to cut out the part to keep.
- **New from Clipboard** (Ctrl+Shift+V): a picture copied anywhere else,
  a screenshot taken with Print Screen or Win+Shift+S included.
- **Open** (Ctrl+O), Open Recent, a picture dropped on the window, or the
  path on the command line.
- **New** (Ctrl+N): an empty white page of 1280 x 800.

## Tools

Pick one on the left or press its letter while the picture has the focus.

| Tool | Key | What a drag does |
|---|---|---|
| Pen | P | draws where the mouse goes |
| Highlighter | H | a wide see-through stroke over text |
| Line | L | a straight line from one end to the other |
| Arrow | A | a line with its head where the drag ends |
| Rectangle, Ellipse | R, E | outlined, or filled with **Fill shapes** |
| Text | T | a click asks for the text and places it there |
| Pixelate | X | the region in coarse blocks, unreadable |
| Blur | B | the region softened |
| Crop | C | keeps the region, drops the rest |

The colour comes from the swatches or **More...**, the width from the
**Width** box; the text size and the highlighter follow the width. Esc
drops a drag that is under way.

The wheel zooms at the pointer, a drag with the right button moves a
zoomed picture, Ctrl+0 fits it to the window and Ctrl+1 shows it at 100%.

## Image and Edit

- **Undo** (Ctrl+Z) and **Redo** (Ctrl+Y) go back up to 50 steps, a crop or
  a resize included.
- **Resize** (Ctrl+W) to a new width, the height following; **Turn Right**
  and **Turn Left** (Ctrl+R, Ctrl+L).
- **Copy Picture** (Ctrl+C) puts the picture on the clipboard for a mail, a
  chat or a document.

## Saving and printing

**Save** (Ctrl+S) and **Save As** write PNG, JPEG or BMP, chosen by the
extension. Nothing but the pixels goes in: no camera, date, position or
name, even when the picture came from a phone. **Print** (Ctrl+P) puts the
picture on the page as large as it fits, with its name on top.

## Like every jd tool

- The window opens where and as large as it was closed.
- **Dark Mode** and the **Status Bar** (View menu), the colour, the width,
  Fill shapes and the tool are kept for the next start.
- Settings live in `HKEY_CURRENT_USER\Software\jdBrush`, the recent list in
  `%APPDATA%\jdBrush\recent.txt`.
- **Help > Set as Default App** registers jdBrush for its picture types
  through `lib/appreg.jdb` and opens Settings on its Default apps page.

Drawing goes through the PICTURE drawing builtins of FORMS (`FORM.DRAW`,
`FORM.PREVIEW`, `FORM.PIXELS`, `FORM.HISTORY`, `FORM.SAVEIMAGE`). The icon
is drawn in `jdbrush_icon.svg` and made into `jdbrush.ico` by
`tools/make_icon.jdb`.
