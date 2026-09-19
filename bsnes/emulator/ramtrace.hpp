#pragma once

// Record work RAM frame by frame, for verifying a reimplementation against the
// original.
//
// A port of a game's machine code goes wrong by writing one wrong byte long
// before anything looks wrong on screen. The only practical defence is to
// compare against the real thing every frame, so this writes what the real
// thing did: the RAM it started from, then per frame the controller state and
// the bytes that changed. A frame usually touches a few hundred bytes out of
// 128K, so deltas keep even long recordings small.
//
// Env-gated and inert unless BSNES_TRACE_RAM is set, like the other hooks here.
//
// The file it writes is the RAM contents of a copyrighted game. It belongs to
// whoever owns the ROM it came from and should never be distributed.
//
// BSNES_TRACE_VRAM=1 records video memory alongside work RAM, the same way and
// at the same moments. It is off by default because most recordings do not need
// it and it makes them bigger, but it is the only way to check the half of a
// port that draws: a routine whose whole job is moving bytes into video memory
// has nothing to be compared against without it.
//
// BSNES_TRACE_SRAM=1 does the same for the cartridge's battery RAM, the bank
// $70 the game keeps its saved times in. Off by default for the same reason:
// almost nothing reads it, and the one subsystem that does - the save block's
// checksum - cannot be checked at all without it, because it sums bytes that
// live nowhere else.
//
// BSNES_TRACE_APU=1 does the same for the audio processor's own 64K of RAM and
// the sound chip's 128 registers. Off by default, and bigger than either of the
// above, but it is the only way to check anything about sound at all: the 65816
// cannot reach audio RAM, and everything the sound driver does happens out of
// its sight. Recording it needs the echo buffer to be going into audio RAM
// rather than into the emulator's private shadow, so the recorder refuses when
// the echo-shadow hack is on rather than writing a recording that differs from
// the machine.
//
// BSNES_TRACE_REGS=1 does the same for the display and transfer registers -
// $2100-$21FF and $4200-$43FF - snooped off the CPU's own stores. They cannot
// be read back out of the machine, most of them being write-only, so a shadow
// of what was written is the only form this domain has; it is also exactly the
// claim a port can be held to, because the port writes them too. Nothing in
// the game reads them back either, which is why a comparison of work RAM can
// be perfect while the picture is wrong: a wrong tilemap base is invisible to
// every other domain the recorder carries.
//
// Off by default, and cheap when on: 768 bytes of shadow, delta-encoded, and a
// racing frame changes a couple of dozen of them.
//
// Format, little-endian, matching the reader in the fzero-rs project:
//   char   magic[4]     "FZTR"
//   uint16 version      9
//   uint32 ram_len      0x20000
//   uint32 frame_count  patched on close
//   Registers initial   the machine when the first record was taken
//   uint32 vram_len     0x10000, or 0 when video memory was not recorded
//   uint32 save_len     the cartridge's battery RAM, or 0 when not recorded
//   uint32 apu_len      0x10000, or 0 when audio RAM was not recorded
//   uint32 dsp_len      128, or 0 when the sound registers were not recorded
//   uint32 regs_len     0x300, or 0 when the registers were not recorded
//   uint8  initial[ram_len]
//   uint8  initial_vram[vram_len]
//   uint8  initial_save[save_len]
//   uint8  initial_apu[apu_len]
//   uint8  initial_dsp[dsp_len]
//   uint8  initial_regs[regs_len]
//   AudioSnapshot initial_audio   (absent when apu_len is 0)
//   per frame:
//     uint16 input      controller 1, in $4218 bit order
//     Registers regs    the machine when this record was taken
//     uint32 delta_count
//     per delta: uint32 offset, uint8 value
//     uint32 vram_delta_count   (absent when vram_len is 0)
//     per delta: uint32 offset, uint8 value
//     uint32 save_delta_count   (absent when save_len is 0)
//     per delta: uint32 offset, uint8 value
//     uint32 apu_delta_count    (absent when apu_len is 0)
//     per delta: uint32 offset, uint8 value
//     uint32 dsp_delta_count    (absent when dsp_len is 0)
//     per delta: uint32 offset, uint8 value
//     uint32 regs_delta_count   (absent when regs_len is 0)
//     per delta: uint32 offset, uint8 value
//     AudioSnapshot regs        (absent when apu_len is 0)
//
// AudioSnapshot is forty bytes: pc as uint16, then a, x, y, sp, psw, the
// control register, the DSP address, the four bytes each way through the
// mailbox, the two spare bytes, and then each timer's divider, prescaler,
// stage and output as three bytes apiece. One pad byte, and then the audio
// processor's own cycle count as a uint64 -- how many of its cycles pass in a
// video frame is a measurement rather than a ratio, because the two machines
// run off separate crystals -- and last a uint16 saying how far into the echo
// buffer the sound chip is writing, which lives inside the chip and appears
// nowhere a driver could look, and a uint8 saying which of the sound chip's
// thirty-two steps it is on, so a reimplementation starts its samples in step
// rather than up to one sample out; then a uint16 for the counter every
// envelope rate is measured against, which runs down over a range of 30720,
// and a uint8 for the toggle that makes the chip attend to key on every other
// sample. Those last two decide when things happen rather than what, and a
// reimplementation without them has every envelope falling a few samples early
// or late and every note starting on the wrong one of two samples. Two pad
// bytes at the end.
//
// Version 8 is the same without regs_len, initial_regs and the per-frame
// register section.
// Version 7 is the same with a snapshot carrying no rate counter and no
// alternate-sample toggle -- the same forty-eight bytes, with those three
// reading as nought, which is why the version and not the length says whether
// they are there.
// Version 6 is the same with a forty-byte snapshot carrying no echo offset.
// Version 5 is the same without the audio snapshots -- it carried audio RAM and
// the sound registers, which is enough to watch the driver and not enough to
// continue it. Version 4 is version 5 without the two audio lengths, the two
// initial audio blocks and the two per-frame audio sections; version 3 is
// version 4 without save_len, initial_save and the per-frame save section;
// version 2 is version 3 without the two vram fields and the per-frame vram
// section. Readers accept all five.
//
// Registers is a,x,y,s,d as uint16 then db,p as uint8: twelve bytes. They are
// needed because routines take their arguments in registers as often as in
// memory -- the car routines are indexed by X, and a snapshot without it cannot
// say which car the call was for.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace RamTrace {

