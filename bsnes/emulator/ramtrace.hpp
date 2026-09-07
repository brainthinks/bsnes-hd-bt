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
// Format, little-endian, matching the reader in the fzero-rs project:
//   char   magic[4]     "FZTR"
//   uint16 version      1
//   uint32 ram_len      0x20000
//   uint32 frame_count  patched on close
//   Registers initial   the machine when the first record was taken
//   uint8  initial[ram_len]
//   per frame:
//     uint16 input      controller 1, in $4218 bit order
//     Registers regs    the machine when this record was taken
//     uint32 delta_count
//     per delta: uint32 offset, uint8 value
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

struct Recorder {
  auto active() const -> bool { return file != nullptr; }

  // Called once per frame with work RAM as it stands at the end of it. The
  // first call establishes the starting state and emits no frame record.
  auto observe(const uint8_t* ram, unsigned length, const Snapshot& regs = {}) -> void {
    if(!enabled(length)) return;
    if(!started) {
      started = true;
      if(!open(length, ram, regs)) return;
      return;
    }
    if(!file) return;
    if(skip) { skip--; return; }
    writeFrame(ram, regs);
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

  auto open(unsigned length, const uint8_t* ram, const Snapshot& regs) -> bool {
    auto path = getenv("BSNES_TRACE_RAM");
    if(!path) return false;
    if(auto after = getenv("BSNES_TRACE_RAM_AFTER")) skip = (unsigned)atoi(after);
    file = fopen(path, "wb");
    if(!file) return false;

    len = length;
    shadow = new uint8_t[len];
    memcpy(shadow, ram, len);

    uint8_t header[26] = {'F', 'Z', 'T', 'R'};
    write16(header + 4, 2);
    write32(header + 6, len);
    write32(header + 10, 0);                // patched by close()
    writeRegisters(header + 14, regs);
    fwrite(header, 1, sizeof(header), file);
    fwrite(ram, 1, len, file);
    atexit(closeAtExit);
    return true;
  }

  auto writeFrame(const uint8_t* ram, const Snapshot& regs) -> void {
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
    frames++;
    input = 0;
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

  FILE* file = nullptr;
  uint8_t* shadow = nullptr;
  unsigned len = 0;
  uint32_t frames = 0;
  unsigned input = 0;
  unsigned skip = 0;
  bool started = false;
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

inline auto tracingExec() -> bool { return coverage().enabled(); }
inline auto noteExec(unsigned pc, bool m8, bool x8) -> void { coverage().note(pc, m8, x8); }
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
inline auto atEntry(unsigned pc) -> bool { return entryTrigger().matches(pc); }

inline auto Recorder::closeAtExit() -> void { recorder().close(); }

}
