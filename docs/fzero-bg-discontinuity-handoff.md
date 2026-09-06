# Widescreen horizon panoramas: findings and fix

Supersedes the earlier handoff of the same name. That document reasoned from
reading the code only; everything below was measured by building bsnes, driving
F-Zero from a scripted controller, and dumping VRAM and frames.

## What the report was

In widescreen, F-Zero's horizon tiles break at the edges of the 4:3 frame. The
break moves and appears on either side as the car turns. bsnes-hd has it too.

## How the games store the horizon

**F-Zero** keeps the horizon as consecutive **256-pixel windows** on separate
tilemap row bands of a 64x32 (512x256) mode-1 background, seven tile rows per
window. `voffset` picks the window, `hoffset` sweeps 0..255 inside it, and the
tilemap's **second 32x32 half holds, at the same rows, the window 256 pixels
further on**. That is what makes the hardware's ordinary 512-pixel wrap draw a
continuous picture: past `hoffset` 255 the fetch crosses into the second half,
which is already the next window.

**Super Mario Kart** does the same thing on different layers: mode 0, BG3 and
BG4 on a 64x64 tilemap, four-row windows. Its horizon does not scroll by
`hoffset` alone -- `voffset` picks the window exactly as F-Zero's does.

Measured driving a full 360 degrees in both directions (F-Zero `Quick/Slot 3`,
Mario Kart `Quick/Slot 2`):

| Game | Layer | mode | map | windows | window height | panorama length |
|---|---|---|---|---|---|---|
| F-Zero | BG1 (near) | 1 | 64x32 | 4 | 7 rows (56 px) | **896 px** |
| F-Zero | BG2 (far)  | 1 | 64x32 | 3 | 7 rows (56 px) | 768 px |
| Mario Kart | BG3 | 0 | 64x64 | 4 | 4 rows (32 px) | 1024 px |
| Mario Kart | BG4 | 0 | 64x64 | 2 | 4 rows (32 px) | 512 px |

Mario Kart's BG2 is a static far sky at `hoffset` 0 whose second tilemap half is
empty; no grid fits it and it keeps ordinary wrapping.

BG1's loop is **not** a whole number of windows. Its fourth window only ever
uses `hoffset` 0..127, and `half0` window 3 columns 16..31 are byte-identical to
`half0` window 0 columns 0..15 — the last window's second half repeats the
first window's beginning. The step back from window 0 is therefore 128 pixels,
not 256.

Both facts are checked directly against VRAM at several headings, and the band
joins were rendered and inspected.

## What was wrong

`panoramaRowOffset` looked for the neighbouring window by matching the visible
row band against every other row band. That works while both halves agree, and
it silently returns "no answer" when they do not — which is exactly what happens
at the window where the loop closes, because a game that streams the panorama
leaves the wrap target half-written. F-Zero's BG1 got no correction at all in
two of its four windows, so those columns fell back to plain wrapping and drew
the wrong window.

In Mario Kart the same gap showed as trees popping in and out of the widescreen
extension: nothing had ever claimed those layers, so the extension drew whatever
stale columns the game had left in the far half of the tilemap.

Exhaustively, over every window and every one of the 256 scroll positions:

| Layer | old | new |
|---|---|---|
| BG1 | 127 discontinuous columns | **0** |
| BG2 | 0 | **0** |

Two claims in the old handoff did not survive measurement, and the code no
longer carries the machinery they implied:

* *"The correction is decided per 8-pixel tile."* Both boundaries sit at
  `x = -hscroll (mod 8)`, so they are always tile-aligned; this was not the bug.
  The walk is split by column anyway, because the rule below is per column.
* *"Wrap conditions test the tilemap edge, not the extension."* Testing the
  tilemap edge is right: inside 0..hmask the hardware is already correct.

## What the fix does

`HdToolkit::panoramaGrid` derives the window grid — base row, rows per window,
window count, and the length of the loop — from the tilemap, and declines unless
at least two windows confirm it against the actual tile words. It is not a
search: away from the wrap, the second half of row r repeats the first half of
row r + height, so each candidate spacing leaves a run of rows with that
property, and the run's start and length give the base, the height and the
window count directly.

