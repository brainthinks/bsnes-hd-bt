# BSNES_LIVE: the recorder as a live reference

`BSNES_LIVE=<path of a Unix socket>` runs the emulator in lockstep with a
program that compares itself against it: fzero-rs's compare mode
(`FZERO_COMPARE=bsnes`, its `docs/compare.md`). Inert unless set, so every
recording made without it is byte for byte what it was.

The code is `bsnes/emulator/live.hpp`, with hooks in
`bsnes/target-bsnes/program/program.cpp` (the frame loop),
`bsnes/sfc/system/system.cpp` (the frame event), `bsnes/emulator/hdtrace.hpp`
(the pad), `bsnes/emulator/ramtrace.hpp` (the register shadow is kept under
the mode as under `BSNES_TRACE_REGS`) and the three PPU cores
(`cgramForTrace`, colour memory for the capture).

## What it does

1. The emulator connects to the socket (the other end listens) and sends the
   hello.
2. It runs its first frame on its own: the frame counter's frame 1, nothing
   held. That is the second entry of a script file's two-entry lead (entry 0
   is never read), and the frame a recording takes its starting state on.
3. From frame 2 on, before each frame it reads requests until an `F`, whose
   pad the frame holds; the frame runs; and it answers with an `S`.
4. `D` asks for the bytes of the last frame's domains; `Q`, or the socket
   closing, quits.

So the other end's pass *n* (from power-on, counting from nought) is the
emulator's frame *n* + 2, exactly the script file's lead
(`BSNES_SCRIPT_FILE`: the entry the machine reads on the frame a recording
calls *n* is entry *n* + 2), and the state an `S` hashes is taken where
`BSNES_TRACE_RAM` takes a record: in `System::frameEvent`, after the poke, at
the start of the vertical blank that ends the frame.

The pad replaces any script (`BSNES_SCRIPT_INPUT`, `BSNES_SCRIPT_FILE`) while
the mode is on. Run-ahead would run a frame more than once, so the mode
refuses it and quits.

## The wire

Every integer is little-endian.

**hello** (emulator to program, once, on connecting):

    magic     8   "FZLIVE" 0x1a 0x00
    version   4   1
    lead      4   2
    domains   1   7
    lengths   4 x 7, in domain order

**requests** (program to emulator):

    'F' pad:u16     run the next frame holding `pad`, in the script file's bit
                    order (up down left right b a y x l r select start, bit 0
                    to bit 11)
    'D' mask:u8     the bytes of the last frame's domains in `mask`
    'Q'             quit

**replies** (emulator to program):

    'S' frame:u32 present:u8 hash:32 x 7
                    after an 'F': the frame counter the frame ran as, which
                    domains the capture holds (bit n for domain n), and the
                    SHA-256 of each (zeroes for an absent one). `present` is
                    nought when the frame event never came, which the other
                    end refuses.
    'B' mask:u8 bytes...
                    after a 'D': the domains of `mask` that are present, each
                    whole, in domain order.

## The domains

| bit | domain  | bytes  | taken from |
|-----|---------|--------|------------|
| 0   | ram     | 131072 | work RAM |
| 1   | vram    | 65536  | the running PPU core's video memory, each word low byte first (as `BSNES_TRACE_VRAM`) |
| 2   | cgram   | 512    | the running core's colour memory, each colour low byte first (fifteen bits) |
| 3   | oam     | 544    | the object table as `$2138` reads it back (as `BSNES_TRACE_VRAM`'s object block) |
| 4   | regs    | 768    | the register shadow, `$2100`-`$21FF` then `$4200`-`$43FF`, the CPU's stores (as `BSNES_TRACE_REGS`) |
| 5   | apu_ram | 65536  | the audio processor's RAM (as `BSNES_TRACE_APU`) |
| 6   | dsp     | 128    | the sound chip's registers (as `BSNES_TRACE_APU`) |

The two audio domains are present only where the sound chip is run as the
hardware runs it - the DSP's `Fast: false`, echo writes reaching audio RAM -
which is `BSNES_TRACE_APU`'s own condition.

## Why a socket and hashes

Hashes keep the steady state to 229 bytes a frame; a program that finds a
hash differing asks for that domain's bytes, still at the same frame, before
it sends the next pad. A Unix socket named by an environment variable needs no
descriptor passing and no second channel, and the emulator's standard output
and error stay free for its own messages.