// The machine's registers at the moment a record was taken.
struct Snapshot {
  uint16_t a = 0, x = 0, y = 0, s = 0, d = 0;
  uint8_t db = 0, p = 0;
};

// The audio processor when a record was taken arrives here already laid out,
// as the thirty-two bytes described above: its RAM says what the sound driver
// has done, and this says what it is about to do and what time it thinks it
// is. SMP::snapshotForTrace fills it, which keeps the one place that knows
// that processor's insides the one place that knows them.
static constexpr unsigned AudioSnapshotBytes = 48;

// The display and transfer registers, as the game last left them.
//
// $2100-$21FF then $4200-$43FF, laid out the way the port lays them out so the
// two can be diffed offset for offset. There is nothing to read them out of:
// the PPU's registers are write-only on the real machine and the emulator's
// internal state is its own business, so this shadows the CPU's stores and
// that shadow *is* the domain.
//
// Deliberately not DMA or HDMA. Those are the same hardware writing the same
// addresses, but a channel walking a table down the screen leaves a register
// holding whatever the last line wanted, which says nothing about the frame
// and would differ between two correct implementations. What a port is
// answerable for here is the stores its own ported code performs, and a
// channel's table is a separate thing already carried in work RAM.
struct RegisterFile {
  static constexpr unsigned Bytes = 0x300;

  auto enabled() -> bool {
    if(state < 0) state = getenv("BSNES_TRACE_REGS") ? 1 : 0;
    return state == 1;
  }

  // The register blocks are mirrored into banks $00-$3F and $80-$BF; banks
  // $40-$7F and $C0-$FF reach cartridge or work RAM at the same offsets and
  // must not be folded in with them.
  auto note(unsigned address, uint8_t value) -> void {
    unsigned bank = address >> 16 & 0xff, within = address & 0xffff;
    if(bank >= 0x40 && bank < 0x80) return;
    if(bank >= 0xc0) return;
    if(within >= 0x2100 && within <= 0x21ff) bytes[within - 0x2100] = value;
    else if(within >= 0x4200 && within <= 0x43ff) bytes[0x100 + within - 0x4200] = value;
  }

  uint8_t bytes[Bytes] = {};
  int state = -1;
};

inline auto registerFile() -> RegisterFile& {
  static RegisterFile file;
  return file;
}

inline auto tracingRegisters() -> bool { return registerFile().enabled(); }
inline auto noteRegisterWrite(unsigned address, uint8_t value) -> void {
  registerFile().note(address, value);
}

struct Recorder {
  auto active() const -> bool { return file != nullptr; }

  // Called once per frame with work RAM as it stands at the end of it. The
  // first call establishes the starting state and emits no frame record.
  auto observe(const uint8_t* ram, unsigned length, const Snapshot& regs = {},
               const uint16_t* video = nullptr, const uint8_t* save = nullptr,
               unsigned saveSize = 0, const uint8_t* apu = nullptr,
               const uint8_t* dspRegs = nullptr, bool audioIsHonest = true,
               const uint8_t* audioRegs = nullptr) -> void {
    if(!enabled(length)) return;
    if(!started) {
      started = true;
      if(!open(length, ram, regs, video, save, saveSize, apu, dspRegs, audioIsHonest,
               audioRegs)) return;
      return;
    }
    if(!file) return;
    if(skip) { skip--; return; }
    writeFrame(ram, regs, video, save, apu, dspRegs, audioRegs);
  }

  // One button, as the SNES gamepad enum orders them. Accumulated until the
  // end of the frame, so a button held across the frame is recorded once.
  auto press(unsigned button) -> void {
    if(button < 12) input |= 1u << bit[button];
  }

  auto close() -> void {
    if(!file) return;
    fseek(file, 10, SEEK_SET);              // past magic, version, ram_len
    uint8_t count[4];
    write32(count, frames);
    fwrite(count, 1, 4, file);
    fclose(file);
    file = nullptr;
    delete[] shadow;
    shadow = nullptr;
    delete[] videoShadow;
    videoShadow = nullptr;
    delete[] saveShadow;
    saveShadow = nullptr;
    delete[] apuShadow;
    apuShadow = nullptr;
    delete[] dspShadow;
    dspShadow = nullptr;
    delete[] regsShadow;
    regsShadow = nullptr;
  }

private:
  // The gamepad enum is Up Down Left Right B A Y X L R Select Start; the
  // joypad register is B Y Select Start Up Down Left Right A X L R from bit 15
  // down. Record the register order, because that is what a port will read.
  static constexpr unsigned bit[12] = {11, 10, 9, 8, 15, 7, 14, 6, 5, 4, 13, 12};

