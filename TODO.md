# Remaining work

Updated 2026-09-05. This is the current roadmap; it supersedes the ordering and
stale implementation descriptions in `docs/hd-ppu-next.md`. That document still
contains the game table and earlier test records.

## Priorities and standing requirements

1. **HD PPU saves compatible with Fast PPU saves**, in both directions.
2. **Parity or better with DerKoun's extra visual features**, including their
   per-game compatibility controls, not just widescreen and true color.
3. **No changes to existing bsnes logic unless necessary; keep necessary changes
   minimal, isolated, and reviewable** to maximize compatibility and upstream
   potential. This applies throughout development, not as a cleanup phase.

HD remains an opt-in sibling PPU for every SNES game. Preserve existing Fast and
Accurate behavior and defaults. Keep enhancements in `ppu-hd`, its GPU renderer,
and narrowly scoped frontend/driver integration. Do not port the old widescreen
patch onto Fast or rewrite the CPU Mode 7 sampler.

Picture requirements: `.grok/skills/hd-ppu/SKILL.md`. Visual validation:
`.grok/skills/hd-ppu-visual/SKILL.md`. Preserve crisp raw texels and 24-bit color;
fix banding without blanket blur, bilinear/mip filtering, or expanded kernels.

## 1. Save-state compatibility

Current implementation: Fast and HD PPU serialization files are identical.
`bsnes/sfc/system/serialization.cpp` writes the shared scanline-PPU flag and
accepts the earlier extra-HD-flag format. This is an implementation foundation,
not proof that all state workflows are compatible.

- [ ] Verify HD → Fast and Fast → HD using the same ROM and compatible serializer
  version, including a new process and Change/Reload in the current session.
  Resume the saved game without a visible reboot or lost progress.
- [ ] Verify official bsnes Fast states load into both Fast and HD here, and states
  produced here load in official Fast where the serializer version/configuration
  matches. Do not silently introduce an HD-only state format.
- [ ] Cover `.bst`/`.bsz`, quick slots, undo/redo, auto-save/auto-resume, rewind,
  and run-ahead. Exercise synchronized states and supported unsynchronized states.
- [ ] Rebuild HD caches and GPU resources from emulated state after loading;
  presentation settings and GPU buffers must not alter the Fast state payload.
- [ ] Check representative coprocessor games and reject incompatible/corrupt
  states cleanly. Accurate's distinct state format is not a promise of HD/Fast
  interchangeability; preserve its behavior and handle mismatches explicitly.
- [ ] Add focused regression coverage for shared layout, legacy state import,
  cross-renderer restoration, and continued execution after load.

## 2. DerKoun feature parity: verified inventory

