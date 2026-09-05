---
name: hd-ppu-visual
description: >
  How to capture and iterate HD PPU Mode 7 pictures in bsnes-hd: real
  fullscreen (not maximize), GPU viewport dumps, no blur/banding/noise.
  Use when changing GPU Mode 7, SS, kernel, F-Zero grass, Mario Kart
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

```bash
# Copy settings and set Defocus Allow so an unfocused dump is not paused.
# Do not edit ~/.config/bsnes-hd-bt/settings.bml for this.
BIN=/home/user/projects/bsnes-hd-bt/bsnes/out/bsnes
SET=/tmp/bsnes-hd-visual.bml
ROM_DIR=/media/user/2020_obs_capture/games/bsneshd/roms
cp ~/.config/bsnes-hd-bt/settings.bml "$SET"
# Defocus: Allow  (Input / Defocus)

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

Look at F-Zero **and** MK every iteration. Fullscreen, both games. F-Zero
Slot 2 and MK Slot 1 (including the toggle) are the signed-off look; see
hd-ppu Verify.

## Iterate

1. Kill bsnes.
2. Change one thing (kernel, SS count, or present-path bind). Do not add
   atlas sampling or a 1-SNES kernel.
3. Rebuild `bsnes/out/bsnes`.
4. Recapture F-Zero Slot 2, MK Slot 1 `--fullscreen`, and the MK toggle.
5. Open the new PNGs and the previous ones. If any texel looks blended
   into its neighbors (soft grass, mushy dashes, smeared dirt), revert.
   If MK far grass rainbowed, the SNES-space kernel shrank — keep support
   at one output pixel and integrate only texels already inside it.
6. Repeat until both tables pass. Then stop. Do not push unless asked.

## Settings that matter

`~/.config/bsnes-hd-bt/settings.bml`: OpenGL 3.2, HD, GpuSupersample true,
SsFactor 12, TrueColor true. Test dumps may use a copy with Defocus Allow.
