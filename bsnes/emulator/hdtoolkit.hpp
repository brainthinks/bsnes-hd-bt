#pragma once

namespace HdToolkit {
  // DerKoun widescreen width on each side, in SNES pixels (multiple of 8).
  // Values > 200 are aspect codes: 1609 = 16:9, 1610 = 16:10, 2109 = 21:9,
  // 201 = 2:1, 403 = 4:3.
  static constexpr auto determineWsExt(int ws, bool overscan, bool aspectCorrection) -> int {
    (void)aspectCorrection;
    double val = ws;
    if(ws > 200) {
      int w = ws / 100;
      int h = ws % 100;
      if(h <= 0) return 0;
      val = overscan ? 224.0 : 216.0;
      val *= w;
      val /= h;
      // Do not shrink by 8:7 PAR. That made 16:9 only 40px/side and then
      // stretched those pixels to fill the monitor. Extra columns are more
      // SNES tiles (64/side at 16:9 overscan-off), not a stretched 256.
      if(val <= 256) return 0;
      val -= 256;
      val /= 2;
    }
    val /= 8;
    if(overscan) val += 0.5;
    ws = (int)val;
    if(ws <= 0) return 0;
    if(ws > 12) ws = 12;  // 96px/side; sprite wrap limit
    return ws * 8;
  }

  // DerKoun BG widescreen: combo offset 0..16 (and 1000+ for .bso lines).
  // extra is SNES columns past 256 on each side, or -8 to crop the 256 edges.
  struct WsBgDecision {
    int extra = 0;
    bool autoCrop = false;
    bool disable = false;
  };

  static constexpr auto decideWsBg(
    unsigned conf, int y, unsigned tileSize, unsigned hoffset, unsigned voffset,
    int widescreen, bool wsOverride
  ) -> WsBgDecision {
    WsBgDecision d;
    d.extra = widescreen;
    if(widescreen <= 0 || wsOverride) { d.extra = 0; return d; }
    if(conf == 14) { d.disable = true; d.extra = 0; return d; }
    if(conf == 12) { d.extra = -8; return d; }
    if(conf == 13) { d.extra = 0; d.autoCrop = true; return d; }
    if(conf == 15) {
      if(tileSize == 0 && hoffset == 0) d.extra = 0;
      return d;
    }
    if(conf == 16) {
      if(tileSize == 0 && hoffset == 0 && voffset == 0) d.extra = 0;
      return d;
    }
    if(conf == 0) { d.extra = 0; return d; }
    if(conf >= 2 && conf <= 11) {
      bool below = (conf % 2) != 0;
      int line = (int)(conf / 2) * 40;
      if(below == (y < line)) d.extra = 0;
      return d;
    }
    if(conf >= 1000 && conf < 3000) {
      if((conf < 2000) != (y < (int)(conf % 1000))) d.extra = 0;
      return d;
    }
    return d;  // conf == 1: on
  }
  // Some games store a panorama as consecutive 256-pixel windows stacked on
  // different tilemap row bands: the first 32x32 half holds the windows, and
  // the second half holds, at the same rows, the window that follows. Ordinary
  // hardware wrapping therefore already continues the picture correctly across
  // the whole 512-pixel map; only a fetch that leaves the map needs its row
  // band moved by one more window.
  //
  // Matching the visible band against every other row band to find that
  // neighbour breaks down at the wrap-around window, which a game that streams
  // the panorama as it turns leaves half-written. Fit the grid instead, once,
  // and read the neighbouring windows off it.
  struct PanoramaGrid {
    unsigned base = 0;      // first tilemap row of the grid
    unsigned height = 0;    // rows per window
    unsigned count = 0;     // windows in the panorama; 0 means "not a panorama"
    unsigned lastSpan = 0;  // pixels the final window adds before the loop closes
    auto length() const -> int { return (int)(256 * (count - 1) + lastSpan); }
  };