Every ordinary 8x8 background on a 64-tile-wide tilemap is offered to it, on any
layer and in any non-hires, non-offset-per-tile mode; a 32-tile-wide map has no
second half to hold the next window. Nothing is keyed to a game or a ROM: a
background that is not laid out this way gets `count == 0` and is rendered
exactly as before, and each scanline resolves its own window, so a visible band
that crosses from one window into the next stays correct.

`HdToolkit::panoramaAdjust` then addresses each **widescreen** column through
the panorama rather than through the tilemap: it works out which window holds
that part of the picture and reads it from the first half. Reading through the
second half would usually agree, but it is where a streaming game keeps scratch,
so the first half — what the game itself puts on screen — is the only source
trusted for the extension.

`renderBackground` groups the columns into runs that share an adjustment and
walks each run separately. Columns **inside** the 256-pixel frame are never
touched: whatever the hardware's own wrapping draws there is the picture.

## Verified

* 4:3 (`Widescreen: 0`): 76 of 76 sampled frames byte-identical to a build with
  the previous logic.
* Widescreen 16:9: only the extension changes — every changed column is at
  `renderX < 0` or `renderX >= 256`, in 20 of 152 sampled frames across two full
  360-degree sweeps.
* `tests/hd-ppu`: 149 passed, 0 failed, including a 4-window short loop.
* Mario Kart: 17 of 51 frames across a full turn change, every changed column in
  the extension, none inside the picture. The trees stop popping.
* Every other ROM to hand -- Pilotwings, Castlevania IV, Contra III, Axelay,
  FF6, Chrono Trigger, Yoshi's Island, Super Metroid (and its widescreen hacks),
  Super Mario World widescreen, Star Fox, Demon's Crest, Doom, Gradius III,
  Bahamut Lagoon, Skuljagger: byte-identical with and without the path. (Two
  games first appeared to differ; that was the auto-saved `.srm` diverging
  between consecutive runs, and they are identical when each run starts from the
  same save file.)

## Not settled

* F-Zero BG1's *right* extension only needs the wrap case when `hoffset >= 193` in the
  short window, which the game never reaches on this track. That path is
  implemented and unit-tested but has not been seen on screen.
* A band straddling the tilemap's own vertical wrap (`last < first`) still keeps
  ordinary wrapping. F-Zero never does this.
* Per-game configuration (eventually `.bso`) remains available as a fallback for
  a game whose layout cannot be derived; nothing needs it today.

## Instrumentation

`bsnes/emulator/hdtrace.hpp` — env-gated, no-ops unless set, to be purged with
the other debug hooks.

| Variable | Effect |
|---|---|
| `BSNES_LOAD_STATE="Quick/Slot 3"` | load a state once the game is up (pre-existing) |
| `BSNES_SCRIPT_INPUT="30:b,150:b+right"` | hold buttons from a frame number; `+` combines |
| `BSNES_QUIT_AFTER=910` | quit after N frames |
| `BSNES_TRACE_BG=<path>` | per-frame heading and per-line BG state |
| `BSNES_DUMP_VRAM_AT="150,300"` | tilemap dump at those frames, into the trace |
| `BSNES_DUMP_VRAM_BIN=<prefix>` | raw VRAM + CGRAM at the same frames |
| `BSNES_FRAME_DIR` + `BSNES_FRAME_AT` | frames as PPM |
| `BSNES_NO_PAN=1` | disable the panorama path, for A/B |
| `BSNES_HEADLESS=1` | skip presenting frames; PPU dumps still happen |
| `BSNES_TIME_FRAME=1` | per-frame wall-clock breakdown |

Driving F-Zero: `B` accelerates; steering only turns the car while it is moving.
`"30:b,150:b+right"` from `Quick/Slot 3` sweeps a full 360 degrees repeatedly.
Mario Kart: `Quick/Slot 2` is paused, so `"30:b,120:b+left"` resumes and turns.

`BSNES_HEADLESS=1` matters for measurement: on this machine `video.output()`
blocks for about a second per frame at HD Mode 7 scale 8, which has nothing to do
with the PPU and makes unattended runs unusable without it.
