---
name: hd-ppu-visual
description: >
  How to capture and iterate HD PPU Mode 7 pictures in bsnes-hd: real
  fullscreen (not maximize), GPU viewport dumps, no blur/banding/noise.
  Use when changing GPU Mode 7, SS, kernel, widescreen, F-Zero grass, Mario Kart
  fullscreen, or when the user mentions banding, blur, noise, screenshots,
  BMP dumps, or /hd-ppu-visual.
---

# HD PPU visual iteration

The picture the user sees is **bsnes Video fullscreen** at the monitor
size (here 2560×1440). Maximize, a 1920 FBO, and Tools → Screenshot
(CPU 256×224) are all the wrong picture.

Pass/fail is in the hd-ppu skill: **crisp raw pixels always**, never
blurred together; also no banding and no noise. This file is how to get
a truthful image and iterate without asking the user.

## Kill leftover bsnes first

`pgrep -f` / `pkill -f` match this agent. Exact `comm` only:

```bash
ps -eo pid,comm | awk '$2=="bsnes"{print $1}' | xargs -r kill -9
ps -eo pid,comm | awk '$2=="bsnes"{print}'   # must be empty
```

Kill before launch and after every dump.

## Capture (do this, not a fake FBO)

A stale `/tmp/bsnes-hd-visual.bml` is a fail. Always recopy, then force
Defocus Allow. For widescreen work, grep the copy: `WsMode` must be `1`
(or `2`) and `Widescreen` must be `1609`. Missing keys dump a 4:3 picture.

```bash
# Copy settings. Do not edit ~/.config/bsnes-hd-bt/settings.bml for this.
BIN=/home/user/projects/bsnes-hd-bt/bsnes/out/bsnes
SET=/tmp/bsnes-hd-visual.bml
ROM_DIR=/media/user/2020_obs_capture/games/bsneshd/roms
cp ~/.config/bsnes-hd-bt/settings.bml "$SET"
sed -i 's/^  Defocus: .*/  Defocus: Allow/' "$SET"
grep -E 'WsMode:|Widescreen:' "$SET"   # widescreen dumps: 1/2 and 1609

DISPLAY=:0 BSNES_LOAD_STATE="Quick/Slot 2" BSNES_DUMP_GPU=/tmp/fz-fs.ppm \
  BSNES_DUMP_FULLSCREEN=1 \
  "$BIN" --fullscreen --settings="$SET" "$ROM_DIR/F-Zero (U) [!].smc"

DISPLAY=:0 BSNES_LOAD_STATE="Quick/Slot 1" BSNES_DUMP_GPU=/tmp/mk-fs.ppm \
  BSNES_DUMP_FULLSCREEN=1 \
  "$BIN" --fullscreen --settings="$SET" "$ROM_DIR/Super Mario Kart (U) [!].smc"
```

- `--fullscreen` is `toggleVideoFullScreen` (GLX monitor-sized window).
  It is **not** window maximize and **not** presentation pseudo-fullscreen.
- Do **not** set `BSNES_DUMP_W` / `BSNES_DUMP_H`. Those render a fake FBO
  and lie about `targetSize`.
- `BSNES_DUMP_FULLSCREEN=1` waits until the real viewport is ≥1920×1000,
  then `glReadPixels` that viewport and `_exit`.
- `BSNES_DUMP_GPU_AFTER=N` counts only those capture-ready frames before
  dumping (default 8, enough to pass F-Zero’s saved fade). `=1` is the
  first fullscreen frame.
- Convert PPM→PNG and **open the PNG**. Unique-color counts are not enough.
  Code inspection is not a pass. Do not tell the user it is fixed until
  you have opened this dump.

If the PPM is smaller than 1920 wide, the dump ran windowed. Fix the wait;
do not proceed.

## Mario Kart fullscreen toggle

`--fullscreen` from launch is not the MK rainbow. Recreate windowed →
fullscreen (no `--fullscreen` on the command line):

```bash
DISPLAY=:0 BSNES_LOAD_STATE="Quick/Slot 1" BSNES_DUMP_GPU=/tmp/mk-tog.ppm \
  BSNES_DUMP_FULLSCREEN=1 BSNES_TOGGLE_FS_AFTER=40 BSNES_DUMP_GPU_AFTER=1 \
  "$BIN" --settings="$SET" "$ROM_DIR/Super Mario Kart (U) [!].smc"
```