  auto enabled(unsigned length) -> bool {
    return length && (file || !started);
  }

  static auto writeRegisters(uint8_t* p, const Snapshot& regs) -> void {
    write16(p, regs.a);
    write16(p + 2, regs.x);
    write16(p + 4, regs.y);
    write16(p + 6, regs.s);
    write16(p + 8, regs.d);
    p[10] = regs.db;
    p[11] = regs.p;
  }

  auto open(unsigned length, const uint8_t* ram, const Snapshot& regs,
            const uint16_t* video, const uint8_t* save, unsigned saveSize,
            const uint8_t* apu, const uint8_t* dspRegs, bool audioIsHonest,
            const uint8_t* audioRegs) -> bool {
    auto path = getenv("BSNES_TRACE_RAM");
    if(!path) return false;
    if(auto after = getenv("BSNES_TRACE_RAM_AFTER")) skip = (unsigned)atoi(after);
    file = fopen(path, "wb");
    if(!file) return false;

    len = length;
    shadow = new uint8_t[len];
    memcpy(shadow, ram, len);

    //Video memory only when asked for, and only if the caller had any to give.
    if(video && getenv("BSNES_TRACE_VRAM")) videoLen = VideoBytes;
    if(videoLen) {
      videoShadow = new uint8_t[videoLen];
      flatten(videoShadow, video);
    }

    //The cartridge's battery RAM, on the same terms.
    if(save && saveSize && getenv("BSNES_TRACE_SRAM")) saveLen = saveSize;
    if(saveLen) {
      saveShadow = new uint8_t[saveLen];
      memcpy(saveShadow, save, saveLen);
    }

    //And the audio processor's RAM with the sound chip's registers, which are
    //asked for and recorded together: neither says much without the other.
    //A recording whose echo buffer went somewhere other than audio RAM would
    //not match the machine, so say so and record nothing rather than record
    //something that cannot be checked.
    if(apu && dspRegs && getenv("BSNES_TRACE_APU")) {
      if(!audioIsHonest) {
        fprintf(stderr, "BSNES_TRACE_APU: refusing, the sound chip is not being run"
                        " as the hardware runs it\n");
      } else {
        apuLen = AudioBytes;
        dspLen = SoundRegisters;
      }
    }
    if(apuLen) {
      apuShadow = new uint8_t[apuLen];
      memcpy(apuShadow, apu, apuLen);
      dspShadow = new uint8_t[dspLen];
      memcpy(dspShadow, dspRegs, dspLen);
    }

    //And the display and transfer registers, which need nothing from the
    //caller: the shadow has been filling itself from the CPU's stores since
    //the machine was turned on.
    if(registerFile().enabled()) regsLen = RegisterFile::Bytes;
    if(regsLen) {
      regsShadow = new uint8_t[regsLen];
      memcpy(regsShadow, registerFile().bytes, regsLen);
    }

    uint8_t header[46] = {'F', 'Z', 'T', 'R'};
    write16(header + 4, 9);
    write32(header + 6, len);
    write32(header + 10, 0);                // patched by close()
    writeRegisters(header + 14, regs);
    write32(header + 26, videoLen);
    write32(header + 30, saveLen);
    write32(header + 34, apuLen);
    write32(header + 38, dspLen);
    write32(header + 42, regsLen);
    fwrite(header, 1, sizeof(header), file);
    fwrite(ram, 1, len, file);
    if(videoLen) fwrite(videoShadow, 1, videoLen, file);
    if(saveLen) fwrite(saveShadow, 1, saveLen, file);
    if(apuLen) fwrite(apuShadow, 1, apuLen, file);
    if(dspLen) fwrite(dspShadow, 1, dspLen, file);
    if(regsLen) fwrite(regsShadow, 1, regsLen, file);
    if(apuLen) writeAudioRegisters(audioRegs);
    atexit(closeAtExit);
    return true;
  }

  //Video memory is words in the emulator and bytes in the file, low byte
  //first, which is the order a transfer to $2118 and $2119 puts them in.
  static auto flatten(uint8_t* out, const uint16_t* video) -> void {
    for(unsigned n = 0; n < VideoBytes / 2; n++) {
      out[n * 2] = video[n] & 0xff;
      out[n * 2 + 1] = video[n] >> 8 & 0xff;
    }
  }

  auto writeFrame(const uint8_t* ram, const Snapshot& regs, const uint16_t* video,
                  const uint8_t* save, const uint8_t* apu, const uint8_t* dspRegs,
                  const uint8_t* audioRegs) -> void {
    // count first, so the record can be written in one pass
    uint32_t changed = 0;
    for(unsigned n = 0; n < len; n++) changed += ram[n] != shadow[n];

    uint8_t head[18];
    write16(head, (uint16_t)input);
    writeRegisters(head + 2, regs);
    write32(head + 14, changed);
    fwrite(head, 1, sizeof(head), file);

    for(unsigned n = 0; n < len; n++) {
      if(ram[n] == shadow[n]) continue;
      uint8_t delta[5];
      write32(delta, n);
      delta[4] = ram[n];
      fwrite(delta, 1, sizeof(delta), file);
      shadow[n] = ram[n];
    }
    if(videoLen) writeVideoDeltas(video);
    if(saveLen) writeDeltas(save, saveShadow, saveLen);
    if(apuLen) writeDeltas(apu, apuShadow, apuLen);
    if(dspLen) writeDeltas(dspRegs, dspShadow, dspLen);
    if(regsLen) writeDeltas(registerFile().bytes, regsShadow, regsLen);
    if(apuLen) writeAudioRegisters(audioRegs);
    frames++;
    input = 0;
  }

