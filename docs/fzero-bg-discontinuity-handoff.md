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

Measured on `Quick/Slot 3`, driving a full 360 degrees in both directions:

| Layer | tilemap rows | windows | window height | panorama length |
|---|---|---|---|---|
| BG1 (near) | 4..31 | 4 | 7 rows (56 px) | **896 px** |
| BG2 (far)  | 11..31 | 3 | 7 rows (56 px) | **768 px** |

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

`HdToolkit::panoramaGrid` fits the window grid — base row, rows per window,
window count, and the length of the loop — to the tilemap, and declines unless
at least two windows confirm it against the actual tile words. Nothing is keyed
to a game or a ROM; a background that is not laid out this way gets `count == 0`
and is rendered exactly as before.

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
* Super Mario Kart, Pilotwings, Castlevania IV, Contra III, Axelay, FF6, Chrono
  Trigger, Yoshi's Island, Super Metroid, Star Fox, Demon's Crest and the
  widescreen-patched Metroid/SMW hacks: the grid never fits, so the path never
  engages and nothing changes.

## Not settled

* **Super Mario Kart's race horizon is a different mechanism**, not this one:
  BG1 on a 32x32 map with per-scanline HDMA `hoffset`/`voffset`, and BG2 static
  at `hoffset` 0 on a 64x64 map. Its widescreen horizon renders continuous
  today; if a seam is found there it needs its own investigation.
* BG1's *right* extension only needs the wrap case when `hoffset >= 193` in the
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

Driving F-Zero: `B` accelerates; steering only turns the car while it is moving.
`"30:b,150:b+right"` from `Quick/Slot 3` sweeps a full 360 degrees repeatedly.
