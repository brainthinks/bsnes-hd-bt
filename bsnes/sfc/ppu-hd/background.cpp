#include <emulator/hdtoolkit.hpp>
#include <emulator/hdtrace.hpp>

auto PPU::Line::cacheBackgroundPanoramas() -> void {
  for(uint n = 0; n < count; n++) {
    auto& line = ppu.lines[start + n];
    for(uint bg = 0; bg < 4; bg++) {
      line.panorama[bg] = {};
      line.panoramaFirstRow[bg] = 0;
    }
  }
  if(!ppu.widescreen() || ppu.wsOverride()) return;
  if(HdTrace::noPanoramas()) return;

  //Any ordinary 8x8 background on a 64-tile-wide tilemap can hold a panorama:
  //F-Zero puts one on BG1 and BG2 in mode 1, Super Mario Kart on BG3 and BG4 in
  //mode 0. A 32-tile-wide map has no second half to hold the next window, and
  //offset-per-tile, hires, mosaic and Mode 7 keep their own address
  //calculations, so all of those are left alone.
  for(uint bg = 0; bg < 4; bg++) {
    auto background = [&](Line& line) -> IO::Background& {
      return bg == 0 ? line.io.bg1 : bg == 1 ? line.io.bg2 : bg == 2 ? line.io.bg3 : line.io.bg4;
    };
    auto usable = [&](Line& line) -> bool {
      auto& b = background(line);
      bool offsetPerTile = line.io.bgMode == 2 || line.io.bgMode == 4 || line.io.bgMode == 6;
      bool hires = line.io.bgMode == 5 || line.io.bgMode == 6;
      return !offsetPerTile && !hires
          && b.tileMode != TileMode::Mode7 && b.tileMode != TileMode::Inactive
          && !b.tileSize && (b.screenSize & 1) && !b.mosaicEnable
          && (b.aboveEnable || b.belowEnable);
    };
    uint n = 0;
    while(n < count) {
      auto& line = ppu.lines[start + n];
      auto& b = background(line);
      if(!usable(line)) { n++; continue; }
      uint end = n + 1;
      while(end < count) {
        auto& next = ppu.lines[start + end];
        auto& nb = background(next);
        if(!usable(next) || nb.screenAddress != b.screenAddress || nb.screenSize != b.screenSize
        || nb.tiledataAddress != b.tiledataAddress || nb.voffset != b.voffset
        || nb.aboveEnable != b.aboveEnable || nb.belowEnable != b.belowEnable) break;
        end++;
      }
      uint rows = b.screenSize & 2 ? 64 : 32;
      uint screenY = b.screenSize & 2 ? 32 << 5 + (b.screenSize & 1) : 0;  //rows 32..63
      uint vmask = (rows << 3) - 1;
      uint first = ((line.y + b.voffset) & vmask) >> 3;
      uint last = ((ppu.lines[start + end - 1].y + b.voffset) & vmask) >> 3;
      //a band straddling the tilemap's own vertical wrap keeps ordinary wrapping
      if(last >= first) {
        auto grid = HdToolkit::panoramaGrid(ppu.vram, b.screenAddress, rows, screenY, first, last);
        for(uint i = n; i < end; i++) {
          auto& target = ppu.lines[start + i];
          target.panorama[bg] = grid;
          //each line resolves its own window: a visible band several tile rows
          //tall can cross from one window into the next
          target.panoramaFirstRow[bg] = ((target.y + b.voffset) & vmask) >> 3;
        }
      }
      n = end;
    }
  }
}

//Debug loader for the extended Mode 7 map. A ROM hack would stream the map and
//its window origin through a side channel; until that exists, load one from a
//file so the rendering can be seen. Dumping writes the hardware's current map
//into the middle of a larger one, which reads back identical to today because
//everything around it is left unauthored.
auto PPU::Line::cacheMode7ExtendedMap() -> void {
  static bool once = false;
  if(once) return;
  once = true;
  if(auto spec = HdTrace::extendedMapDump()) {
    char path[512];
    unsigned factor = 2;
    if(auto comma = strchr(spec, ',')) {
      unsigned length = (unsigned)(comma - spec);
      if(length >= sizeof(path)) length = sizeof(path) - 1;
      memcpy(path, spec, length);
      path[length] = 0;
      factor = (unsigned)atoi(comma + 1);
    } else {
      snprintf(path, sizeof(path), "%s", spec);
    }
    HdToolkit::Mode7ExtendedMap::dumpFromVram(path, ppu.vram, factor ? factor : 2);
  }
  if(auto path = HdTrace::extendedMapPath()) ppu.mode7ExtMap.load(path);
}

