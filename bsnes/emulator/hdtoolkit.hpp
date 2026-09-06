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

  // `rows` is the tilemap's height in tiles (32 or 64) and `screenY` the word
  // offset where rows 32..63 of a 64-row map live, matching getTile().
  //
  // The grid is derived, not searched. Away from the wrap, the second half of
  // row r holds the same tiles as the first half of row r + height, so the run
  // of rows with that property is the panorama minus its last window: its
  // length gives the height and the window count, and its start gives the base.
  inline auto panoramaGrid(const unsigned short* vram, unsigned address,
    unsigned rows, unsigned screenY, unsigned first, unsigned last) -> PanoramaGrid {
    PanoramaGrid none;
    if(last < first || last >= rows || rows > 64) return none;

    auto word = [&](unsigned half, unsigned row, unsigned column) -> unsigned short {
      unsigned offset = (row & 31) * 32 + (column & 31);
      if(half) offset += 1024;
      if(row & 32) offset += screenY;
      return vram[(address + offset) & 0x7fff];
    };

    unsigned hash[2][64];
    bool detail[64];
    for(unsigned half = 0; half < 2; half++) {
      for(unsigned row = 0; row < rows; row++) {
        unsigned short leftmost = word(half, row, 0);
        unsigned h = 2166136261u;
        bool varies = false;
        for(unsigned x = 0; x < 32; x++) {
          unsigned short tile = word(half, row, x);
          if(tile != leftmost) varies = true;
          h = (h ^ tile) * 16777619u;
        }
        hash[half][row] = h;
        if(!half) detail[row] = varies;
      }
    }

    //Away from the wrap, the second half of row r repeats the first half of row
    //r + height, so each candidate spacing leaves a run of rows with that
    //property: the panorama minus its last window. Walk those runs. A row of
    //flat sky repeats almost anything, so a run has to carry some detail.
    PanoramaGrid best;
    unsigned bestSpan = 0;
    for(unsigned height = 2; height <= 16; height++) {
      unsigned row = 0;
      while(row < rows) {
        if(row + height >= rows || hash[1][row] != hash[0][row + height]) { row++; continue; }
        unsigned begin = row;
        bool hasDetail = false;
        while(row < rows && row + height < rows && hash[1][row] == hash[0][row + height]) {
          if(detail[row]) hasDetail = true;
          row++;
        }
        unsigned run = row - begin;
        if(!hasDetail || run % height) continue;
        unsigned count = run / height + 1;  //the run omits the window that wraps
        unsigned span = count * height;
        if(count < 2 || begin + span > rows) continue;
        if(first < begin || last >= begin + span) continue;  //must hold the visible band
        if(span <= bestSpan) continue;
        best = {begin, height, count, 256};
        bestSpan = span;
      }
    }
    if(!best.count) return none;

    //the run was compared by row hash; confirm the whole grid against the tiles
    unsigned span = best.height * best.count;
    unsigned confirmed = 0;
    for(unsigned band = 0; band < best.count; band++) {
      bool matches = true;
      for(unsigned i = 0; i < best.height && matches; i++) {
        unsigned row = best.base + band * best.height + i;
        unsigned other = best.base + (row - best.base + best.height) % span;
        for(unsigned x = 0; x < 32; x++) {
          if(word(1, row, x) != word(0, other, x)) { matches = false; break; }
        }
      }
      if(matches) confirmed++;
    }
    //One window may lag: a game that streams the panorama writes the upcoming
    //window as it turns, so the wrap target is often stale. Two confirmed
    //windows are the least that distinguishes a panorama from a coincidence,
    //so a two-window grid has to confirm both.
    if(confirmed < 2 || confirmed + 1 < best.count) return none;

    //The panorama's length need not be a whole number of windows: F-Zero's
    //nearest layer closes after three and a half, so the last window's second
    //half repeats the first window's beginning. Recover that overlap from the
    //second half, which holds the panorama 256 pixels on from each window.
    for(unsigned shift = 1; shift < 32; shift++) {
      bool matches = true, hasDetail = false;
      for(unsigned i = 0; i < best.height && matches; i++) {
        unsigned from = best.base + (best.count - 1) * best.height + i;
        unsigned to = best.base + i;
        for(unsigned x = 0; x + shift < 32; x++) {
          unsigned short a = word(1, from, x);
          if(a != word(0, to, x + shift)) { matches = false; break; }
          if(x && a != word(1, from, 0)) hasDetail = true;
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
