# jdPix

A picture viewer on native Windows forms. It opens at once, steps through
the pictures of a folder and keeps nothing about you: no library, no sync,
no account, no cloud, no ads.

```
jdbasic jdpix.jdb [picture | folder]
jdbasic -c jdpix.jdb            (jdpix.jdb.props names the .exe)
```

## Looking

The picture opens fitted to the window; a picture smaller than the window
is shown at its own size. A phone photo stands the way it was taken.

| Do | How |
|---|---|
| Next / previous picture in the folder | Right / Left, Page Down / Page Up, Space |
| First / last picture | Home / End |
| Zoom at the pointer | Mouse wheel |
| Zoom in / out at the middle | Ctrl+Plus / Ctrl+Minus, or + / - |
| Fit to the window | Ctrl+0 |
| Actual size (100%) | Ctrl+1 |
| Fit, or 100% at the pointer | Double click |
| Pan | Drag while zoomed in |
| Open a picture or a folder | Ctrl+O, or drop it on the window |

The status bar shows the name, where it is in the folder, its size in
pixels and the zoom.

## What a picture gives away

View > Info (Ctrl+I) opens a panel beside the picture: size, file size,
dates, the camera, and whatever else the file holds, a GPS position and the
names of photographer, author or camera owner included, plus one line that
sums up what the picture gives away.

File > Save a Copy without Metadata (Ctrl+Shift+S) writes a copy with none
of it, the picture itself copied byte for byte. A photo keeps its
orientation, so it still stands upright.

## Changing

- **Turn Right / Turn Left** (Ctrl+R / Ctrl+L): a JPEG is turned by its
  EXIF orientation alone, so it loses no quality; other pictures are written
  turned.
- **Delete** (Del): the picture goes to the recycle bin, from where it can
  be put back, and the next one is shown.
- **Copy Picture** (Ctrl+C): onto the clipboard, transparency kept, for
  Paint, Word or a browser.

The window opens where it was closed, and the Info panel as it was left.

Built on the jdBasic `FORM.PICTURE` control (`SCALEMODE`, `ZOOM`, `SCROLLX`,
`MOUSEWHEEL`) and the `IMAGE.*`, `FILE.RECYCLE` and `CLIPBOARD.SETIMAGE`
builtins.
