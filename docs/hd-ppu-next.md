# HD PPU next steps

True color, HD Mode 7, and GPU supersampling are in good shape on Linux with
OpenGL 3.2. That slice is far enough along to freeze for tests and review.
This file is the leftover work that is **not** tests or review.

Accurate remains the default hardware path. Fast is untouched. Perspective
correction is always on for HD (widescreen will want the same interpolation).

## Before calling this done

1. **Strip debug hooks.** `BSNES_DUMP_FRAME` and `BSNES_LOAD_STATE` were test
   scaffolding and should not ship.

2. **GPU supersampling driver UX.** GPU SS only runs on OpenGL 3.2 via GLX.
   Other video drivers silently use the CPU path. Windows and macOS are not
   wired. The checkbox should say that, or disable itself when the active
   driver cannot do it.

3. **GPU Mode 7 is still a subset of the CPU sampler.** It matches F-Zero's
   title (and the 1/a perspective floor). It does not yet honor color math,
   window clipping, EXTBG, or Mode 7 repeat 2/3 (outside-map transparent/clamp).
   Those will show up in other games.

4. **Save states.** Fast↔HD is supposed to serialize, but an F-Zero Slot 1
   save was "incompatible format." That needs a real round-trip before people
   rely on it.

5. **libretro.** `videoFrame(uint32)` exists; HD options and GPU SS are not a
   finished libretro feature.

## Out of scope for this feature (later)

- Widescreen (already stashed as `phase-1 widescreen overlay before hd-ppu sibling`)
- Rewriting the CPU Mode 7 sampler
- Changing Fast or Accurate
- Defaulting GPU SS on (it needs OpenGL; XShm users would get a silent fallback)