  //A record with no snapshot to give writes zeroes rather than skipping the
  //section, because the header has already said the section is there.
  auto writeAudioRegisters(const uint8_t* audioRegs) -> void {
    uint8_t blank[AudioSnapshotBytes] = {};
    fwrite(audioRegs ? audioRegs : blank, 1, AudioSnapshotBytes, file);
  }

  //One region's worth of changes: a count, then that many offset-and-value
  //pairs, then the shadow catches up. How busy a region is varies enormously -
  //battery RAM changes on almost no frame at all, the game writing it only when
  //a race is saved or a slot erased, while the sound registers change on nearly
  //every one - but the record is shaped the same for all of them.
  //
  //A caller with nothing to give writes an empty record rather than skipping
  //it, because the section's presence is fixed by the header.
  auto writeDeltas(const uint8_t* now, uint8_t* shadowed, unsigned length) -> void {
    uint32_t changed = 0;
    if(now) for(unsigned n = 0; n < length; n++) changed += now[n] != shadowed[n];
    uint8_t count[4];
    write32(count, changed);
    fwrite(count, 1, 4, file);
    if(!now) return;

    for(unsigned n = 0; n < length; n++) {
      if(now[n] == shadowed[n]) continue;
      uint8_t delta[5];
      write32(delta, n);
      delta[4] = now[n];
      fwrite(delta, 1, sizeof(delta), file);
      shadowed[n] = now[n];
    }
  }

  //The same delta pass over video memory. A frame of racing changes a few
  //hundred bytes of it, the same order as work RAM; a track load changes most
  //of it once.
  auto writeVideoDeltas(const uint16_t* video) -> void {
    static uint8_t* flat = nullptr;
    if(!flat) flat = new uint8_t[VideoBytes];
    if(video) flatten(flat, video); else memcpy(flat, videoShadow, videoLen);
    writeDeltas(flat, videoShadow, videoLen);
  }

  static auto write16(uint8_t* p, unsigned v) -> void {
    p[0] = v & 0xff;
    p[1] = v >> 8 & 0xff;
  }

  static auto write32(uint8_t* p, unsigned v) -> void {
    p[0] = v & 0xff;
    p[1] = v >> 8 & 0xff;
    p[2] = v >> 16 & 0xff;
    p[3] = v >> 24 & 0xff;
  }

  static auto closeAtExit() -> void;

  static constexpr unsigned VideoBytes = 64 * 1024;
  static constexpr unsigned AudioBytes = 64 * 1024;
  static constexpr unsigned SoundRegisters = 128;

  FILE* file = nullptr;
  uint8_t* shadow = nullptr;
  uint8_t* videoShadow = nullptr;
  unsigned videoLen = 0;
  uint8_t* saveShadow = nullptr;
  unsigned saveLen = 0;
  uint8_t* apuShadow = nullptr;
  unsigned apuLen = 0;
  uint8_t* dspShadow = nullptr;
  unsigned dspLen = 0;
  uint8_t* regsShadow = nullptr;
  unsigned regsLen = 0;
  unsigned len = 0;
  uint32_t frames = 0;
  unsigned input = 0;
  unsigned skip = 0;
  bool started = false;

public:
  //Which frame of the recording has been written. A picture dumped for
  //comparison has to be numbered the same way the recording is, or the two are
  //off by however many frames passed before recording began - and a comparison
  //of adjacent frames of a racing game looks like a renderer that is nearly
  //right rather than like a misalignment.
  auto recordedFrames() const -> uint32_t { return frames; }
  auto recording() const -> bool { return file != nullptr; }
};

// Which code writes a range of memory.
//
// Porting a subsystem starts with finding it, and the emulator already knows:
// whatever stores into the buffer is the routine. Cheaper and far more certain
// than reading a disassembly hoping to recognise it.
struct WriteWatch {
  auto enabled(const char* variable = "BSNES_WATCH_WRITE", bool reads = false,
               bool byPc = false) -> bool {
    if(state < 0) {
      state = 0;
      if(auto spec = getenv(variable)) {
        char* end = nullptr;
        low = (unsigned)strtoul(spec, &end, 16);
        if(end && *end == '-') high = (unsigned)strtoul(end + 1, nullptr, 16);
        if(high >= low) {
          counts = (uint32_t*)calloc(Space, sizeof(uint32_t));
          if(counts) {
            state = 1;
            inverted = byPc;
            kind = byPc ? "addresses read by" : reads ? "reads of" : "writes to";
            atexit(reads ? reportReadsAtExit : reportAtExit);
          }
        }
      }
    }
    return state == 1;
  }

