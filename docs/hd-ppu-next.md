# HD PPU: remaining work vs follow-ups

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

Fast↔HD serialize was supposed to match; an F-Zero Slot 1 load failed with
"incompatible format." Required:

- Save and load on HD, same session
- HD → Fast and Fast → HD (Change/Reload without reset, and via `.bst` / `.bsz`)
- HD → Accurate (expect a defined failure or a restart, not a corrupt PPU)
- Quick and undo/redo
- Confirm official Fast states still load in Fast on this fork

### Tests

There are **no** automated HD / Mode 7 / PPU visual tests. CI only compiles.
Coverage so far is manual F-Zero title screenshots. Test the core game table
vs official Fast, with GPU SS on.

### Purge debug stuff (last)

Leave `BSNES_DUMP_FRAME` and `BSNES_LOAD_STATE` until the rest of list 1 is
done, then strip them.

## List 2 — follow-ups (not this HD desktop slice)

- **Widescreen** (stashed: `phase-1 widescreen overlay before hd-ppu sibling`)
- **libretro / RetroArch.** Software core, no GL context. Extract HD later if
  we care; not a gate for desktop HD. GPU SS in RA would need `SET_HW_RENDER`.
- GPU SS on Windows/macOS (WGL/CGL)
- Extra DerKoun-style layers once widescreen exists
- Carving this into upstream-friendly patches

Non-goals: do not change Fast or Accurate; do not rewrite the CPU Mode 7
sampler.
