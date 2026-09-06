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

**How the streaming actually works.** The map is expanded into WRAM at
$7F:4A00–4BFF and DMA'd to VRAM: about 512 bytes a frame in steady driving,
three blocks of 256/128/128 plus a small 69-byte one. So the course data in ROM
is encoded and the game decodes it into that buffer; the emulator can see both
the buffer and the destination address of every block.

**Whether the streamed history can be stitched is still open.** An early test
said no — successive whole maps do not align under any translation (best shift
87% vs 86% for no shift, 69% vs 69%, 75% vs 75%). That test was poor: it
correlated maps captured far apart, by which point nearly every column had been
replaced, over highly repetitive city tiles. Watching the per-block destinations
instead is a much better shot and has not been tried. Do not treat "cannot be
stitched" as established.

**The world position is a plain translation, and it is in RAM.** This was the
load-bearing assumption; it holds. The Mode 7 offsets are the world position
modulo 1024 — they slide smoothly as the car drives and wrap at 1024 (957 down
to 64, then 972). Unwrapping them gives a continuous world position, and
searching WRAM for a value that tracks it finds several with correlation
**1.0000** and matching ranges. Checked as an exact relationship rather than a
correlation, across 175 snapshots over 700 frames:

| WRAM (F-Zero U) | tracks | offset from the unwrapped world coordinate |
|---|---|---|
| `$7E:00A8`, `$7E:00A2` | world X | constant 2688 |
| `$7E:0B70` | world X | constant 3200 |
| `$7E:00AA`, `$7E:0022` | world Y | constant 3504 |
| `$7E:0EB8` | world Y | constant 0 |

Constant across every sample, in 1:1 pixel units. So the mapping from the
hardware's map coordinates to a world frame is exactly a 2D translation, and
its value can be read from two documented addresses.

Method, to redo on another game: log the Mode 7 offsets per frame and dump WRAM
every few frames; unwrap the offsets by detecting the 1024 jumps; then for every
16-bit WRAM address, check whether `value - unwrapped` is constant.

Caveats: one save state, one track, ~700 frames, and only the stretch of
driving that fits in it. Re-check across a full lap and on other tracks,
especially where the course crosses the map wrap. Addresses are per ROM
revision, and some of the above are probably mirrors of each other.

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

**A file loader first, whatever supplies the data later.** The rendering half is
independent of where the tiles come from, so it was worth proving on its own. It
was originally assumed the supply had to be a ROM hack writing through a side
channel; that assumption is wrong and the loader deliberately does not encode
it.

**Generic mechanism in the emulator, per-game knowledge as data.** Every route
needs someone to know how a particular game lays out its course. What matters is
where that knowledge lives. It must not live in `mode7hd.cpp`. This rules out
having the emulator watch the stream and infer the world frame: inferring
per-game behaviour is per-game logic in disguise, and the fragile kind. The
emulator gets one generic capability — render Mode 7 through an extended map,
positioned by a world origin it reads from a memory address — and knows nothing
about which game or what the value means. Everything else is a supplement file.

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

## The world cache, and what it measured

Built: tilemap writes are remembered against the world coordinate they were
written for, and the Mode 7 samplers consult that cache wherever a sample has
left the hardware's window — the same precedence rule the extended map uses, so
an empty cache renders exactly as today.

- `bsnes/emulator/m7worldcache.hpp` — `Mode7WorldCache` (open addressing,
  power-of-two capacity, evicts rather than growing, because a miss only costs a
  fallback) and `Mode7WorldOrigin` (the descriptor: two addresses and the
  constant each value sits above the world coordinate).
- Writes are captured in `writeVRAM<0>`, which is where Mode 7 tilemap entries
  land. The world coordinate for a slot is chosen by rounding the origin to
  whole maps, because the map is a ring buffer and a slot holds the copy of its
  column nearest the player.
- The origin is recomputed each flush as `worldPosition - Mode 7 register`,
  which is always a whole number of maps and self-corrects.
- `BSNES_M7_WORLD="7e00a8-2688,7e00aa-3504"` supplies the descriptor,
  `BSNES_M7_WORLD_BITS` the capacity, `BSNES_M7_WORLD_STATS=1` prints fill and
  hit counts.

Verified: with no descriptor the build is byte-identical to the previous commit
across 30 frames of a drive; `tests/hd-ppu` is at 237 passing.

**It works, and on a first pass it barely helps.** Over 700 frames of driving:

| frame | tiles cached | hits | misses |
|---|---|---|---|
| 100 | 2802 | 0 | 1.40M |
| 300 | 11406 | 0 | 2.48M |
| 500 | 20954 | 0 | 3.84M |
| 700 | 25858 | 81934 | 4.97M |

The cache fills steadily and the origin tracks the car correctly, but the hit
rate is about 1.6% and stays at zero until the player has been somewhere twice.
The reason is structural, not a bug: **a forward-facing camera needs world
ahead of the player, and the game only streams a place as it approaches.** The
cache holds what is behind. It should pay off on a second lap, and for looking
sideways at ground already driven, and it cannot help a first pass.

**A second pass roughly doubles it, and then it stops.** Measured on F-Zero's
attract demo, which drives a whole lap properly and repeats it. Title-screen
stretches are excluded — there the world is a single static map and everything
hits, which says nothing. Race segments only:

| pass | cache at end | hit rate |
|---|---|---|
| first | 16,564 → 239,223 | **31.0%** |
| second | 239,223 → 239,233 | **61.3%** |
| third | 239,233 → 239,233 | 60.9% |

So the cache saturates after one pass — it stops growing almost exactly, meaning
the game has streamed everything it is ever going to — and repeat visits then
serve about twice as many samples as the first.

**But it plateaus at 61%, and that ceiling is real.** It is not capacity and not
the data structure: at 2^22 slots instead of 2^20 the numbers are identical
(31.1% and 61.2%), and the table never exceeded a quarter load. The remaining
39% ask for world the game never streams at all — the far samples near the
horizon reach hundreds to a couple of thousand map units out, and the game only
ever loads a 1024-wide band along the course corridor.

That reframes the speculative decoder. It was going to be the piece that made
this useful, and it still covers the first pass — but it can only produce what
the ROM's course data actually describes. Whether that data extends sideways
far enough to serve the remaining 39%, or whether the course is a ribbon with
nothing beside it, is now the load-bearing question for the whole idea, and it
is unanswered. Worth settling before building the decoder.

Also unmeasured: this is one track. Each track would fill its own world, and
nothing has been checked about switching between them.

## What is left

1. **GPU sampler.** The GPU Mode 7 shader still reads the hardware's 128x128
   map from VRAM, so loading an extended map currently *disables* GPU
   supersampling and falls back to the CPU picture — which the hd-ppu skill
   rightly calls not the HD look. Teaching the shader a larger texture is the
   next piece of rendering work and is not hard; the texture upload and the
   sampler wrap are the only parts that change.
2. **Where the tiles come from.** Settled in shape, not yet built. **The
   supplement carries no game assets.** Anything already in the ROM is read from
   the ROM at run time; the supplement holds only new data and descriptors, so
   it never contains copyrighted content.

   That rules out shipping the course map in the file, and the world-position
   measurement above makes it unnecessary. The emulator can **cache tilemap
   writes as they stream past, keyed by world coordinate** rather than by map
   coordinate — the world coordinate being read from the address the supplement
   names. Every tile then comes from the ROM at run time, through the game's own
   decoder, and the supplement is two addresses and a constant.

   This is generic mechanism: cache what is written, index it by a number read
   from a named address. Nothing in emulator code knows which game it is. It is
   also not the rejected "infer the world frame from the stream" idea — nothing
   is inferred, the frame is supplied.

   Built and measured; see the section above. The warm-up turned out to be
   structural rather than a matter of patience, which makes the speculative
   decoder necessary rather than optional. Whether a cache may persist between
   runs is still open — a local cache is not a distributed asset, but that needs
   a decision rather than an assumption.

   A ROM hack publishing the origin stays the fallback for a game with no usable
   position value in RAM.

   For the record, this item was framed wrongly twice before. First it claimed a ROM
   hack would have to decode 2–4x more course on a 3.58MHz CPU: the SNES CPU
   does not have to produce the tiles at all. Then it proposed the emulator
   derive the world frame from the stream, which is per-game logic in the wrong
   place. The three ways to obtain the data, cheapest first, are still worth
   listing — as ways to build the supplement, not as runtime behaviour:

   a. **Watch what the game already produces.** The emulator sees every
      streamed block and where it lands. Rejected as a plan: placing blocks in
      a global frame means inferring per-game behaviour inside the emulator.

   b. **Run the game's own decoder speculatively.** bsnes already serializes
      complete machine state, so a snapshot can be taken, the decode routine
      pointed at a course position the game has not asked for, run, the WRAM
      buffer harvested, and the snapshot restored. Cost on the host is
      nothing. Needs the routine's entry point and the input that selects a
      position — per-game reverse engineering, but modest, and no ROM hack.

   c. **Decode from ROM on the host.** Full format reverse engineering. Most
      work, most control, still no ROM hack.

   With the world position available, (a) becomes the plan — but keyed by a
   supplied coordinate rather than an inferred one. (b) and (c) stay useful for
   filling the world ahead of the player without a warm-up lap, and neither puts
   assets in the supplement, since both read the ROM at run time.
3. **Configuration.** Replace the env hooks with an HD PPU option, default off,
   engaging only when extended data is available, so every other game stays
   bit-identical.

## Open questions

- An extended map changes the 4:3 picture too, at the rows that sample outside
  the map. That is unavoidable — those rows are wrapped today — and is fine for
  a hack that opts in, but it means this can never be an accuracy-preserving
  toggle for unmodified ROMs.
- The reach measurement is from one save state. Other tracks, higher speeds and
  wider aspect ratios should be sampled before fixing on 2x versus 4x.
- Nothing has been tried on a game other than F-Zero. Mario Kart's Mode 7 floor
  has not been looked at from this angle at all.
- The `.m7x` file the debug loader reads contains tile indices lifted from the
  ROM. It is a development artifact and must not be distributed; the shipping
  path reads tiles from the ROM at run time.
- Measured: a first pass serves 31% of out-of-window samples, later passes 61%,
  and the ceiling is not the cache. The open question is now whether the ROM's
  course data describes the world beside the corridor at all. If it does not,
  no amount of decoding helps and the remaining 39% needs authored content —
  which the no-copied-assets rule permits, since new assets are fine.