  // Normally: which code touched this memory. Inverted: which memory this code
  // touched, which is how a routine's inputs get named without reading it.
  //
  // Counting into an array indexed by the whole 24-bit space rather than a list
  // of what has been seen. A list has to be searched on every memory access,
  // and this is called millions of times a second; it also silently truncates
  // once it fills, which turns "here is everything the routine reads" into
  // "here are the first N", a difference that does not announce itself.
  auto note(unsigned address, unsigned pc) -> void {
    unsigned key = inverted ? address : pc;
    unsigned filter = inverted ? pc : address;
    if(filter < low || filter > high) return;
    //when asking what a routine reads, its own bytes are instruction fetches
    //rather than inputs, and they would crowd out everything worth seeing
    if(inverted && address >= low && address <= high) return;
    auto& count = counts[key & (Space - 1)];
    if(!count) seen++;
    count++;
  }

  auto report() -> void {
    if(state != 1 || !seen) return;
    fprintf(stderr, "[watch] %s %06x-%06x: %u distinct addresses\n", kind, low, high, seen);
    inverted ? reportRegions() : reportSites();
  }

private:
  static constexpr unsigned Space = 1u << 24;

  // The answer to "what does this code read" is thousands of addresses in runs,
  // so print the runs. Which class of memory they land in is the first thing
  // worth knowing: cartridge means the routine decodes data that an asset
  // extractor would have to pull out, work RAM means it does not.
  auto reportRegions() -> void {
    unsigned cartridge = 0, ram = 0, other = 0;
    for(unsigned n = 0; n < Space; n++) {
      if(!counts[n]) continue;
      auto bank = n >> 16, within = n & 0xffff;
      bool rom = (bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) ? within >= 0x8000 : bank >= 0xc0;
      bool wram = bank == 0x7e || bank == 0x7f
               || ((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && within < 0x2000);
      wram ? ram++ : rom ? cartridge++ : other++;
    }
    fprintf(stderr, "  cartridge %u, work RAM %u, other %u\n", cartridge, ram, other);

    unsigned printed = 0;
    for(unsigned n = 0; n < Space; ) {
      if(!counts[n]) { n++; continue; }
      unsigned first = n, hits = 0;
      //runs stop at bank boundaries, or a region would print with a start and
      //end in different banks and read as nonsense
      while(n < Space && counts[n] && (n >> 16) == (first >> 16)) hits += counts[n++];
      if(printed++ < Limit) {
        fprintf(stderr, "  %02x:%04x-%04x  %u bytes, %u reads\n",
          first >> 16 & 0xff, first & 0xffff, (n - 1) & 0xffff, n - first, hits);
      }
    }
    if(printed > Limit) fprintf(stderr, "  ... and %u more regions\n", printed - Limit);
  }

  auto reportSites() -> void {
    for(unsigned n = 0; n < Space; n++) {
      if(!counts[n]) continue;
      fprintf(stderr, "  %02x:%04x  %u times\n", n >> 16 & 0xff, n & 0xffff, counts[n]);
    }
  }

  static constexpr unsigned Limit = 400;
public:
  static auto reportAtExit() -> void;
  static auto reportReadsAtExit() -> void;

  uint32_t* counts = nullptr;
  const char* kind = "writes to";
  unsigned seen = 0, low = 0, high = 0;
  bool inverted = false;
  int state = -1;
};

// Which bytes of the cartridge are code, and what operand widths they ran with.
//
// A 65816 cannot be disassembled reliably from the ROM alone. Operand width is
// not in the encoding — the same three bytes are one instruction with a 16-bit
// accumulator and two with an 8-bit one — and code sits interleaved with
// compressed data that disassembles into convincing nonsense. A static sweep
// produces something that looks right and is silently wrong.
//
// Execution settles both questions. Every instruction that runs is code, and
// the flags at the time are the widths it ran with. One playthrough turns
// guesswork into an exact map, and the disassembler can then decode only real
// code, correctly.
struct Coverage {
  static constexpr unsigned Executed = 1, M8 = 2, M16 = 4, X8 = 8, X16 = 16, Read = 32;

  auto enabled() -> bool {
    if(state < 0) {
      state = 0;
      if(auto path = getenv("BSNES_TRACE_EXEC")) {
        map = (uint8_t*)calloc(Space, 1);
        if(map) { state = 1; target = path; atexit(writeAtExit); }
      }
    }
    return state == 1;
  }

  auto note(unsigned pc, bool m8, bool x8) -> void {
    auto& flags = map[pc & (Space - 1)];
    flags |= Executed | (m8 ? M8 : M16) | (x8 ? X8 : X16);
  }

