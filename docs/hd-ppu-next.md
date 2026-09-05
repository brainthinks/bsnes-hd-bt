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

`tests/hd-ppu` covers packing, GLSL compilation/linking, GPU compositing,
configurable supersampling, raw-texel magnification, extreme horizontal/vertical
minification, and fixed-colour ramp boundaries. Run with
`EGL_PLATFORM=surfaceless make -C tests/hd-ppu run` on a headless Linux host.
The real shader must compile and render; a skipped GL context is not a GPU pass.

The September 2026 regression work uses `a292184b9` (fog), `a22b0b3d2`
(F-Zero filtering), and `aab3d06c9` (Mario Kart footprint) as visual references.
The implementation retains sharp raw texels in magnification, integrates
texel crossings along the more compressed screen axis on the GPU, and reconstructs
short monotonic fixed-colour ramps without changing emulated IO or save states.
The CPU Mode 7 sampling algorithm remains unchanged.

Verified scenes: F-Zero Quick Slot 2 and Mario Kart two-player Quick Slot 1
in real 2560x1440 fullscreen, both immediately and after the initial fades;
Castlevania IV 4-3 cylinder and the saved 4-2 room; HD+CPU on XShm (no OpenGL).
The complete game table above, a full 4-2 rotation sequence, and sustained
performance/motion testing still need coverage. These captures are not a
claim that every Mode 7 game or effect has been validated.

For GPU captures, `BSNES_DUMP_FULLSCREEN=1` waits for the real viewport.
`BSNES_DUMP_GPU_AFTER=N` delays capture until N eligible presentations
(default 8); use a larger value to get past a saved fade-in. Capture sidecars
contain the matching line uniforms, VRAM, palette and output geometry for
inspecting sampling errors. Do not commit ROM-derived capture data.

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
