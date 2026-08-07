# repofilm

The history of a git repository, drawn as a galaxy that grows.

Every file is a body orbiting its directory. Directories orbit their parent.
A commit lights up the files it touched and draws a beam from the directory
hub to each of them. Nothing is ever removed, so the picture is the repository
accumulating.

It works on any git repository, not just this one.

## Run it

```
build/jdBasic.exe jdb/demos/graphics/repofilm/repofilm.jdb .
build/jdBasic.exe jdb/demos/graphics/repofilm/repofilm.jdb D:/usr/dev/jderg --speed 3
```

| Argument | Meaning |
|---|---|
| first bare argument | path to the repository, default `.` |
| `--speed n` | repository days per second of film, default 1.35 |
| `--frames dir` | write every frame as a PNG at a fixed 30 fps instead of playing live |

`ESC` or `q` ends it.

## Make a video

```
mkdir -p tmp/frames
build/jdBasic.exe jdb/demos/graphics/repofilm/repofilm.jdb . --frames tmp/frames
jdb/demos/graphics/repofilm/render.sh tmp/frames tmp/repofilm.mp4
```

The frame directory has to exist. Frames are written before the flip, so what
lands in the PNG is exactly what the window shows.

## How the layout is built

Positions come from the path alone, so two runs of the same repository produce
the same picture and a video can be re-rendered without everything jumping.

Children of a directory are placed by phyllotaxis: the i-th child sits at angle
`i * 137.5 degrees` and radius `spread * sqrt(i + 1)`. That is the sunflower
packing, and it fills a disc evenly without a physics step. The spread of each
directory is `cluster_radius(depth) / sqrt(children + 1)`, which keeps a
directory holding 400 files inside the same circle as one holding 4.

Subdirectories take the low slots and end up in the core of their cluster,
files take the rest and scatter outward.

## What it reads

One call:

```
git log --reverse --pretty=format:%x01%at%x09%s --numstat
```

For this repository that is 292 KB and 7795 lines, parsed in well under a
second. Commit lines are marked with `CHR$(1)`, the rest are `added`, `deleted`,
`path`. Binary files report `-` and are counted as a small fixed change.
Renames arriving as `dir/{old => new}/file` are folded onto the new path, so a
renamed file keeps its identity instead of appearing twice.

Merge commits contribute nothing under `--numstat` and are skipped.

## Colours

Green is jdBasic, blue is the C++ runtime, orange is data, purple is web,
grey is documentation, pink is assets, yellow is build scripts. Radius grows
with the logarithm of the lines a file has accumulated, capped so one
generated file cannot swallow the frame.
