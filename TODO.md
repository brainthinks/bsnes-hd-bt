# HD PPU remaining work

Assume Mode 7 supersampling is complete (signed-off F-Zero Slot 2 and
Mario Kart Slot 1 in real bsnes `--fullscreen`, tag `hd-ppu-01`).
This file is what is left to **meet or exceed DerKoun bsnes-hd**.

Picture rules: `.grok/skills/hd-ppu/SKILL.md`.
Capture recipe: `.grok/skills/hd-ppu-visual/SKILL.md`.
Earlier HD-desktop inventory: `docs/hd-ppu-next.md`.

## Already at or beyond DerKoun

| DerKoun | This fork |
|---|---|
| HD Mode 7 at higher resolution (CPU scale × SS) | GPU SS at the real window (what DerKoun asked for and never shipped) |
| True color (3×8 vs 3×5) | 24-bit HD PPU, including non-Mode-7 blend |
| Line-color smoothing | `reconstructColorRamps` + fog lerp (signed-off on F-Zero) |
| Perspective correction | Always on for HD, with Fast’s cylinder skip |
| Repeat 2/3, color math, windows on Mode 7 | In the GPU shader |

This fork also has a third sibling PPU (Accurate / Fast / HD) and isolated
HD settings (`~/.config/bsnes-hd/`). DerKoun had neither.

Exceeding DerKoun from here means doing widescreen on the GPU window path
instead of CPU scale, and not copying DerKoun’s blurry SS. The signed-off
F-Zero/MK look is already that second part.

## Needed for DerKoun parity

### 1. Widescreen (the other half of bsnes-hd)

Without this, the fork is not DerKoun HD in the sense people mean when
they launch it.

Stashed as `phase-1 widescreen overlay before hd-ppu sibling`. That stash
is Fast-PPU code from before the HD sibling existed. Port it onto HD; do
not apply the stash onto Fast.

- [ ] Mode: none / Mode 7 only / all
- [ ] Aspect ratio (16:9, 16:10, 21:9, 2:1, plus custom)
- [ ] Per-layer BG1–4: on/off, autoHor, autoHor&Ver, crop/cropAuto, above/below a scanline, disable
- [ ] Sprites: clip / safe / unsafe
- [ ] Ignore window + fallback x (FF6 / Terranigma)
- [ ] WS area fill (color / auto / black) and markers
- [ ] Overscan crop 216 so 5×16:9 is exactly 1080
- [ ] `.bso` per-game overrides
- [ ] Stretch-windowing for widescreen ROM patches

### 2. HD windowing

DerKoun’s experimental “Window HD”: smooth iris, shadows, and spell masks
across neighboring lines. This fork only lerps COLDATA fog when the window
bits match.

- [ ] Super Tennis, F-Zero shadows, and similar window effects

### 3. HD→SD mosaic

DerKoun: classic mosaic / 1× scale / ignore. HD currently never drops
Mode 7 to the 256-wide mosaic path (that checkbox is Fast-only).

- [ ] Terranigma underworld (the usual case)

### 4. Perspective modes

DerKoun: off / on / auto × wide / medium / narrow. HD is always-on plus
cylinder skip.

- [ ] Tales of Phantasia (known “needs narrow”)

### 5. Disable BG / sprites / windows

- [ ] Wallpaper-screenshot controls (listed DerKoun feature)

## This fork’s remaining list (not DerKoun features)

Required to ship HD even if Mode 7 SS is done. From `docs/hd-ppu-next.md`.

- [ ] **Save states** — HD ↔ Fast without a power cycle; official Fast states must still load
- [ ] **Require OpenGL 3.2 for HD GPU** and drop the leftover GPU-SS checkbox
- [ ] **Prove the game table** — Pilotwings, CV4 4-2/4-3, Contra III EXTBG, Axelay, FF6 world map, Chrono Trigger, and the rest in `docs/hd-ppu-next.md`
- [ ] **EXTBG on the GPU path** — still CPU when `extbg` (`!extbg` in `mode7hd.cpp`). If “Mode 7 SS complete” includes Contra III, this is the leftover Mode 7 hole
- [ ] **Purge debug env last** — `BSNES_DUMP_FRAME`, `BSNES_LOAD_STATE`, `BSNES_DUMP_GPU`, `BSNES_DUMP_FULLSCREEN`, `BSNES_DUMP_GPU_AFTER`, `BSNES_TOGGLE_FS_AFTER`

## Not required for desktop DerKoun parity

Parked as list 2 in `docs/hd-ppu-next.md`:

- [ ] libretro / RetroArch (DerKoun’s main distribution; GPU SS would need `SET_HW_RENDER`)
- [ ] Windows/macOS GPU (HD+CPU already works on any blit driver)
- [ ] Upstream-friendly patches
- [ ] Extra DerKoun-style layers once widescreen exists

## Practical order

1. Widescreen on HD (the parity gap)
2. Save states (so HD is usable)
3. Mosaic, perspective width, window HD, layer-disable — DerKoun knobs, smaller than widescreen
4. Game-table sign-off, then strip debug
