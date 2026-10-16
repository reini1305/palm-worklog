# WorkLog

Palm OS 4 work journal (runs on Palm OS 3.5+).

- **Day view**: begin/end of work, meetings on the left, to-dos on the right.
  - Tap *Begin*/*End* to pick a time (menu: *Begin Now* `/B`, *End Now* `/E`).
  - Tap a meeting or to-do to edit it; tap empty space in a column to add one.
  - Tap a to-do's checkbox to toggle it.
  - Arrows or hardware up/down change the day; tap the date to pick one.
- **Month view**: begin, end and duration per day, with a monthly total.
  Tap a row to open that day.

## Build

Native (macOS, Apple Silicon via Rosetta 2), toolchain in `~/opt/palm`:

    make                # -> WorkLog.prc

Toolchain setup (once):
- prc-tools-remix: `tools/build-prc-tools-macos.sh` from a checkout of
  [savaughn/prc-tools-remix](https://github.com/savaughn/prc-tools-remix),
  then copy `dist$HOME/opt/palm/*` to `~/opt/palm/`. Builds x86_64 binaries
  with `clang -arch x86_64` (macOS 26 CLT can't run clang under Rosetta).
- PilRC: build from [jichu4n/pilrc](https://github.com/jichu4n/pilrc) with
  `clang -std=gnu89 -DSIZEOF_INT=4 -DSIZEOF_LONG=8 -DSIZEOF_CHAR_P=8 *.c`
  (all .c in the top dir except win.c/xwin.c) into `~/opt/palm/bin`.
- SDKs: [palm-os-sdk](https://github.com/jichu4n/palm-os-sdk) contents into
  `~/opt/palm/palmdev`, then `~/opt/palm/bin/palmdev-prep -d sdk-4`.

Or in Docker: `./build.sh`.

## Run

Drag `WorkLog.prc` onto [CloudpilotEmu](https://cloudpilot-emu.github.io)
running a Palm OS 4 ROM (m515 or m500), or HotSync it to a device.

## Data

Database `WorkLogDB-WkJn` (creator `WkJn`, type `DATA`) has the backup bit set,
so HotSync copies it to the desktop. Each record is:

    UInt16 date   days since 1904-01-01
    UInt8  kind   0 = day (begin/end), 1 = meeting, 2 = to-do
    UInt8  flags  bit 0 = to-do done
    Int16  t1     minutes after midnight: day begin / meeting start
    Int16  t2     day end
    char   text[] NUL-terminated

Records are sorted by (date, kind, t1). All values are big-endian.
