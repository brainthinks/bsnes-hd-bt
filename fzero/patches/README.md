# The fzero-rs recorder patches

These are copies of the recorder changes the fzero-rs project made to this
fork, kept here because they are diffs of this GPL-3.0 source and therefore
cannot live in fzero-rs (which contains only MIT-licensed code). Each change is
also a commit in this repository's history; the file is the same change as a
patch.

| patch | trace | commit |
|---|---|---|
| smp-phase.patch | v10 | 3bb5fc77 |
| divider-clocks.patch | v11 | 54ae1975 |
| voice-state.patch | v12 | 67303b5e |
| voice-step.patch | v13 | a35b1337 |
| oam-block.patch | v14 | 03d7ad3b |
| fetch-address.patch | v15 | 440c953c |
| port-trace.patch | v16 (BSNES_TRACE_PORTS) | 1c2d9449 |
| recorder-v16.patch | v16 | 1c2d9449 |
| live-mode.patch | v16 (BSNES_LIVE, see ../LIVE.md; protocol version 4) | be6c0552, 8b606848, 70bbada5, a442694f, 648e9544 (binary: see ../LIVE.md "The build") |

Moved from fzero-rs `docs/patches/` on 2026-09-29.
