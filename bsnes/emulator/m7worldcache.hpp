#pragma once

// Mode 7 tilemap entries remembered by world coordinate.
//
// A game that streams its course keeps only 128x128 tiles resident and rewrites
// them as the player moves, so the widescreen extension reaches past what is
// loaded and the hardware wraps it. Everything the game has already streamed
// went through VRAM, though, so it can simply be kept.
//
// The one thing that cannot be worked out from the stream is where each write
// belongs in the world -- the map is a ring buffer and the same slot is reused
// for different places. That mapping is supplied, not inferred: a per-game
// descriptor names the memory holding the player's world position, and
// everything here is indexed by the coordinate that yields. Nothing in this
// file knows which game it is looking at, and no tile data is ever stored
// anywhere but memory: the tiles come from the ROM at run time, through the
// game's own decoder.
//
// Open addressing with linear probing, power-of-two capacity. A full table
// evicts rather than growing: a missing entry only means the caller falls back
// to what the hardware would have done.

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace HdToolkit {

struct Mode7WorldCache {
  ~Mode7WorldCache() { release(); }

  auto ready() const -> bool { return slots != nullptr; }
  auto count() const -> unsigned { return used; }
  auto capacity() const -> unsigned { return mask ? mask + 1 : 0; }

  auto release() -> void {
    delete[] slots;
    slots = nullptr;
    mask = 0;
    used = 0;
  }

  auto reserve(unsigned capacityLog2) -> void {
    release();
    if(capacityLog2 < 4) capacityLog2 = 4;
    if(capacityLog2 > 24) capacityLog2 = 24;
    unsigned n = 1u << capacityLog2;
    slots = new Slot[n];
    mask = n - 1;
    used = 0;
  }

  auto clear() -> void {
    if(!slots) return;
    for(unsigned n = 0; n <= mask; n++) slots[n].live = false;
    used = 0;
  }

  auto record(int worldTileX, int worldTileY, unsigned tile) -> void {
    if(!slots) return;
    uint64_t key = pack(worldTileX, worldTileY);
    unsigned at = (unsigned)(hash(key) & mask);
    for(unsigned step = 0; step < Probe; step++) {
      auto& slot = slots[(at + step) & mask];
      if(!slot.live) {
        slot = {key, (uint8_t)tile, true};
        used++;
        return;
      }
      if(slot.key == key) { slot.tile = (uint8_t)tile; return; }
    }
    //crowded: take the last slot probed. Evicting costs a fallback, not a bug.
    slots[(at + Probe - 1) & mask] = {key, (uint8_t)tile, true};
  }

  auto lookup(int worldTileX, int worldTileY, unsigned& tile) const -> bool {
    if(!slots) return false;
    uint64_t key = pack(worldTileX, worldTileY);
    unsigned at = (unsigned)(hash(key) & mask);
    for(unsigned step = 0; step < Probe; step++) {
      auto& slot = slots[(at + step) & mask];
      if(!slot.live) return false;
      if(slot.key == key) { tile = slot.tile; return true; }
    }
    return false;
  }

private:
  static constexpr unsigned Probe = 8;

  struct Slot {
    uint64_t key = 0;
    uint8_t tile = 0;
    bool live = false;
  };

  static auto pack(int x, int y) -> uint64_t {
    return (uint64_t)(uint32_t)x << 32 | (uint32_t)y;
  }

  static auto hash(uint64_t key) -> uint64_t {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33;
    return key;
  }

  Slot* slots = nullptr;
  unsigned mask = 0;
  unsigned used = 0;
};

// Where a game keeps the player's world position. Per-game *data*, never code:
// two addresses and the constant each value sits above the world coordinate.
// The emulator reads the numbers and does not know what game they describe.
struct Mode7WorldOrigin {
  bool valid = false;
  unsigned addressX = 0, addressY = 0;   // 24-bit CPU addresses, WRAM
  int deltaX = 0, deltaY = 0;            // value + delta == the world coordinate

  // "7e00a8-2688,7e00aa-3504": read 16 bits at $7E:00A8, subtract 2688.
  auto parse(const char* text) -> bool {
    valid = false;
    if(!text) return false;
    unsigned ax = 0, ay = 0;
    long dx = 0, dy = 0;
    if(!field(text, ax, dx)) return false;
    auto comma = strchr(text, ',');
    if(!comma || !field(comma + 1, ay, dy)) return false;
    addressX = ax; addressY = ay;
    deltaX = (int)dx; deltaY = (int)dy;
    valid = true;
    return true;
  }

  // `read16` gives the emulator's view of CPU memory at an address.
  template<typename Read> auto worldX(Read read16) const -> int {
    return (int)read16(addressX) + deltaX;
  }
  template<typename Read> auto worldY(Read read16) const -> int {
    return (int)read16(addressY) + deltaY;
  }

private:
  static auto field(const char* text, unsigned& address, long& delta) -> bool {
    char* end = nullptr;
    address = (unsigned)strtoul(text, &end, 16);
    if(end == text) return false;
    delta = 0;
    if(*end == '-' || *end == '+') delta = strtol(end, &end, 10);
    return true;
  }
};

}
