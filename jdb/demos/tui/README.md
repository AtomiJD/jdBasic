# jdtop - a terminal system monitor in pure jdBasic

A small `htop`-style system monitor written entirely in jdBasic, rendered
into the terminal with the `TUI.*` namespace (FTXUI). It reads `/proc`
directly and shells out to nothing.

![tabs: Overview / Processes / Net-Disk]

## What it shows

- **Overview** - total CPU gauge, one bar per logical core, a memory gauge,
  load average and uptime, and a scrolling braille graph of CPU history.
- **Processes** - the top processes by resident memory (RSS), refreshed live.
- **Net / Disk** - receive/transmit throughput and disk read/write throughput,
  each with an auto-scaling braille graph. Rates are derived from a real
  `TICK()` time delta, so they stay correct even when a frame takes longer.

The title bar tint tracks CPU load: `cool` below 50%, `gold` from 50%,
`warm` from 85%.

## How the data is gathered

Everything comes from the kernel's `/proc` filesystem, parsed with plain
string ops:

- CPU: jiffy deltas from `/proc/stat` (aggregate line plus `cpuN` per core)
- Memory: `MemTotal` / `MemAvailable` from `/proc/meminfo`
- Processes: `/proc/<pid>/statm` (RSS pages) and `/proc/<pid>/comm`
- Network: `/proc/net/dev` (skips `lo`)
- Disk: `/proc/diskstats`, whole physical disks only (partitions filtered out)

## Build and run

Build jdBasic with the `TUI` flag (which pulls in FTXUI), then run the script.

```
TUI=1 GFX=1 IMGUI=1 SQLITE=1 ./build.sh
./build/jdbasic jdb/demos/tui/jdtop.jdb
```

Keys: `1` / `2` / `3` (or Left / Right) switch tabs, `q` or `Ctrl+Q` quits.

## Poke it

To watch the graphs move and the theme change, generate some load from a
second terminal:

- CPU: `yes > /dev/null` (stop with `Ctrl+C`)
- Disk: `dd if=/dev/zero of=/tmp/blob bs=1M count=2000 oflag=direct` then `rm /tmp/blob`
- Network: a large `curl` download, or `ping -f` a host you own

It is Linux-only: the `/proc` layout it parses is Linux-specific.