Checked against DerKoun's [beta 10.6 README and settings](https://github.com/DerKoun/bsnes-hd#settings)
and [override documentation](https://github.com/DerKoun/bsnes-hd#setting-override-files)
on 2026-09-05. This verifies the feature inventory, not completion in this fork.
Equivalent or better behavior is the goal; copying implementation, defaults, or
known artifacts is not required.

| Feature / controls | This fork / remaining work |
|---|---|
| HD Mode 7 resolution and configurable supersampling | Implemented; GPU sampling at output resolution. Broader validation pending. |
| True color | Implemented in HD, including non-Mode-7 blending. |
| Line-color smoothing, adjustable radius/off | Ramp reconstruction and fog interpolation exist; residual banding and control parity remain. |
| Perspective off/on/auto; wide/medium/narrow | HD has automatic cylinder handling; complete controls and test Tales of Phantasia. |
| Mosaic: classic, 1×, ignore | Implement HD policy; current mosaic control is Fast-only. |
| Widescreen: disabled, Mode 7, all scenes | In progress; BG extension and horizon panoramas done, see `docs/fzero-bg-discontinuity-handoff.md`. |
| Aspect presets and custom widths/ratios | Pending; retain independent pixel aspect correction. |
| BG1–4 enable, automatic modes, scanline boundaries, crop/cropAuto, disable | Pending. |
| Sprite clip/safe/unsafe/disable | Pending. |
| Window-ignore modes and fallback coordinate | Pending. |
| Widescreen fill, markers and opacity | Pending. |
| HD window smoothing, radius/off | Pending. |
| 216/224-line output cropping | Pending HD-specific policy. |
| Per-game `.bso` overrides; patched-ROM window stretching | Pending, including scale, sprite-limit and CPU-overclock overrides. |
| Screenshot layer/sprite/window suppression | Pending. |

### Implementation and acceptance work

- [ ] Port the relevant widescreen work from
  `patches/phase-1-widescreen-overlay-before-hd-ppu.patch` into HD only. The patch
  predates the sibling PPU; inspect and adapt it rather than applying it wholesale.
- [ ] Extend HD's GPU composition to widescreen while keeping ordinary sprites
  sharp. Verify clipping, wraparound, priority, HUD boundaries, and window/color
  math against the source behavior and representative games.
- [ ] Support existing widescreen ROM patches and `.bso` files. Verify parsing,
  defaults, lookup location, option precedence, and restoration when changing
  games. Check actual DerKoun code when documentation is ambiguous. Preserve
  original bsnes defaults outside opt-in HD/per-game configuration.
- [ ] Match the useful rendering and compatibility controls in the inventory.
  Validate non-Mode-7 scenes too; do not promise that an emulator can supply
  offscreen game objects that require a ROM patch.
- [ ] Retest F-Zero, Super Mario Kart, FF6, Terranigma, Tales of Phantasia,
  Super Tennis, and widescreen-patched Super Mario World as applicable to each
  feature. Record scene, settings, comparison build, and results.

## 3. Compatibility and upstream potential

- [ ] Audit the fork's diff against its official bsnes base. Classify necessary
  integration separately from HD additions; remove incidental edits and explain
  why each shared-core change is needed.
- [ ] Preserve CPU/APU/coprocessor timing, memory behavior, existing game fixes,
  state conventions, and Fast/Accurate rendering unless a separately justified
  compatibility fix requires a change. Avoid unrelated refactors.
- [ ] Keep HD, SDL3, and general bug fixes independently reviewable and separable
  for potential upstream submission. Prefer small interfaces over scattered
  enhancement-specific conditionals in existing logic.
- [ ] Validate Fast/Accurate and existing input/video drivers after shared changes;
  verify builds with optional dependencies unavailable. Document material
  platform-specific behavior and controller workarounds.
- [ ] Update tests and documentation with each feature. Do not mark parity or
  compatibility complete solely because code exists or a single scene passes.

Upstream readiness is a standing requirement. Opening PRs to official bsnes is
not part of the current task, but must remain feasible.

## 4. Rendering quality and coverage

- [x] Isolated HD PPU and settings directory (`~/.config/bsnes-hd-bt/`).
- [x] Configurable GPU Mode 7 supersampling and true-color rendering.
- [x] CPU/GPU sampler selector and OpenGL 3.2 checks for GPU selection exist.
- [x] Mario Kart fullscreen transition fix: initialize GPU presentation correctly.
- [ ] Resolve the user's remaining slight banding report without reducing texel
  sharpness. Earlier F-Zero/Mario Kart sign-offs are reference scenes, not a
  declaration that current rendering has no remaining defects.
- [ ] Complete GPU EXTBG support; `mode7hd.cpp` still excludes it from GPU work.
- [ ] Verify GPU handling of repeats, windows, transparency, color math and
  perspective across the full game table in `docs/hd-ppu-next.md`, including the
  complete CV4 4-2 rotation and 4-3 cylinder sequences.
- [ ] Verify HD+CPU fallback and video-driver changes, shader/resource failure,
  first-frame presentation, and fullscreen toggles. Keep Fast/Accurate usable
  with their existing drivers.
- [ ] Measure sustained gameplay performance and motion stability, including
  real 2560×1440 fullscreen and audio continuity. Preserve Linux improvements.
- [ ] Remove temporary debug hooks after required validation is complete:
  `BSNES_DUMP_FRAME`, `BSNES_LOAD_STATE`, `BSNES_DUMP_GPU`,
  `BSNES_DUMP_FULLSCREEN`, `BSNES_DUMP_GPU_AFTER`, `BSNES_TOGGLE_FS_AFTER`.

## 5. SDL3 / modern controllers

- [x] SDL3 input worker, canonical SNES bindings, and event-driven hotplug discovery
  without repeated controller-list enumeration while connections are unchanged.
- [x] Regression tests for reconnects, held-button release, stable binding identity,
  worker restart, and Linux synthetic-device hotplug (`tests/sdl-input`).
- [x] Physical Pro 2 reconnect stall diagnosed: enhanced-mode feature request
  blocked for 15.56 seconds. Targeted Linux evdev fallback for `2dc8:6006` tested
  with real reconnect and button input; other device IDs retain HIDAPI.
- [ ] Verify additional controllers, simultaneous devices, USB/Bluetooth changes,
  player assignment, modal settings windows, and DualSense/Steam Input behavior.
- [ ] Implement/validate desired motion, rumble, battery reporting, and extra-button
  support. Current basic inputs are not proof that enhanced features work.
- [ ] Revisit the Pro 2 workaround if firmware/SDL makes enhanced mode reliable.
  Preserve prompt reconnects; document capability tradeoffs and the explicit
  hint override. Do not introduce aggressive polling or UI-thread blocking.

## Deferred platform scope

Libretro/RetroArch and Windows/macOS GPU backends remain follow-ups to desktop
Linux HD. They are distribution/backend work, not missing visual features in the
parity inventory. Preserve existing platform behavior while making focused changes.

## Working order

1. Establish and test the save-state compatibility contract.
2. Deliver widescreen and the verified feature inventory in isolated HD changes.
3. Continue rendering-quality and controller work with their regression coverage.
4. Complete the game/platform validation matrix, then remove debug hooks.

Apply the minimal-change/upstream-compatibility requirement at every step.