  // Every byte the cartridge is read for, instruction fetches included. What is
  // read but is not part of any decoded instruction is the game's data, which
  // is what an extractor has to pull out; separating the two is left to the
  // reader, which knows how long each instruction turned out to be.
  auto noteRead(unsigned address) -> void {
    unsigned bank = address >> 16 & 0xff;
    bool cartridge = (bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && (address & 0xffff) >= 0x8000;
    if(cartridge || bank >= 0xc0) map[address & (Space - 1)] |= Read;
  }

  auto write() -> void {
    if(state != 1 || !map) return;
    auto file = fopen(target, "wb");
    if(!file) return;
    //every address with anything recorded about it goes in the file, but only
    //some of them are code: the rest were read as data. Reporting the total as
    //"executed" overstates it by an order of magnitude, which is misleading
    //when the number is being used to judge how much of the game has been seen.
    unsigned count = 0, ran = 0;
    for(unsigned n = 0; n < Space; n++) {
      count += map[n] != 0;
      ran += (map[n] & Executed) != 0;
    }

    uint8_t header[10] = {'F', 'Z', 'C', 'V'};
    header[4] = 1; header[5] = 0;
    header[6] = count & 0xff; header[7] = count >> 8 & 0xff;
    header[8] = count >> 16 & 0xff; header[9] = count >> 24 & 0xff;
    fwrite(header, 1, sizeof(header), file);
    for(unsigned n = 0; n < Space; n++) {
      if(!map[n]) continue;
      uint8_t record[5] = {(uint8_t)(n & 0xff), (uint8_t)(n >> 8 & 0xff),
                           (uint8_t)(n >> 16 & 0xff), 0, map[n]};
      fwrite(record, 1, sizeof(record), file);
    }
    fclose(file);
    fprintf(stderr, "[coverage] %u instructions executed, %u addresses touched, written to %s\n",
      ran, count, target);
  }

private:
  static constexpr unsigned Space = 1u << 24;   //the whole 24-bit address space
  static auto writeAtExit() -> void;

  uint8_t* map = nullptr;
  const char* target = nullptr;
  int state = -1;
};

inline auto coverage() -> Coverage& {
  static Coverage instance;
  return instance;
}

inline auto Coverage::writeAtExit() -> void { coverage().write(); }

inline auto recorder() -> Recorder&;   //defined below; it owns the frame count

//BSNES_TRACE_EXEC_FRAMES="a-b,c-d,...": record coverage only while the frame
//being recorded falls inside one of these ranges, both ends included. Unset
//records every frame, which is what it has always done.
//
//A map covers whatever the run covered, so a verification that compares only
//part of a recording cannot honestly count a map that covers all of it - it
//would be claiming frames nobody checked. The long Grand Prix run is the case
//that matters: thirty-nine thousand of its forty thousand frames are compared
//byte for byte, and a handful in the middle cannot be, because the machine did
//not finish them inside a video frame. Told which ranges were compared, the
//recorder writes a map of exactly those.
inline auto coverageWanted(unsigned frame) -> bool {
  static int parsed = -1;
  static unsigned low[256], high[256], count = 0;
  if(parsed < 0) {
    parsed = 0;
    if(auto text = getenv("BSNES_TRACE_EXEC_FRAMES")) {
      parsed = 1;
      const char* p = text;
      while(*p && count < 256) {
        unsigned first = (unsigned)strtoul(p, (char**)&p, 10);
        unsigned last = first;
        if(*p == '-') { p++; last = (unsigned)strtoul(p, (char**)&p, 10); }
        low[count] = first; high[count] = last; count++;
        while(*p && *p != ',') p++;
        if(*p == ',') p++;
      }
    }
  }
  if(parsed == 0) return true;
  //The frame this counts is the one the RAM trace is writing, which is
  //deliberate: it is the same numbering fzero-verify and tools/segments.py
  //use, and a frame counter of its own would be free to disagree with them.
  //The cost is that without BSNES_TRACE_RAM the count never moves and every
  //instruction looks like frame nought, so say so rather than quietly writing
  //a map of the wrong thing.
  if(!getenv("BSNES_TRACE_RAM")) {
    static bool said = false;
    if(!said) {
      said = true;
      fprintf(stderr, "BSNES_TRACE_EXEC_FRAMES needs BSNES_TRACE_RAM: "
                      "the frames it counts are the ones being recorded\n");
    }
    return false;
  }
  for(unsigned i = 0; i < count; i++) if(frame >= low[i] && frame <= high[i]) return true;
  return false;
}

inline auto bracketCovers(unsigned pc) -> bool;   //defined below, with the trigger

//BSNES_TRACE_EXEC_BRACKET=1: with BSNES_TRACE_RAM_PAIRED, record coverage only
//between the entry and the exit - the instructions a paired comparison
//actually compares. Everything else the recording runs is outside the bracket
//and outside what the check proves.
inline auto coverageBracketed() -> bool {
  static int on = -1;
  if(on < 0) {
    on = getenv("BSNES_TRACE_EXEC_BRACKET") ? 1 : 0;
    if(on && !getenv("BSNES_TRACE_RAM_PAIRED")) {
      fprintf(stderr, "BSNES_TRACE_EXEC_BRACKET needs BSNES_TRACE_RAM_PAIRED: "
                      "without a pair there is no bracket to stay inside\n");
      exit(1);
    }
  }
  return on == 1;
}

inline auto tracingExec() -> bool {
  return coverage().enabled() && coverageWanted(recorder().recordedFrames());
}
//An interrupt taken between a paired check's entry and its exit runs inside
//the bracket in time and outside it in every other sense: the check does not
//compare what the handler wrote (fzero-rs watches the whole of work RAM less
//the raster chain's continuation pointer for exactly this reason), so the
//handler's instructions must not be credited to it. Measured before this
//existed: the tow-pit map carried 26 starts of the IRQ raster chain among its
//278, and the three player-progress brackets carried the NMI handler and
//everything it calls - four to seven thousand starts for a routine of a
//hundred bytes. The stack pointer says where the handler is: it is below the
//level the interrupt was taken at until the rti pops it back, and no code
//outside the handler can run below that level in between.
//
//The level has to be re-read at *every* instruction start, not only at the
//ones a bracket covers: the first version asked only inside the bracket, so a
//level captured at an interrupt taken from the main loop was never cleared by
//the rti's successor (outside any bracket), and a routine the main loop later
//called - three bytes deeper on the stack, as every jsr is - sat "below the
//handler" for the rest of the run. Both bracket maps came out empty.
struct InterruptDepth {
  auto entered(unsigned s) -> void { if(!inside) { inside = true; level = s; } }
  //asked at every instruction start: the rti's successor runs at `level` again
  auto update(unsigned s) -> void { if(inside && s >= level) inside = false; }
  auto within() const -> bool { return inside; }
private:
  bool inside = false;
  unsigned level = 0;
};
inline auto interruptDepth() -> InterruptDepth& { static InterruptDepth d; return d; }
inline auto noteInterrupt(unsigned s) -> void { interruptDepth().entered(s); }
inline auto noteStack(unsigned s) -> void { interruptDepth().update(s); }

inline auto noteExec(unsigned pc, bool m8, bool x8) -> void {
  if(coverageBracketed() && (!bracketCovers(pc) || interruptDepth().within())) return;
  coverage().note(pc, m8, x8);
}
inline auto noteRead(unsigned address) -> void { coverage().noteRead(address); }

inline auto watch() -> WriteWatch& {
  static WriteWatch instance;
  return instance;
}

// The same question asked of reads: which code consumes a range. Between the
// two, a routine's inputs and outputs can be named without reading a line of
// its disassembly.
inline auto readWatch() -> WriteWatch& {
  static WriteWatch instance;
  return instance;
}

inline auto WriteWatch::reportAtExit() -> void { watch().report(); }

inline auto watching() -> bool { return watch().enabled(); }
inline auto watchWrite(unsigned address, unsigned pc) -> void { watch().note(address, pc); }
inline auto watchingReads() -> bool { return readWatch().enabled("BSNES_WATCH_READ", true); }
inline auto watchRead(unsigned address, unsigned pc) -> void { readWatch().note(address, pc); }

// Optional main-CPU timing sidecar; no change to the RAM trace format.
// BSNES_TRACE_TIMING=<csv> BSNES_TRACE_TIMING_AT=0089d5,0088ff,...
// Requires ordinary frame-based BSNES_TRACE_RAM so the frame labels agree
// with screenshots. master_clock is the wrapping 32-bit CPU master counter.
struct TimingTrace {
  auto enabled() -> bool {
    if(state >= 0) return state == 1;
    state = 0;
    auto path = getenv("BSNES_TRACE_TIMING");
    if(!path) return false;
    auto list = getenv("BSNES_TRACE_TIMING_AT");
    if(!list || !*list || !getenv("BSNES_TRACE_RAM") || getenv("BSNES_TRACE_RAM_AT")) {
      fprintf(stderr, "[timing] requires BSNES_TRACE_TIMING_AT and frame-based BSNES_TRACE_RAM\n");
      exit(2);
    }
    while(*list) {
      char* end = nullptr;
      auto pc = strtoul(list, &end, 16);
      if(end == list || pc > 0xffffff || count == 128 || (*end && *end != ',')) {
        fprintf(stderr, "[timing] invalid address list\n");
        exit(2);
      }
      addresses[count++] = pc;
      if(!*end) break;
      list = end + 1;
      if(!*list) { fprintf(stderr, "[timing] empty final address\n"); exit(2); }
    }
    file = fopen(path, "w");
    if(!file) { fprintf(stderr, "[timing] cannot open output\n"); exit(2); }
    fprintf(file, "frame,master_clock,scanline,hclock,pc\n");
    state = 1;
    return true;
  }
  auto note(unsigned pc, unsigned clocks, unsigned line, unsigned horizontal) -> void {
    for(unsigned i = 0; i < count; i++) if(addresses[i] == pc) {
      fprintf(file, "%u,%u,%u,%u,%06x\n", recorder().recordedFrames(), clocks, line, horizontal, pc);
      fflush(file);
      return;
    }
  }
  ~TimingTrace() { if(file) fclose(file); }
private:
  FILE* file = nullptr;
  int state = -1;
  unsigned addresses[128] = {}, count = 0;
};
inline auto timingTrace() -> TimingTrace& { static TimingTrace trace; return trace; }

// BSNES_WATCH_READS_BY=039243-039488: what a stretch of code reads.
inline auto inputWatch() -> WriteWatch& {
  static WriteWatch instance;
  return instance;
}
inline auto watchingInputs() -> bool {
  return inputWatch().enabled("BSNES_WATCH_READS_BY", true, true);
}
inline auto watchInput(unsigned address, unsigned pc) -> void { inputWatch().note(address, pc); }

inline auto WriteWatch::reportReadsAtExit() -> void { readWatch().report(); inputWatch().report(); }

// Where each DMA block comes from and where it lands.
//
// A ROM byte that is read but never executed could be graphics, a palette, a
// map, music or a lookup table, and reads alone cannot tell them apart. A DMA
// says: this range went to that port. $2118 is VRAM, $2122 is CGRAM, $2104 is
// OAM, $2140-$2143 is the audio CPU. That is evidence about what the bytes are,
// not merely that something touched them.
//
//   BSNES_TRACE_DMA=/path/to/log
//
// One line per transfer, aggregated afterwards. Work RAM sources are recorded
// too: graphics expanded into $7F0000 and sent on from there would otherwise
// leave no trace of where they ended up.
struct DmaTrace {
  auto enabled() -> bool {
    if(state < 0) {
      state = 0;
      if(auto path = getenv("BSNES_TRACE_DMA")) {
        file = fopen(path, "w");
        if(file) { state = 1; atexit(closeAtExit); }
      }
    }
    return state == 1;
  }

