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
//   uint8  initial[ram_len]
//   per frame:
//     uint16 input      controller 1, in $4218 bit order
//     uint32 delta_count
//     per delta: uint32 offset, uint8 value

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace RamTrace {

struct Recorder {
  auto active() const -> bool { return file != nullptr; }

  // Called once per frame with work RAM as it stands at the end of it. The
  // first call establishes the starting state and emits no frame record.
  auto observe(const uint8_t* ram, unsigned length) -> void {
    if(!enabled(length)) return;
    if(!started) {
      started = true;
      if(!open(length, ram)) return;
      return;
    }
    if(!file) return;
    if(skip) { skip--; return; }
    writeFrame(ram);
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

  auto open(unsigned length, const uint8_t* ram) -> bool {
    auto path = getenv("BSNES_TRACE_RAM");
    if(!path) return false;
    if(auto after = getenv("BSNES_TRACE_RAM_AFTER")) skip = (unsigned)atoi(after);
    file = fopen(path, "wb");
    if(!file) return false;

    len = length;
    shadow = new uint8_t[len];
    memcpy(shadow, ram, len);

    uint8_t header[14] = {'F', 'Z', 'T', 'R'};
    write16(header + 4, 1);
    write32(header + 6, len);
    write32(header + 10, 0);                // patched by close()
    fwrite(header, 1, sizeof(header), file);
    fwrite(ram, 1, len, file);
    atexit(closeAtExit);
    return true;
  }

  auto writeFrame(const uint8_t* ram) -> void {
    // count first, so the record can be written in one pass
    uint32_t changed = 0;
    for(unsigned n = 0; n < len; n++) changed += ram[n] != shadow[n];

    uint8_t head[6];
    write16(head, (uint16_t)input);
    write32(head + 2, changed);
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
  auto enabled() -> bool {
    if(state < 0) {
      state = 0;
      if(auto spec = getenv("BSNES_WATCH_WRITE")) {
        char* end = nullptr;
        low = (unsigned)strtoul(spec, &end, 16);
        if(end && *end == '-') high = (unsigned)strtoul(end + 1, nullptr, 16);
        if(high >= low) { state = 1; atexit(reportAtExit); }
      }
    }
    return state == 1;
  }

  auto note(unsigned address, unsigned pc) -> void {
    if(address < low || address > high) return;
    for(unsigned n = 0; n < seen; n++) {
      if(sites[n].pc == pc) { sites[n].count++; return; }
    }
    if(seen < Max) sites[seen++] = {pc, 1};
  }

  auto report() -> void {
    if(state != 1 || !seen) return;
    fprintf(stderr, "[watch] writes to %06x-%06x came from %u places:\n", low, high, seen);
    for(unsigned n = 0; n < seen; n++) {
      fprintf(stderr, "  %02x:%04x  %u writes\n",
        sites[n].pc >> 16 & 0xff, sites[n].pc & 0xffff, sites[n].count);
    }
  }

private:
  static constexpr unsigned Max = 64;
  struct Site { unsigned pc, count; };
  static auto reportAtExit() -> void;

  Site sites[Max] = {};
  unsigned seen = 0, low = 0, high = 0;
  int state = -1;
};

inline auto watch() -> WriteWatch& {
  static WriteWatch instance;
  return instance;
}

inline auto WriteWatch::reportAtExit() -> void { watch().report(); }

inline auto watching() -> bool { return watch().enabled(); }
inline auto watchWrite(unsigned address, unsigned pc) -> void { watch().note(address, pc); }

inline auto recorder() -> Recorder& {
  static Recorder instance;
  return instance;
}

inline auto Recorder::closeAtExit() -> void { recorder().close(); }

}