`TOGGLE_FS_AFTER` calls `toggleVideoFullScreen` after N main-loop frames.
`DUMP_GPU_AFTER=1` is the first fullscreen-ready frame (the rainbow lived
here). Repeat with `DUMP_GPU_AFTER=30` to confirm it stays GPU-composited.
Pass/fail is the Mario Kart line in the hd-ppu skill.

## What to look at

Crop the Mode 7 floor, not the HUD.

| Shot | Pass | Fail |
|---|---|---|
| F-Zero Slot 2 grass | Sharp 2-color checker up close; far field stable, no scanline stripes | Mushy/woven top half; hard seam; horizontal bands |
| F-Zero dashes/road | Pixel-sharp yellow rings, flat road | Soft rings |
| MK Slot 1 dirt | Diamond dirt, rainbow road edge | Smeared dirt |
| MK Slot 1 far infield | Stable dotted green | Rainbow moiré, sparkle, red wedges |
| MK Slot 1 first frame after windowed→fullscreen | Already GPU Mode 7 | Rainbow far grass / stretched 256×224 that later corrects |
| F-Zero/MK widescreen Mode 7 | Floor continues into the extra columns; no horizontal wrap-stripes | Scanline stripes; 4:3 picture with empty sides; pitch smear |

Look at F-Zero **and** MK every iteration. Fullscreen, both games. F-Zero
Slot 2 and MK Slot 1 (including the toggle) are the signed-off look; see
hd-ppu Verify.

Widescreen is the same duty: dump, open, fix, dump again. After any
width, pitch, crop, or `viewportSize` change, recapture F-Zero Slot 2
widescreen **before** asking the user. Diagnoses from the PNG:

- Horizontal wrap-stripes: CPU buffer pitch ≠ `lineWidth()`.
- 4:3 picture with empty sides: `viewportSize` still uses 256, or the
  dump copy has `WsMode: 0` / missing `Widescreen`.
- Extra columns black/backdrop while the 256 core is Mode 7: Mode 7
  loop is not walking `x=-ws..256+ws`, or `wsOverride` zeroed `ws`.
- Small leftover bars after the picture is already wide: 16:9 is 64
  SNES pixels per side on 216 lines (`384x216`). Do not shrink that with
  8:7 PAR and then stretch. GPU log should read `384x216`. `336x216` is
  the old PAR-shrunk width (stretched tiles, less map).
- HUD/sprites staying in the middle with Mode 7 filling the sides is
  expected for Widescreen = Mode 7 (`WsMode` 1) until that layer is **On**.
  BG1–4 knobs are in Enhancements (HD). autoHor&Ver (default) skips a
  screen-wide layer at scroll 0 (HUD). On walks the tilemap into extra
  columns — no color clamp. If a HUD sky is black in extra columns while
  BG extra>0, GPU Mode 7 is replacing non-Mode-7 BG1; only valid Mode 7
  lines should use the GPU sampler. F-Zero sky is a 512-wide dual-screen
  nametable; extra columns wrap that map. Suppressing wrap blacks the
  left extra (those tiles are the other end of the 512 map).

Crop the left and right extra columns of the PNG (about 40/336 of the
game picture each side at 16:9 + PAR). Those crops must show the floor
continuing, not HUD, not black, not a wrapped next scanline.

## Unattended runs: measure, do not only look

A dumped PNG answers "does this look blurred". It cannot answer "is the right
tile fetched at every heading" — the widescreen horizon has ~1000 distinct
scroll positions per layer and a wrong one shows up as a seam or a popping
tree only at a few of them. For that class of bug, drive the game and check
the numbers.

`bsnes/emulator/hdtrace.hpp` is the harness. Everything is env-gated and a
no-op unless set.