  //`target` is where a transfer lands beyond the port itself: for a VRAM write
  //that is the word address in $2116, which is the only way to know which part
  //of the tilemap a strip belongs to.
  auto note(unsigned source, unsigned length, unsigned port, unsigned direction,
            unsigned mode, bool fixed, unsigned target = 0, unsigned pc = 0) -> void {
    if(!file) return;
    //log every transfer, cartridge or not: a block staged in work RAM and sent
    //on from there is how expanded graphics reach VRAM, and dropping those
    //hides the second half of the chain
    fprintf(file, "%06x %u %04x %u %u %u %04x %06x\n",
            source & 0xffffff, length ? length : 0x10000, port, direction, mode, fixed,
            target & 0xffff, pc & 0xffffff);
  }

  auto close() -> void { if(file) { fclose(file); file = nullptr; } }

private:
  static auto closeAtExit() -> void;
  FILE* file = nullptr;
  int state = -1;
};

inline auto dmaTrace() -> DmaTrace& {
  static DmaTrace instance;
  return instance;
}
inline auto DmaTrace::closeAtExit() -> void { dmaTrace().close(); }
inline auto tracingDma() -> bool { return dmaTrace().enabled(); }
inline auto noteDma(unsigned source, unsigned length, unsigned port,
                    unsigned direction, unsigned mode, bool fixed,
                    unsigned target = 0, unsigned pc = 0) -> void {
  dmaTrace().note(source, length, port, direction, mode, fixed, target, pc);
}

inline auto recorder() -> Recorder& {
  static Recorder instance;
  return instance;
}

// Snapshot RAM at a routine's entry rather than at the end of a frame.
//
// End-of-frame RAM cannot supply a mid-frame routine's inputs. A routine reads
// variables that later code in the same frame overwrites, so by the time the
// frame ends the values it actually ran on are gone -- and a port fed the
// end-of-frame values computes something the original never computed. The
// course streamer is the case that forced this: its cursor is set up just
// before it runs and reset just after, and neither the previous frame's RAM nor
// this frame's contains what it read.
//
// Triggering the same recorder on a program counter instead fixes that. Each
// record is the machine exactly as the routine found it, so a port can be fed
// its true inputs, and the record that follows holds what the routine produced.
//
// Give two program counters and the records alternate: the routine's entry,
// then wherever it has finished. That is usually necessary rather than a
// refinement, because a routine's outputs are often scratch that later code in
// the same frame reuses -- comparing them at the next entry compares whatever
// overwrote them. The instruction after the call site works as an exit.
//
//   BSNES_TRACE_RAM_AT=03939e            entry only
//   BSNES_TRACE_RAM_AT=039268,0392aa     entry and exit, alternating
// Optional paired mode records pcs[0] followed by pcs[1], ignoring unrelated
// visits to the exit. Nested/re-entered brackets fail rather than mispairing.
// Use unique boundaries for a non-recursive routine; this is not a call tracer.
struct EntryTrigger {
  auto enabled() -> bool {
    if(state < 0) {
      state = 0;
      if(auto spec = getenv("BSNES_TRACE_RAM_AT")) {
        for(auto p = spec; *p && count < Max;) {
          char* end = nullptr;
          pcs[count] = (unsigned)strtoul(p, &end, 16);
          if(end == p) break;
          count++;
          p = *end == ',' ? end + 1 : end;
        }
        if(count) state = 1;
        if(auto value = getenv("BSNES_TRACE_RAM_PAIRED")) paired = strcmp(value, "1") == 0;
        if(paired && (count != 2 || pcs[0] == pcs[1])) {
          fprintf(stderr, "[ramtrace] paired mode requires two distinct PCs\n");
          exit(1);
        }
      }
    }
    return state == 1;
  }

