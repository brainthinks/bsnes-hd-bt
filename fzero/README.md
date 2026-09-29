# The fzero-rs recorder

The branch `fzero-trace-recorder` of this fork adds a measuring instrument to
bsnes. It records what the machine does while a game runs, and
[fzero-rs](https://github.com/brainthinks/fzero-rs) uses those recordings to
check its native port of F-Zero, frame by frame. fzero-rs is MIT-licensed and
holds no code from here. Every change this branch makes to bsnes lives in this
repository: as commits, and as the copies in [patches/](patches/).

Every switch below is an environment variable and is inert unless it is set.
A run without them is the emulator as it was.

## What it adds

**Recording** (`.fztr` files, fzero-rs's own format):

| switch | records |
|---|---|
| `BSNES_TRACE_RAM`, `_RAM_AT`, `_RAM_PAIRED`, `_RAM_AFTER` | work RAM, taken at the end of each video frame, or when the processor reaches a given address (`_AT`), or at a routine's entry and exit (`_PAIRED`); `_AFTER` skips frames first |
| `BSNES_TRACE_VRAM`, `BSNES_TRACE_SRAM` | video memory and battery RAM beside it |
| `BSNES_TRACE_REGS`, `_REGS_SEED_CHECK` | the register file the game writes |
| `BSNES_TRACE_DMA` | DMA and HDMA transfers |
| `BSNES_TRACE_APU` | the audio processor, the sound chip and their timing |
| `BSNES_TRACE_PORTS` | the conversation through the four audio ports |
| `BSNES_TRACE_TIMING`, `_TIMING_AT` | where in the frame instructions and stores fall |
| `BSNES_TRACE_EXEC`, `_EXEC_FRAMES`, `_EXEC_BRACKET` | which instructions ran |
| `BSNES_WATCH_READ`, `_READS_BY`, `BSNES_WATCH_WRITE` | who reads or writes an address |

**Driving and dumping:**

| switch | does |
|---|---|
| `BSNES_SCRIPT_FILE` | pads, one entry per frame, from a file (two-frame lead) |
| `BSNES_POKE`, `BSNES_POKE_AT` | write a byte at a chosen frame |
| `BSNES_SAVE_STATE_AT` | take a save state at a chosen frame |
| `BSNES_FRAME_HASHES` | a hash of every picture |
| `BSNES_DUMP_SAMPLES`, `_VOICES`, `_STAGES`, `_CONSOLE`, `_M7` | the sound output, voice state, pipeline stages, the console's own boot ROM, Mode 7 state |

**Live reference.** `BSNES_LIVE=<socket>` runs the emulator in lockstep with
fzero-rs's compare mode (`FZERO_COMPARE=bsnes`). The protocol is in
[LIVE.md](LIVE.md).

fzero-rs's documentation says what each recording holds and how it is read;
the switch names and the `.fztr` format are that project's design.

## Building

As upstream bsnes on Linux:

    make -C bsnes -j8

The binary is `bsnes/out/bsnes`. fzero-rs looks for it at
`~/projects/bsnes-hd-bt/bsnes/out/bsnes`, or wherever `BSNES` (for compare
mode, `FZERO_BSNES`) points.

fzero-rs pins the recorder it trusts by SHA-256. It rebuilds the recorder only
under its `tools/recorder_rebuild_control.sh` (record, build, record again,
compare). Each rebuild, and its new hash, is recorded in the commit that made
it, and in [LIVE.md](LIVE.md) "The build" for the live mode.

## Also here

- [patches/](patches/): each recorder change as a patch, with the commit it
  came from ([patches/README.md](patches/README.md)).
- [tests/](tests/): `test_recorder.cpp`, the paired-entry trigger test, which
  includes this source and so lives here.

## Licence

This is bsnes, and GPL-3.0 like it (see `../LICENSE.txt` and
`../GPLv3.txt`). bsnes was originally developed by byuu and is now a group project;
the HD work follows DerKoun's bsnes-hd. See `../CREDITS.md` and the top-level
`README.md`. The additions on
this branch are under the same licence.
