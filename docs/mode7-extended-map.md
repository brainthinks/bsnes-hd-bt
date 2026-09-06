# Mode 7 extended map

Working record for the widescreen Mode 7 map work: what it is for, what was
measured, what was decided and why, what exists now, and what is left. The
horizon-panorama work that preceded it is written up separately in
`fzero-bg-discontinuity-handoff.md`.

## The problem

In widescreen, F-Zero's track repeats into the extra columns: a second copy of
the road, with its own yellow markers, appears to the left and right. It is
visible on the title screen and throughout a race, and the boundary of the
repeated area moves as the car turns.

## What was measured

Everything here was measured on this branch, not inferred.

**It is the Mode 7 map wrap.** F-Zero sets M7SEL `repeat = 0` (wrap) on every
Mode 7 line. Painting every pixel whose sample falls outside the 1024x1024 map
shows large, irregular regions of the picture coming from the wrap, moving as
the car drives, and reaching well inside the 4:3 frame — 890 of 2700 sampled
points inside the normal 256-wide picture are already outside the map. The game
depends on the wrap for its own image; widescreen only breaks the guarantee its
designers had, that the *road* never wraps into view.

**The course is streamed.** Decoding the Mode 7 tilemap out of VRAM at two
points in one lap gives two completely different course sections — the
start/finish straight, then somewhere else entirely. Between sampled moments
17–33% of the 16384 tilemap entries are rewritten. Tile *pixel* data never
changes. So the track is far longer than one map; VRAM holds a window onto it.

**The hardware cannot hold more.** Mode 7 addressing is seven bits per axis
(`tileX = pixelX >> 3 & 127`). One map, fixed location, no second bank, no
paging register. It occupies the low half of VRAM interleaved — tilemap in the
low bytes of words $0000–$3FFF, 256 tiles of pixel data in the high bytes — and
F-Zero's sky and HUD occupy the upper half ($6000 tiles, $7800 map).

**Streamed history cannot be stitched.** Successive map states do not align
under any translation: the best shift matches no better than no shift at all
(87% vs 86%, 69% vs 69%, 75% vs 75%). The game re-authors the neighbourhood
around the car rather than scrolling a window through a larger world, so there
is no consistent coordinate frame to accumulate into. (Coarse correlation over
repetitive city tiles — indicative, not proof, but enough not to build on.)

**How much extra map would be needed.** Per scanline, the span of map
coordinates the visible floor reaches, in multiples of the hardware map
(F-Zero, save state 4, 21:9 / 96px extensions):

| scanline | map coords sampled | maps needed |
|---|---|---|
| 216 (near the car) | 539 … 941 | 0.92 |
| 132 | 519 … 1018 | 0.99 |
| 96 | 493 … 1117 | 1.09 |
| 60 | 390 … 1505 | 1.47 |
| 48 (topmost Mode 7 line) | 226 … 2131 | **2.08** |

A 2x map (256x256 tiles) covers the visible floor except the top scanline or
two; 4x is comfortable. The requirement is bounded because the game clips the
Mode 7 region to start below the horizon.

**Tile art is not the constraint.** F-Zero uses 60 of the 256 Mode 7 tile
slots, and the tile pixels are unchanged across a lap. A larger world costs
tilemap *entries* only: 128KB at 2x, 512KB at 4x, emulator-side.

## Decisions

**Extend, do not replace.** The map answers only where the sample has left the
hardware's 1024x1024 window — exactly where the hardware would wrap. Inside the
window VRAM always wins, so the live game keeps drawing itself and a stale or
wrong file cannot corrupt the picture. This is the same invariant the panorama
work used (never change what the hardware itself would draw).

**Strictly additive.** Past its own edge the map defers to the hardware instead
of wrapping at the extended boundary. An unauthored or short map therefore
renders *exactly* as it does today — verified frame-for-frame. Wrapping at the
extended edge was tried first and rejected: far samples wrapped back into the
authored area and reintroduced the phantom, just further out.