  inline auto panoramaGrid(const unsigned short* vram, unsigned address,
    unsigned first, unsigned last) -> PanoramaGrid {
    PanoramaGrid none;
    if(last < first || last > 31) return none;

    unsigned hash[2][32];
    bool detail[32];
    for(unsigned half = 0; half < 2; half++) {
      for(unsigned row = 0; row < 32; row++) {
        unsigned base = address + half * 1024 + row * 32;
        unsigned short leftmost = vram[base & 0x7fff];
        unsigned h = 2166136261u;
        bool varies = false;
        for(unsigned x = 0; x < 32; x++) {
          unsigned short word = vram[(base + x) & 0x7fff];
          if(word != leftmost) varies = true;
          h = (h ^ word) * 16777619u;
        }
        hash[half][row] = h;
        if(!half) detail[row] = varies;
      }
    }

    PanoramaGrid best;
    unsigned bestGood = 0, bestSpan = 0;
    for(unsigned height = 2; height <= 16; height++) {
      for(unsigned count = 2; count * height <= 32; count++) {
        unsigned span = height * count;
        for(unsigned base = 0; base + span <= 32; base++) {
          if(first < base || last >= base + span) continue;
          unsigned good = 0, bad = 0;
          for(unsigned band = 0; band < count; band++) {
            bool matches = true, hasDetail = false;
            for(unsigned i = 0; i < height; i++) {
              unsigned row = base + band * height + i;
              if(detail[row]) hasDetail = true;
              if(hash[1][row] != hash[0][base + (row - base + height) % span]) matches = false;
            }
            if(!hasDetail) continue;  //an all-sky window proves nothing either way
            matches ? good++ : bad++;
          }
          //one window may lag: a game that streams the panorama writes the
          //upcoming window as it turns, so the wrap target is often stale
          if(good < 2 || bad > 1) continue;
          if(good > bestGood || (good == bestGood && span > bestSpan)) {
            best = {base, height, count, 256};
            bestGood = good;
            bestSpan = span;
          }
        }
      }
    }
    if(!best.count) return none;

    //the search compared row hashes; confirm the winner against the tilemap
    unsigned span = best.height * best.count;
    unsigned confirmed = 0;
    for(unsigned band = 0; band < best.count; band++) {
      bool matches = true;
      for(unsigned i = 0; i < best.height && matches; i++) {
        unsigned row = best.base + band * best.height + i;
        unsigned other = best.base + (row - best.base + best.height) % span;
        for(unsigned x = 0; x < 32; x++) {
          if(vram[(address + 1024 + row * 32 + x) & 0x7fff]
          != vram[(address + other * 32 + x) & 0x7fff]) { matches = false; break; }
        }
      }
      if(matches) confirmed++;
    }
    if(confirmed < 2) return none;

    //The panorama's length need not be a whole number of windows: F-Zero's
    //nearest layer closes after three and a half, so the last window's second
    //half repeats the first window's beginning. Recover that overlap from the
    //second half, which holds the panorama 256 pixels on from each window.
    best.lastSpan = 256;
    for(unsigned shift = 1; shift < 32; shift++) {
      bool matches = true, hasDetail = false;
      for(unsigned i = 0; i < best.height && matches; i++) {
        unsigned from = best.base + (best.count - 1) * best.height + i;
        unsigned to = best.base + i;
        for(unsigned x = 0; x + shift < 32; x++) {
          unsigned short a = vram[(address + 1024 + from * 32 + x) & 0x7fff];
          unsigned short b = vram[(address + to * 32 + x + shift) & 0x7fff];
          if(a != b) { matches = false; break; }
          if(x && a != vram[(address + 1024 + from * 32) & 0x7fff]) hasDetail = true;
        }
      }
      if(matches && hasDetail) { best.lastSpan = 256 - shift * 8; break; }
    }
    return best;
  }

  // Address one widescreen column through the panorama rather than through the
  // tilemap: work out which window holds that part of the picture and read it
  // from the first half. Going through the second half instead would often
  // agree, but a game that streams the panorama keeps scratch there -- F-Zero
  // leaves the far half of its last window unwritten -- and only the first half
  // is what the game itself puts on screen. Call this for widescreen columns
  // only; inside the frame the hardware's own wrapping is the picture.
  inline auto panoramaAdjust(PanoramaGrid grid, unsigned first, int unwrapped,
    int& hAdjust, int& vAdjust) -> bool {
    hAdjust = vAdjust = 0;
    if(!grid.count || first < grid.base) return false;
    int window = (int)((first - grid.base) / grid.height);
    int length = grid.length();
    if(length <= 0) return false;
    int position = (window * 256 + unwrapped) % length;
    if(position < 0) position += length;
    int target = position / 256, offset = position % 256;
    if(target >= (int)grid.count) return false;
    hAdjust = offset - unwrapped;
    vAdjust = (target - window) * (int)grid.height * 8;
    return true;
  }

}
