# HD PPU: remaining work vs follow-ups

Agent requirements for HD PPU work live in `.grok/skills/hd-ppu/SKILL.md`.

This feature is **HD PPU** on the desktop app: a third renderer (Accurate /
Fast / HD) with 24-bit color and HD Mode 7. Supersampling on HD is the GPU
path (no checkbox). Fast and Accurate do not change. The CPU Mode 7 sampler
algorithm is not rewritten.

Make it work the way we want on desktop first. Upstream extraction and
RetroArch can wait.

### Order

1. Finish GPU Mode 7 (this is next)
2. Require OpenGL 3.2 for HD; drop the GPU-SS checkbox
3. Save states
4. Manual testing against the game table
5. Purge debug hooks last

## List 1 — finish HD PPU

### Finish the rest of Mode 7 (next)

CPU HD Mode 7 is the Fast sampler at HD scale with 24-bit color. GPU SS is
still a **subset**: wrap-only map, no color math, no windows, no EXTBG, no
repeat 2/3 (transparent / tile-0 outside the 1024² map). F-Zero's title is
not enough.

GPU SS must match the CPU HD path on the cases below, or refuse GPU SS for
that scanline/frame and keep the CPU pixels (same rule we already use for
non-Mode-7 sky).

#### Core games (must test, CPU HD and GPU SS vs Fast/official)

| Game | Why |
|---|---|
| **F-Zero** | HDMA perspective floor; title + race + results |
| **Super Mario Kart** | HDMA perspective; sprites on the track; HUD |
| **Pilotwings** | HDMA + rotation, not a racing floor |
| **Super Castlevania IV** | Rotating rooms; often empty/transparent outside the map |
| **Contra III** | Stage 2 Mode 7; **EXTBG** (player under the bridge) |
| **Axelay** | Mode 7 stages; color math |
| **Final Fantasy VI** (FF3 US) | World map; **repeat 2/3** outside the map; color math |
| **Chrono Trigger** | Overworld / epoch Mode 7 |
| **Secret of Mana** | World map |
| **Super Mario World** | Bowser fight and other Mode 7 bits |
| **Yoshi's Island** | Mode 7 stages/effects |
| **Super Metroid** | Ceres and other Mode 7 rooms |

#### Secondary (hit if the core set is clean)

| Game | Why |
|---|---|
| Super Ghouls 'n Ghosts | Rotation / scale bosses |
| Super Turrican 2 | EXTBG |
| Demon's Crest | Mode 7 bosses |
| Zelda: A Link to the Past | Ending / Mode 7 effects |
| Terranigma / Illusion of Gaia | RPG maps |
| Super Tennis / Super Smash Tennis | **Windows** + color math (net/scoreboard) |
| HyperZone, Exhaust Heat / F1 ROC | Extra HDMA-floor coverage |
| Mohawk & Headphone Jack | Fast PPU already has an EXTBG order hack; HD copied it |

### Desktop drivers

**HD GPU sampling requires OpenGL 3.2.** HD sampler is a CPU/GPU dropdown
(default GPU). HD+CPU works on any video driver. Fast and Accurate keep
working on any video driver.

- If the user loads a video driver other than OpenGL 3.2 while HD is
  selected: error that this driver does not support HD PPU, then switch
  the renderer to Fast.
- If a non-OpenGL driver is already active and the user selects HD: same
  error, stay on Fast (do not apply HD).

Linux GLX is list 1. Windows/macOS OpenGL is list 2.

### Save states

HD is Fast-shaped (same VRAM/CGRAM/IO). Do not write a separate `hdPPU`
bit — that made official Fast states fail here and made Change/Reload
Fast↔HD power-cycle. Accurate stays incompatible with scanline states.

- Save and load on HD, same session
- HD → Fast and Fast → HD (Change/Reload without reset, and via `.bst` / `.bsz`)
- HD → Accurate (expect a defined failure or a restart, not a corrupt PPU)
- Quick and undo/redo
- Official Fast states load in Fast (and HD) on this fork