**Tile indices only.** Pixels keep coming from VRAM's 256 shared tiles. There
is no reason to duplicate tile art and every reason not to.

**Origin in whole tiles.** The window origin is a tile coordinate, so the
sub-tile offset (`pixelX & 7`) is unaffected and the palette fetch is unchanged.

**A file loader, not a ROM channel, for now.** The eventual mechanism has to be
a side channel the ROM hack writes, because the emulator cannot infer the world
layout (see above). A file gets the rendering half proven and lets the result be
seen before any 65816 exists.

## What exists now

- `bsnes/emulator/m7extmap.hpp` — `HdToolkit::Mode7ExtendedMap`: the store, the
  lookup, the file format, and a dump that writes the hardware's current map
  into the middle of a larger one. Self-contained, no bsnes dependencies, unit
  tested.
- Hooked into both CPU Mode 7 samplers (`mode7.cpp` 1x and `mode7hd.cpp` HD).
- Loaded once per run from `cacheMode7ExtendedMap()` in `background.cpp`.
- `tests/hd-ppu`: 200 passing, including load/validation, the window-precedence
  rule, the additive edge, and a dump round trip.

Debug hooks (in `hdtrace.hpp`, to be replaced by configuration):

| variable | effect |
|---|---|
| `BSNES_M7_EXTMAP=<file>` | render Mode 7 through the extended map |
| `BSNES_M7_EXTMAP_DUMP=<file>[,factor]` | write a starter file from the current hardware map (factor 2 or 4) |
| `BSNES_M7_EXTMAP_MARK=1` | tint pixels the extended map supplied |

Workflow: dump a starter file, confirm it renders identically (it will — only
the centre is authored and the centre is never consulted), paint the
surroundings, load it back.

File format, little-endian: `"M7XM"`, `u16` version 1, `u16` tilesW, `u16`
tilesH (128–512, multiple of 128), `u16` originX, `u16` originY (tile
coordinates of the hardware map's 0,0 within the extended one), then
`tilesW*tilesH` `u16` entries — a tile index, or `0xffff` for "not authored".

Verified: a starter file changes nothing across four sampled frames of a drive;
an authored surround replaces the phantom track where the hardware would have
wrapped, and `MARK` shows exactly which pixels came from it.

## What is left

1. **GPU sampler.** The GPU Mode 7 shader still reads the hardware's 128x128
   map from VRAM, so loading an extended map currently *disables* GPU
   supersampling and falls back to the CPU picture — which the hd-ppu skill
   rightly calls not the HD look. Teaching the shader a larger texture is the
   next piece of rendering work and is not hard; the texture upload and the
   sampler wrap are the only parts that change.
2. **The ROM-hack side channel.** The hack must publish, per frame, the tile
   data for the surrounding world and the origin of the hardware window within
   it. An emulator-recognised write port or a magic DMA target both work; pick
   an address that is inert on real hardware. This is the part the emulator
   cannot do for itself.
3. **Configuration.** Replace the env hooks with an HD PPU option, default off,
   engaging only when a ROM supplies extended data, so every other game stays
   bit-identical.
4. **Cost on the game side.** The hack has to decompress and lay out 2–4x more
   course than it does now, on a 3.58MHz CPU, while streaming. The emulator
   absorbs the storage, not the CPU time. This is the real risk to the whole
   idea and has not been assessed.

## Open questions

- An extended map changes the 4:3 picture too, at the rows that sample outside
  the map. That is unavoidable — those rows are wrapped today — and is fine for
  a hack that opts in, but it means this can never be an accuracy-preserving
  toggle for unmodified ROMs.
- The reach measurement is from one save state. Other tracks, higher speeds and
  wider aspect ratios should be sampled before fixing on 2x versus 4x.
- Nothing has been tried on a game other than F-Zero. Mario Kart's Mode 7 floor
  has not been looked at from this angle at all.
