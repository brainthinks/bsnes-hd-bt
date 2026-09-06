#pragma once

// An emulator-side Mode 7 tilemap larger than the 128x128 tiles the hardware
// can address.
//
// Mode 7 addressing is seven bits per axis (`pixelX >> 3 & 127`), so a game can
// only ever have 1024x1024 pixels of map resident and must stream the rest --
// F-Zero rewrites a sixth to a third of its map as you drive. In widescreen the
// extra columns reach past that window and the hardware wraps them, which is
// where the repeated track comes from. Measured on F-Zero, the visible floor
// reaches about 2.1 map widths at the topmost Mode 7 scanline and under one map
// below the middle of the screen, so a 2x or 4x map covers it.
//
// The extra tiles cannot come from VRAM (Mode 7 already occupies its low half,
// interleaved) and cannot be recovered from the streamed history (successive
// map states do not align under any translation). They have to be supplied. A
// ROM hack would stream them through a side channel along with the origin of
// the hardware's window within the larger map; this file is the rendering half
// of that, plus a file format so the result can be seen before any of the game
// side exists.
//
// Tile *pixels* still come from VRAM: the extended map holds tile indices only,
// and the 256 shared tiles are plenty (F-Zero uses 60 of them).
//
// File format, little-endian:
//   char     magic[4]  "M7XM"
//   uint16   version   1
//   uint16   tilesW    128..512, a multiple of 128
//   uint16   tilesH    likewise
//   uint16   originX   tile column where the hardware map's x=0 sits
//   uint16   originY   tile row where the hardware map's y=0 sits
//   uint16   entries[tilesH * tilesW]   tile index, or 0xffff for "not authored"

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace HdToolkit {

struct Mode7ExtendedMap {
  static constexpr unsigned Unmapped = 0xffff;
  static constexpr unsigned MinTiles = 128;
  static constexpr unsigned MaxTiles = 512;

  ~Mode7ExtendedMap() { unload(); }

  auto loaded() const -> bool { return entries != nullptr; }
  auto width() const -> unsigned { return tilesW; }
  auto height() const -> unsigned { return tilesH; }

  auto unload() -> void {
    delete[] entries;
    entries = nullptr;
    tilesW = tilesH = originX = originY = 0;
  }

  auto load(const char* path) -> bool {
    unload();
    auto file = fopen(path, "rb");
    if(!file) return false;
    uint8_t header[14];
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header)
           && !memcmp(header, "M7XM", 4)
           && read16(header + 4) == 1;
    unsigned w = ok ? read16(header + 6) : 0;
    unsigned h = ok ? read16(header + 8) : 0;
    ok = ok && valid(w) && valid(h);
    if(ok) {
      unsigned count = w * h;
      auto data = new uint16_t[count];
      for(unsigned n = 0; ok && n < count; n++) {
        uint8_t word[2];
        if(fread(word, 1, 2, file) != 2) ok = false;
        else data[n] = (uint16_t)read16(word);
      }
      if(ok) {
        entries = data;
        tilesW = w;
        tilesH = h;
        originX = read16(header + 10) % w;
        originY = read16(header + 12) % h;
      } else {
        delete[] data;
      }
    }
    fclose(file);
    return loaded();
  }

  // pixelX/pixelY are hardware-map pixel coordinates. The extended map answers
  // only where the sample has left the hardware's 1024x1024 window -- exactly
  // where the hardware would wrap. Inside the window VRAM always wins, so the
  // live game keeps drawing itself and a stale file cannot corrupt the picture.
  // Returns false wherever the map holds nothing or does not reach, so an
  // unauthored area renders exactly as it does today.
  auto lookup(int pixelX, int pixelY, unsigned& tile) const -> bool {
    if(!loaded()) return false;
    if(!((pixelX | pixelY) & ~1023)) return false;
    int w = (int)(tilesW * 8), h = (int)(tilesH * 8);
    int x = pixelX + (int)originX * 8;
    int y = pixelY + (int)originY * 8;
    //strictly additive: past its own edge the map defers rather than wrapping,
    //so anything it does not cover still renders exactly as it does today
    if(x < 0 || x >= w || y < 0 || y >= h) return false;
    unsigned entry = entries[(unsigned)(y >> 3) * tilesW + (unsigned)(x >> 3)];
    if(entry == Unmapped) return false;
    tile = entry & 0xff;
    return true;
  }

  // Write the hardware's 128x128 map into the middle of a `factor` times larger
  // one, leaving everything else unauthored: a starting point to paint on, and
  // a file that renders identically to today until it is painted.
  static auto dumpFromVram(const char* path, const uint16_t* vram, unsigned factor) -> bool {
    if(factor < 1 || 128 * factor > MaxTiles) return false;
    unsigned tiles = 128 * factor;
    unsigned origin = (tiles - 128) / 2;
    auto file = fopen(path, "wb");
    if(!file) return false;
    uint8_t header[14] = {'M', '7', 'X', 'M'};
    write16(header + 4, 1);
    write16(header + 6, tiles);
    write16(header + 8, tiles);
    write16(header + 10, origin);
    write16(header + 12, origin);
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    for(unsigned y = 0; ok && y < tiles; y++) {
      for(unsigned x = 0; ok && x < tiles; x++) {
        unsigned entry = Unmapped;
        if(y >= origin && y < origin + 128 && x >= origin && x < origin + 128) {
          entry = vram[(y - origin) * 128 + (x - origin)] & 0xff;
        }
        uint8_t word[2];
        write16(word, entry);
        ok = fwrite(word, 1, 2, file) == 2;
      }
    }
    fclose(file);
    return ok;
  }

private:
  static auto valid(unsigned tiles) -> bool {
    return tiles >= MinTiles && tiles <= MaxTiles && tiles % 128 == 0;
  }
  static auto read16(const uint8_t* p) -> unsigned { return p[0] | p[1] << 8; }
  static auto write16(uint8_t* p, unsigned v) -> void { p[0] = v & 0xff; p[1] = v >> 8 & 0xff; }

  uint16_t* entries = nullptr;
  unsigned tilesW = 0, tilesH = 0, originX = 0, originY = 0;
};

}
