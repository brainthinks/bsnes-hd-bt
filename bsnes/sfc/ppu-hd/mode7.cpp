auto PPU::Line::renderMode7(PPU::IO::Background& self, uint8 source) -> void {
  // HD PPU always samples Mode 7 at HD scale. Mosaic is not a reason to
  // drop back to the 256-wide path (that is the Fast "HD->SD Mosaic" option).
  if(ppu.hdScale() > 1) return renderMode7HD(self, source);

  int Y = this->y;
  if(self.mosaicEnable) Y -= io.mosaic.size - io.mosaic.counter;
  int y = !io.mode7.vflip ? Y : 255 - Y;

  int a = (int16)io.mode7.a;
  int b = (int16)io.mode7.b;
  int c = (int16)io.mode7.c;
  int d = (int16)io.mode7.d;
  int hcenter = (int13)io.mode7.x;
  int vcenter = (int13)io.mode7.y;
  int hoffset = (int13)io.mode7.hoffset;
  int voffset = (int13)io.mode7.voffset;

  uint mosaicCounter = 1;
  uint mosaicPalette = 0;
  uint8 mosaicPriority = 0;
  uint32 mosaicColor = 0;

  auto clip = [](int n) -> int { return n & 0x2000 ? (n | ~1023) : (n & 1023); };
  int originX = (a * clip(hoffset - hcenter) & ~63) + (b * clip(voffset - vcenter) & ~63) + (b * y & ~63) + (hcenter << 8);
  int originY = (c * clip(hoffset - hcenter) & ~63) + (d * clip(voffset - vcenter) & ~63) + (d * y & ~63) + (vcenter << 8);

  bool windowAbove[256];
  bool windowBelow[256];
  renderWindow(self.window, self.window.aboveEnable, windowAbove);
  renderWindow(self.window, self.window.belowEnable, windowBelow);

  int ws = (int)ppu.widescreen();
  if(ppu.wsOverride()) ws = 0;
  for(int X = -ws; X < 256 + ws; X++) {
    int x = !io.mode7.hflip ? X : 255 - X;
    int pixelX = originX + a * x >> 8;
    int pixelY = originY + c * x >> 8;
    int tileX = pixelX >> 3 & 127;
    int tileY = pixelY >> 3 & 127;
    bool outOfBounds = (pixelX | pixelY) & ~1023;
    uint15 tileAddress = tileY * 128 + tileX;
    uint15 paletteAddress = ((pixelY & 7) << 3) + (pixelX & 7);
    //the extended map answers first where it has been authored; everywhere else
    //the hardware's own wrapping stands
    unsigned extended = 0;
    bool fromExtended = ppu.mode7ExtMap.lookup(pixelX, pixelY, extended)
                     || ppu.mode7WorldLookup(pixelX, pixelY, extended);
    uint8 tile = fromExtended ? (uint8)extended
               : io.mode7.repeat == 3 && outOfBounds ? 0 : ppu.vram[tileAddress] >> 0;
    uint8 palette = !fromExtended && io.mode7.repeat == 2 && outOfBounds ? 0
                  : ppu.vram[tile << 6 | paletteAddress] >> 8;

    uint8 priority;
    if(source == Source::BG1) {
      priority = self.priority[0];
    } else if(source == Source::BG2) {
      priority = self.priority[palette >> 7];
      palette &= 0x7f;
    }

    if(--mosaicCounter == 0) {
      mosaicCounter = self.mosaicEnable ? io.mosaic.size : 1;
      mosaicPalette = palette;
      mosaicPriority = priority;
      if(io.col.directColor && source == Source::BG1) {
        mosaicColor = decode(directColor(0, palette));
      } else {
        mosaicColor = decode(cgram[palette]);
      }
    }
    if(!mosaicPalette) continue;

    uint wx = ppu.winXad(X);
    if(self.aboveEnable && !windowAbove[wx]) plotAbove(X, source, mosaicPriority, mosaicColor);
    if(self.belowEnable && !windowBelow[wx]) plotBelow(X, source, mosaicPriority, mosaicColor);
  }
}
