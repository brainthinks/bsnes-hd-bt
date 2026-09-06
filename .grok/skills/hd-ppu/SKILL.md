---
name: hd-ppu
description: >
  HD PPU on bsnes-hd: optional non-accurate enhancements for every SNES game
  (true color, GPU offload, Mode 7 supersampling). Use when changing ppu-hd,
  GPU Mode 7, true color, banding, noise, or when the user mentions HD PPU,
  the HD renderer, or /hd-ppu.
---

# HD PPU

HD is a third renderer (Accurate / Fast / HD). It is allowed to be
non-accurate. It must apply to **every game**, not a Mode 7 demo. Mode 7 is
the current focus; features like 24-bit color are not Mode 7-specific.

Accurate and Fast stay hardware-accurate and must not change. HD is opt-in.

## Required on every HD PPU change

1. **True color** — HD output is 24-bit. Do not collapse to RGB555. Do not
   replace the 15-bit Fast/Accurate path.
2. **GPU offload** — anything expensive (Mode 7 sampling, atlas, SS, filtering)
   runs on the GPU when OpenGL 3.2 is available. CPU must still produce a
   correct frame if the GPU path is missing or fails (wrong driver, shader
   compile, empty atlas). The user must still *see the enhancement* when GPU
   is selected and working — a CPU 1× fallback that looks like Fast is not
   “HD working.”
3. **Crisp raw pixels. Always.** The user wants to see individual Mode 7
   texels, never colors melted together. Magnified grass, dirt, and dashes
   must look nearest-neighbor / in-texel SS — hard edges, two-color
   checkers, pixel-sharp rings. Do not gaussian, bilinear, mip, 2×2 VRAM
   box, 1-SNES kernel, or expand filter support into neighboring output
   pixels. Mixing neighboring texels to “kill banding” is a fail even if
   the floor is stable. Banding and noise are also regressions; fix them
   without blur. If F-Zero grass looks out of focus, revert. Iterate on
   **bsnes fullscreen** (`--fullscreen`, real viewport) using
   `.grok/skills/hd-ppu-visual/SKILL.md`. Do not ask the user to eyeball
   every attempt. Maximize and a fake `DUMP_W/H` FBO are not fullscreen.
   Widescreen, pitch, and `viewportSize` changes use that same dump loop:
   rebuild, capture F-Zero Slot 2, open the PNG, then fix. Code review is
   not a pass.
4. **All games** — F-Zero is not enough. Super Mario Kart, Castlevania IV,
   Contra III (EXTBG), Pilotwings, and the rest of the game table in
   `docs/hd-ppu-next.md` must keep their Mode 7 (and non-Mode-7 HD) features.
5. **No regressions** — do not break Accurate/Fast, save states, or an HD
   feature that already worked (SS, perspective, fog color math, cylinder
   non-perspective groups, true color).

## Architecture

- Third sibling PPU copied from Fast, not a wrapper.
- GPU sampler is the HD extra-sample path. CPU Mode 7 algorithm is not rewritten.
- Do not live-switch PPU mid-frame.
- GPU SS samples at the window. Do not expand the CPU framebuffer to HD scale
  just to nearest-scale sprites.
- CPU 1× Mode 7 under GPU is a *fallback*, not the HD look. If GPU SS is on
  and the picture matches official Fast, the GPU pass is not compositing —
  fix that, do not ship it.
- GPU Mode 7 must be bound on every present (`bindGpuMode7` from `videoFrame`
  and `viewportRefresh`). `--fullscreen` recreates GLX; after reinit present
  black until the first GPU Mode 7 frame. Never stretch the CPU 256×224 blit
  to the monitor — that is the Mario Kart rainbow far-grass flash.

## Verify

F-Zero Slot 2 and Mario Kart Slot 1 in real bsnes fullscreen are the
signed-off look (banding, clear pixels, perspective). Recapture both after
any filter, present-path, or GLX change. Do not “improve” them with blur,
mips, atlas color, or a 1-SNES kernel.

- **F-Zero Slot 2 race, `--fullscreen`**: sharp 2-color checker up close,
  pixel-sharp yellow dashes, far field stable (no scanline stripes, no mushy
  seam). Interpolated COLDATA fog is intended true-color, not a bug.
- **Super Mario Kart Slot 1 (2-player), `--fullscreen` and windowed →
  fullscreen toggle**: diamond dirt, stable dotted far infield, track
  supersampled. The first fullscreen frame must already be GPU Mode 7 —
  never a rainbow stretched CPU blit that later “corrects itself.”

Still required, not a signed-off look:

- CV4 4-2 rotation and 4-3 cylinder vs official Fast.
- Remaining games in the `docs/hd-ppu-next.md` table (Pilotwings, Axelay,
  FF6 world map, Contra III EXTBG, Chrono Trigger, …).
- Fast/Accurate unchanged; HD+CPU still runs without OpenGL 3.2.

Automated locks: `make -C tests/hd-ppu run`. Visual recipe:
`.grok/skills/hd-ppu-visual/SKILL.md`. What has been run lives in
`docs/hd-ppu-next.md` Tests.