  //Whether a paired comparison covers this instruction: everything from the
  //entry up to but not including the exit, which is the instruction after the
  //call site and belongs to the caller. Asked before matches() has updated
  //`inside`, so the entry has to be named rather than inferred.
  auto covers(unsigned address) -> bool {
    if(!paired) return false;
    if(address == pcs[0]) return true;
    if(address == pcs[1]) return false;
    return inside;
  }

  auto matches(unsigned address) -> bool {
    if(paired) {
      if(address == pcs[0]) {
        if(inside) {
          fprintf(stderr, "[ramtrace] repeated entry before paired exit\n");
          exit(1);
        }
        inside = true;
        return true;
      }
      if(address == pcs[1] && inside) { inside = false; return true; }
      return false;
    }
    for(unsigned n = 0; n < count; n++) if(pcs[n] == address) return true;
    return false;
  }

private:
  static constexpr unsigned Max = 8;
  bool paired = false, inside = false;
  unsigned pcs[Max] = {};
  unsigned count = 0;
  int state = -1;
};

inline auto entryTrigger() -> EntryTrigger& {
  static EntryTrigger instance;
  return instance;
}

inline auto snapshotting() -> bool { return entryTrigger().enabled(); }
inline auto bracketCovers(unsigned pc) -> bool { return entryTrigger().covers(pc); }
inline auto atEntry(unsigned pc) -> bool { return entryTrigger().matches(pc); }

inline auto Recorder::closeAtExit() -> void { recorder().close(); }

}