```bash
BIN=/home/user/projects/bsnes-hd-bt/bsnes/out/bsnes
ROM_DIR=/media/user/2020_obs_capture/games/bsneshd/roms

DISPLAY=:0 BSNES_HEADLESS=1 \
  BSNES_LOAD_STATE="Quick/Slot 3" \
  BSNES_SCRIPT_INPUT="30:b,150:b+right" \
  BSNES_TRACE_BG=/tmp/fz-trace.txt \
  BSNES_FRAME_DIR=/tmp/fz BSNES_FRAME_AT="150,160,170" \
  BSNES_QUIT_AFTER=910 \
  "$BIN" "$ROM_DIR/F-Zero (U) [!].smc"
```

- `BSNES_HEADLESS=1` skips presenting the frame; the PPU's own dumps still
  happen. **Use it for every unattended run.** Here `video.output()` blocks
  about a second per frame through GLX at HD Mode 7 scale 8 — the same on a
  pre-change baseline, so it is not PPU work — which turns a 900-frame sweep
  into 15 minutes and leaves a window on the user's desktop. XShm does not
  have the problem; the GL swap does.
- `BSNES_SCRIPT_INPUT="frame:buttons,..."` holds buttons from a frame number
  until the next waypoint; `+` combines (`b+right`). Names: up down left right
  b a y x l r select start none.
- `BSNES_QUIT_AFTER=N` exits after N frames. Always set it.
- `BSNES_TRACE_BG` writes per-frame heading and per-line BG state (mode, tile
  size, screen size, screen address, h/v offset, derived panorama grid).
- `BSNES_DUMP_VRAM_AT="200,400"` dumps the tilemap into the trace;
  `BSNES_DUMP_VRAM_BIN=<prefix>` writes raw VRAM + CGRAM for those frames, to
  decode offline when the question is "how does this game store the horizon".
- `BSNES_NO_PAN=1` disables the panorama path, for A/B.

Driving: F-Zero `B` accelerates and steering only turns while moving, so
`"30:b,150:b+right"` from `Quick/Slot 3` sweeps 360 degrees repeatedly. Mario
Kart `Quick/Slot 2` is paused, so `"30:b,120:b+left"` resumes and turns.

### A/B two builds honestly

Two traps make an A/B lie:

1. **Save RAM.** Auto-save rewrites the game's `.srm` on exit, so the second
   run starts from a different save than the first and the games diverge for
   reasons that have nothing to do with the change. Copy the ROM **and its
   `.srm`** into a fresh directory for each side. Without this, Yoshi's Island
   and Super Metroid "differ" and Mario Kart looks 100% changed.
2. **Leftover bsnes.** A run that outlives its shell keeps a window open and
   rewrites `settings.bml` on exit, silently undoing a restore. Kill by exact
   `comm` (see the top of this file) after every run, and never edit
   `~/.config/bsnes-hd-bt/settings.bml` — pass `--settings=` a copy.

### The invariant to assert

For any widescreen BG change, compare the two sides per column and assert that
**no changed column lies inside renderX 0..255** (output x 64..319 at 16:9).
Inside the 256-pixel picture the hardware's own wrapping *is* the picture; only
the extension may move. Separately, re-run with widescreen off and require the
frames to be byte-identical.

That pair caught every mistake in the horizon-panorama work, including one the
PNGs looked fine for.

## Iterate

1. Kill bsnes.
2. Change one thing (kernel, SS count, present-path bind, or widescreen
   pitch/width/viewport). Do not add atlas sampling or a 1-SNES kernel.
3. Rebuild `bsnes/out/bsnes`.
4. Recapture F-Zero Slot 2, MK Slot 1 `--fullscreen`, and the MK toggle.
   Widescreen work: F-Zero Slot 2 first, then MK.
5. Open the new PNGs and the previous ones. If any texel looks blended
   into its neighbors (soft grass, mushy dashes, smeared dirt), revert.
   If MK far grass rainbowed, the SNES-space kernel shrank — keep support
   at one output pixel and integrate only texels already inside it.
6. Repeat until both tables pass. Then stop. Do not push unless asked.

## Settings that matter

`~/.config/bsnes-hd-bt/settings.bml`: OpenGL 3.2, HD, GpuSupersample true,
SsFactor 12+, TrueColor true. Widescreen dumps also need HDMode7
`WsMode` 1 or 2 and `Widescreen` 1609. Test dumps may use a copy with
Defocus Allow; never reuse a stale copy.