### Tests

Picture rules live in `.grok/skills/hd-ppu/SKILL.md`. The capture recipe
lives in `.grok/skills/hd-ppu-visual/SKILL.md`. This section is the inventory
of what has actually been run. `/tmp` PPM/PNG/sidecars are ROM-derived
scratch — do not commit them, and do not treat them as the record.

#### Automated (`tests/hd-ppu`)

`EGL_PLATFORM=surfaceless make -C tests/hd-ppu run`

Locks packing, GLSL compile/link, GPU compositing (`ss=1` unique ≥ 2 vs
`ss=8` a different average), live `decodeVram`, no `if(!aboveWin) rgb=0`,
flush-wipe policy, luma clamp, `integrateM7`, no gaussian (`exp(-2.0)`) or
3× taper (`1.0 + 2.0 * taper`), extreme minification max channel error ≤ 1
(including negative/wrapped and X-compressed), magnification matching a
12×12 raw-texel grid, and COLDATA ramp reconstruction (monotonic 2–8 line
plateaus; reverse/window/math/group edges stay discrete). The real shader
must compile and render; a skipped GL context is not a GPU pass.

Last run: 103 passed, 0 failed.

#### Visual (real 2560×1440 bsnes `--fullscreen`)

Geometry of a valid dump is `256 224 1877 1440 2560 1440` (SNES / target /
output). Smaller than 1920 wide means the dump ran windowed.

| Scene | How | Result |
|---|---|---|
| F-Zero Slot 2 race | `--fullscreen`, `DUMP_FULLSCREEN=1`, `DUMP_GPU_AFTER=8` | Signed-off: crisp checker/dashes, no scanline bands, interpolated fog intended |
| MK Slot 1 2-player | `--fullscreen`, same | Signed-off: diamond dirt, stable far infield, perspective |
| MK Slot 1 windowed→fullscreen | no `--fullscreen`; `TOGGLE_FS_AFTER=40`; `DUMP_GPU_AFTER=1` then `30` | Signed-off: first FS frame already GPU Mode 7; no rainbow CPU blit |
| CV4 4-3 cylinder / saved 4-2 room | earlier `--fullscreen` dumps | Captured once; **not** re-signed against the current crisp filter |
| HD+CPU on XShm | no OpenGL 3.2 | Captured once; re-check after the fullscreen bind change |

Filter look: `0f250f287` (kernel = one output pixel; integrate only texels
already inside it). Fullscreen present-path: `Program::bindGpuMode7` on
`videoFrame` and `viewportRefresh`; `OpenGL::terminate` clears
`mode7MapReady`; GLX `initialize` presents black before the first Mode 7
frame. The CPU Mode 7 sampling algorithm remains unchanged.

Still untested vs the game table: Pilotwings, Axelay, FF6 world map,
Contra III EXTBG, Chrono Trigger, and the rest. A full 4-2 rotation
sequence and sustained performance/motion testing still need coverage.

### Purge debug stuff (last)

Leave `BSNES_DUMP_FRAME`, `BSNES_LOAD_STATE`, `BSNES_DUMP_GPU`,
`BSNES_DUMP_FULLSCREEN`, `BSNES_DUMP_GPU_AFTER`, and
`BSNES_TOGGLE_FS_AFTER` until the rest of list 1 is done, then strip them.

## List 2 — follow-ups (not this HD desktop slice)

- **Widescreen** (stashed: `phase-1 widescreen overlay before hd-ppu sibling`)
- **libretro / RetroArch.** Software core, no GL context. Extract HD later if
  we care; not a gate for desktop HD. GPU SS in RA would need `SET_HW_RENDER`.
- GPU SS on Windows/macOS (WGL/CGL)
- Extra DerKoun-style layers once widescreen exists
- Carving this into upstream-friendly patches

Non-goals: do not change Fast or Accurate; do not rewrite the CPU Mode 7
sampler.
