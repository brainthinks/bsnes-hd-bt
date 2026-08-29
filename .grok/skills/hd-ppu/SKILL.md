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
3. **No Mode 7 noise or banding** — minification must be a stable average, not
   sparkle or scanline bands. SS and/or mips are the tools; dropping SS on
   far floors to “save” an atlas is a regression.
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

## Verify

- F-Zero race: SS floor, fog, no red-tint, no banding.
- Super Mario Kart: track texture present **and** supersampled, including
  2-player.
- CV4 4-2 rotation and 4-3 cylinder vs official Fast.
- Fast/Accurate unchanged; HD+CPU still runs without OpenGL 3.2.