auto PPU::Line::renderBackground(PPU::IO::Background& self, uint8 source) -> void {
  if(!self.aboveEnable && !self.belowEnable) return;
  if(self.tileMode == TileMode::Mode7) return renderMode7(self, source);
  if(self.tileMode == TileMode::Inactive) return;

  auto wsDec = HdToolkit::decideWsBg(
    ppu.wsbg(source), (int)this->y, self.tileSize, self.hoffset, self.voffset,
    (int)ppu.widescreen(), ppu.wsOverride()
  );
  if(wsDec.disable) return;
  int ws = wsDec.extra;
  bool autoCrop = wsDec.autoCrop;
  int globalWs = (int)ppu.widescreen();

  bool windowAbove[448];
  bool windowBelow[448];
  renderWindow(self.window, self.window.aboveEnable, windowAbove, (uint)globalWs);
  renderWindow(self.window, self.window.belowEnable, windowBelow, (uint)globalWs);

  bool hires = io.bgMode == 5 || io.bgMode == 6;
  bool offsetPerTileMode = io.bgMode == 2 || io.bgMode == 4 || io.bgMode == 6;
  bool directColorMode = io.col.directColor && source == Source::BG1 && (io.bgMode == 3 || io.bgMode == 4);
  uint colorShift = 3 + self.tileMode;
  int width = 256 << hires;

  uint tileHeight = 3 + self.tileSize;
  uint tileWidth = !hires ? tileHeight : 4;
  uint tileMask = 0x0fff >> self.tileMode;
  uint tiledataIndex = self.tiledataAddress >> 3 + self.tileMode;

  uint paletteBase = io.bgMode == 0 ? source << 5 : 0;
  uint paletteShift = 2 << self.tileMode;

  uint hscroll = self.hoffset;
  uint vscroll = self.voffset;
  uint hmask = (width << self.tileSize << !!(self.screenSize & 1)) - 1;
  uint vmask = (width << self.tileSize << !!(self.screenSize & 2)) - 1;

  uint y = this->y;

  if(hires) {
    hscroll <<= 1;
    if(io.interlace) y = y << 1 | (field() && !self.mosaicEnable);
  }
  if(self.mosaicEnable) {
    y -= (io.mosaic.size - io.mosaic.counter) << (hires && io.interlace);
  }

  uint mosaicCounter = 1;
  uint mosaicPalette = 0;
  uint8 mosaicPriority = 0;
  uint32 mosaicColor = 0;
  bool lastCropped = false;

  //Widescreen panorama continuation. F-Zero and games like it store the horizon
  //as 256-pixel windows on separate tilemap row bands, with the tilemap's second
  //32x32 half holding, at the same rows, the window 256 pixels further on.
  //Ordinary hardware wrapping is therefore already right across the whole of
  //0..hmask; only a fetch that leaves the map has to be redirected, and where it
  //lands is not tile-aligned, so the walk is split by column rather than decided
  //once per 8-pixel tile.
  auto grid = panorama[source];
  uint panoramaFirst = panoramaFirstRow[source];
  int scroll = (int)(hscroll & hmask);
  int spanLow = -ws, spanHigh = width + ws;

  auto renderSpan = [&](int from, int to, int hAdjust, int vAdjust) {
  if(from >= to) return;
  int x = from - ((from + (int)hscroll) & 7);
  while(x < to) {
    uint hoffset = x + hscroll + hAdjust;
    uint voffset = y + vscroll + vAdjust;
    if(offsetPerTileMode) {
      uint validBit = 0x2000 << source;
      uint offsetX = x + (hscroll & 7);
      if(offsetX >= (1 << tileWidth)) {  //first column is exempt
        uint hlookup = getTile(io.bg3, offsetX - (1 << tileWidth) + ((io.bg3.hoffset & ~7) << hires), io.bg3.voffset + 0);
        if(io.bgMode == 4) {
          if(hlookup & validBit) {
            if(!(hlookup & 0x8000)) {
              hoffset = offsetX + (hlookup & ~7);
            } else {
              voffset = y + hlookup;
            }
          }
        } else {
          uint vlookup = getTile(io.bg3, offsetX - (1 << tileWidth) + ((io.bg3.hoffset & ~7) << hires), io.bg3.voffset + 8);
          if(hlookup & validBit) {
            hoffset = offsetX + (hlookup & ~7);
          }
          if(vlookup & validBit) {
            voffset = y + vlookup;
          }
        }
      }
    }
    hoffset &= hmask;
    voffset &= vmask;

    uint tileNumber = getTile(self, hoffset, voffset);
    uint mirrorY = tileNumber & 0x8000 ? 7 : 0;
    uint mirrorX = tileNumber & 0x4000 ? 7 : 0;
    uint8 tilePriority = self.priority[bool(tileNumber & 0x2000)];
    uint paletteNumber = tileNumber >> 10 & 7;
    uint paletteIndex = paletteBase + (paletteNumber << paletteShift) & 0xff;

    if(tileWidth  == 4 && (bool(hoffset & 8) ^ bool(mirrorX))) tileNumber +=  1;
    if(tileHeight == 4 && (bool(voffset & 8) ^ bool(mirrorY))) tileNumber += 16;
    tileNumber = (tileNumber & 0x03ff) + tiledataIndex & tileMask;

    uint16 address;
    address = (tileNumber << colorShift) + (voffset & 7 ^ mirrorY) & 0x7fff;

    uint64 data;
    data  = (uint64)ppu.vram[address +  0] <<  0;
    data |= (uint64)ppu.vram[address +  8] << 16;
    data |= (uint64)ppu.vram[address + 16] << 32;
    data |= (uint64)ppu.vram[address + 24] << 48;

    for(uint tileX = 0; tileX < 8; tileX++, x++) {
      if(x < from || x >= to) continue;
      if(--mosaicCounter == 0) {
        uint color, shift = mirrorX ? tileX : 7 - tileX;
      /*if(self.tileMode >= TileMode::BPP2)*/ {
          color  = data >> shift +  0 &   1;
          color += data >> shift +  7 &   2;
        }
        if(self.tileMode >= TileMode::BPP4) {
          color += data >> shift + 14 &   4;
          color += data >> shift + 21 &   8;
        }
        if(self.tileMode >= TileMode::BPP8) {
          color += data >> shift + 28 &  16;
          color += data >> shift + 35 &  32;
          color += data >> shift + 42 &  64;
          color += data >> shift + 49 & 128;
        }

        mosaicCounter = self.mosaicEnable ? io.mosaic.size << hires : 1;
        mosaicPalette = color;
        mosaicPriority = tilePriority;
        if(directColorMode) {
          mosaicColor = decode(directColor(paletteNumber, mosaicPalette));
        } else {
          mosaicColor = decode(cgram[paletteIndex + mosaicPalette]);
        }
      }
      if(!mosaicPalette) { lastCropped = false; continue; }
      if(autoCrop && (lastCropped || x < 8 || x > 255 - 8) && mosaicColor == 0) {
        lastCropped = true;
        continue;
      }
      lastCropped = false;

      int wx = (int)ppu.winXad(x) + globalWs;
      if(wx < 0 || wx >= 256 + 2 * globalWs) continue;

      if(!hires) {
        if(self.aboveEnable && !windowAbove[wx]) plotAbove(x, source, mosaicPriority, mosaicColor);
        if(self.belowEnable && !windowBelow[wx]) plotBelow(x, source, mosaicPriority, mosaicColor);
      } else {
        uint X = x >> 1;
        int Wx = (int)ppu.winXad((int)X) + globalWs;
        if(Wx < 0 || Wx >= 256 + 2 * globalWs) continue;
        if(!ppu.hd()) {
          if(x & 1) {
            if(self.aboveEnable && !windowAbove[Wx]) plotAbove(X, source, mosaicPriority, mosaicColor);
          } else {
            if(self.belowEnable && !windowBelow[Wx]) plotBelow(X, source, mosaicPriority, mosaicColor);
          }
        } else {
          if(self.aboveEnable && !windowAbove[Wx]) plotHD(above, X, source, mosaicPriority, mosaicColor, true, x & 1);
          if(self.belowEnable && !windowBelow[Wx]) plotHD(below, X, source, mosaicPriority, mosaicColor, true, x & 1);
        }
      }
    }
  }
  };

  //no panorama: one uncorrected walk, exactly as before
  if(!grid.count) return renderSpan(spanLow, spanHigh, 0, 0);

  //otherwise group the columns into runs that share an adjustment. Columns
  //inside the frame keep the hardware's own wrapping, whatever it draws.
  auto adjust = [&](int x, int& hAdjust, int& vAdjust) {
    hAdjust = vAdjust = 0;
    if(x >= 0 && x < width) return;
    HdToolkit::panoramaAdjust(grid, panoramaFirst, x + scroll, hAdjust, vAdjust);
  };
  int from = spanLow;
  while(from < spanHigh) {
    int hAdjust, vAdjust;
    adjust(from, hAdjust, vAdjust);
    int to = from + 1;
    while(to < spanHigh) {
      int h, v;
      adjust(to, h, v);
      if(h != hAdjust || v != vAdjust) break;
      to++;
    }
    renderSpan(from, to, hAdjust, vAdjust);
    from = to;
  }
}

auto PPU::Line::getTile(PPU::IO::Background& self, uint hoffset, uint voffset) -> uint {
  bool hires = io.bgMode == 5 || io.bgMode == 6;
  uint tileHeight = 3 + self.tileSize;
  uint tileWidth = !hires ? tileHeight : 4;
  uint screenX = self.screenSize & 1 ? 32 << 5 : 0;
  uint screenY = self.screenSize & 2 ? 32 << 5 + (self.screenSize & 1) : 0;
  uint tileX = hoffset >> tileWidth;
  uint tileY = voffset >> tileHeight;
  uint offset = (tileY & 0x1f) << 5 | (tileX & 0x1f);
  if(tileX & 0x20) offset += screenX;
  if(tileY & 0x20) offset += screenY;
  return ppu.vram[self.screenAddress + offset & 0x7fff];
}
