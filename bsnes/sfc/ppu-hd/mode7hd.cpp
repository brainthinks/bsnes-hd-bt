//determine mode 7 line groups for perspective correction
auto PPU::Line::cacheMode7HD() -> void {
  ppu.mode7LineGroups.count = 0;
  if(!ppu.hdPerspective()) return;

  #define isLineMode7(line) (line.io.bg1.tileMode == TileMode::Mode7 && !line.io.displayDisable && ( \
    (line.io.bg1.aboveEnable || line.io.bg1.belowEnable) \
  ))
  bool state = false;
  uint y;
  for(y = 0; y < Line::count; y++) {
    if(state != isLineMode7(ppu.lines[Line::start + y])) {
      state = !state;
      if(state) {
        ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count] = ppu.lines[Line::start + y].y;
      } else {
        ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] = ppu.lines[Line::start + y].y - 1;
        int offset = (ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count]) / 8;
        ppu.mode7LineGroups.startLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count] + offset;
        ppu.mode7LineGroups.endLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - offset;
        ppu.mode7LineGroups.count++;
      }
    }
  }
  #undef isLineMode7
  if(state && Line::count) {
    uint last = Line::start + Line::count - 1;
    ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] = ppu.lines[last].y;
    int offset = (ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count]) / 8;
    ppu.mode7LineGroups.startLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count] + offset;
    ppu.mode7LineGroups.endLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - offset;
    ppu.mode7LineGroups.count++;
  }
}

auto PPU::Line::renderMode7HD(PPU::IO::Background& self, uint8 source) -> void {
  const bool extbg = source == Source::BG2;
  const uint outScale = ppu.hdScale();
  uint sampScale = ppu.hdSupersample();
  if(sampScale < 2 || extbg) sampScale = 1;
  if(sampScale > 16) sampScale = 16;
  const uint scale = outScale * sampScale;

  uint* sampTmp = nullptr;
  if(sampScale > 1) {
    sampTmp = new uint[256 * 4 * outScale]();
  }

  Pixel pixel;
  Pixel* above = &this->above[0];
  Pixel* below = &this->below[0];

  int y_a = -1;
  int y_b = -1;
  #define isLineMode7(n) (ppu.lines[n].io.bg1.tileMode == TileMode::Mode7 && !ppu.lines[n].io.displayDisable && ( \
    (ppu.lines[n].io.bg1.aboveEnable || ppu.lines[n].io.bg1.belowEnable) \
  ))
  if(ppu.hdPerspective()) {
    for(int i : range(ppu.mode7LineGroups.count)) {
      if(y >= ppu.mode7LineGroups.startLine[i] && y <= ppu.mode7LineGroups.endLine[i]) {
        y_a = ppu.mode7LineGroups.startLerpLine[i];
        y_b = ppu.mode7LineGroups.endLerpLine[i];
        break;
      }
    }
  }
  if(y_a == -1 || y_b == -1 || y_a == y_b) {
    y_a = y;
    y_b = y;
    if(y_a >   1 && isLineMode7(y_a)) y_a--;
    if(y_b < 239 && isLineMode7(y_b)) y_b++;
  }
  #undef isLineMode7

  float a_a = (int16)ppu.lines[y_a].io.mode7.a;
  float b_a = (int16)ppu.lines[y_a].io.mode7.b;
  float c_a = (int16)ppu.lines[y_a].io.mode7.c;
  float d_a = (int16)ppu.lines[y_a].io.mode7.d;

  float a_b = (int16)ppu.lines[y_b].io.mode7.a;
  float b_b = (int16)ppu.lines[y_b].io.mode7.b;
  float c_b = (int16)ppu.lines[y_b].io.mode7.c;
  float d_b = (int16)ppu.lines[y_b].io.mode7.d;

  int hcenter = (int13)io.mode7.x;
  int vcenter = (int13)io.mode7.y;
  int hoffset = (int13)io.mode7.hoffset;
  int voffset = (int13)io.mode7.voffset;

  if(io.mode7.vflip) {
    y_a = 255 - y_a;
    y_b = 255 - y_b;
  }

  if(ppu.gpuSupersample() && this->y < 240 && !extbg) {
    ppu.gpuMode7.active = true;
    ppu.gpuMode7.ss = ppu.gpuSsFactor();
    float* p = ppu.gpuMode7.lines + this->y * 24;
    p[ 0] = a_a; p[ 1] = b_a; p[ 2] = c_a; p[ 3] = d_a;
    p[ 4] = a_b; p[ 5] = b_b; p[ 6] = c_b; p[ 7] = d_b;
    p[ 8] = (float)y_a; p[ 9] = (float)y_b;
    p[10] = (float)hcenter; p[11] = (float)vcenter;
    p[12] = (float)((hoffset - hcenter) % 1024);
    p[13] = (float)((voffset - vcenter) % 1024);
    uint repeat = (uint)io.mode7.repeat & 3;
    p[14] = (io.mode7.hflip ? 1.0f : 0.0f) + 2.0f * (float)repeat;
    p[15] = (io.mode7.vflip ? 1.0f : 0.0f) + 2.0f;
    uint32 fc = decode(io.col.fixedColor);
    uint32 bc = decode(cgram[0]);
    p[16] = HDMode7::mathFlags(io.col.enable[Source::BG1], io.col.mathMode, io.col.halve, io.col.blendMode);
    HDMode7::rgbFromPacked(fc, p + 17);
    HDMode7::rgbFromPacked(bc, p + 20);
    p[23] = io.bg1.belowEnable ? 1.0f : 0.0f;
    bool mathWin[256];
    bool aboveWin[256];
    renderWindow(io.col.window, io.col.window.belowMask, mathWin);
    renderWindow(io.col.window, io.col.window.aboveMask, aboveWin);
    uint8* win = ppu.gpuMode7.colorWindow + this->y * 256;
    for(uint x : range(256)) {
      win[x] = HDMode7::colorWindowBits(mathWin[x], aboveWin[x]);
    }
  }

  bool windowAbove[256];
  bool windowBelow[256];
  renderWindow(self.window, self.window.aboveEnable, windowAbove);
  renderWindow(self.window, self.window.belowEnable, windowBelow);

  int pixelYp = INT_MIN;
  for(int ys : range(scale)) {
    float yf = y + ys * 1.0 / scale - 0.5;
    if(io.mode7.vflip) yf = 255 - yf;

    float a = 1.0 / lerp(y_a, 1.0 / a_a, y_b, 1.0 / a_b, yf);
    float b = 1.0 / lerp(y_a, 1.0 / b_a, y_b, 1.0 / b_b, yf);
    float c = 1.0 / lerp(y_a, 1.0 / c_a, y_b, 1.0 / c_b, yf);
    float d = 1.0 / lerp(y_a, 1.0 / d_a, y_b, 1.0 / d_b, yf);

    int ht = (hoffset - hcenter) % 1024;
    float vty = ((voffset - vcenter) % 1024) + yf;
    float originX = (a * ht) + (b * vty) + (hcenter << 8);
    float originY = (c * ht) + (d * vty) + (vcenter << 8);

    int pixelXp = INT_MIN;
    for(int x : range(256)) {
      bool doAbove = self.aboveEnable && !windowAbove[x];
      bool doBelow = self.belowEnable && !windowBelow[x];

      for(int xs : range(scale)) {
        float xf = x + xs * 1.0 / scale - 0.5;
        if(io.mode7.hflip) xf = 255 - xf;

        int pixelX = (originX + a * xf) / 256;
        int pixelY = (originY + c * xf) / 256;

        bool skip = false;
        if(pixelX != pixelXp || pixelY != pixelYp) {
          uint tile    = io.mode7.repeat == 3 && ((pixelX | pixelY) & ~1023) ? 0 : (ppu.vram[(pixelY >> 3 & 127) * 128 + (pixelX >> 3 & 127)] & 0xff);
          uint palette = io.mode7.repeat == 2 && ((pixelX | pixelY) & ~1023) ? 0 : (ppu.vram[(((pixelY & 7) << 3) + (pixelX & 7)) + (tile << 6)] >> 8);

          uint8 priority;
          if(!extbg) {
            priority = self.priority[0];
          } else {
            priority = self.priority[palette >> 7];
            palette &= 0x7f;
          }
          skip = !palette;

          if(!skip) {
            uint32 color;
            if(io.col.directColor && !extbg) {
              color = decode(directColor(0, palette));
            } else {
              color = decode(cgram[palette]);
            }
            pixel = {source, priority, color};
            pixelXp = pixelX;
            pixelYp = pixelY;
          }
        } else {
          skip = !pixel.priority;
        }

        if(sampScale == 1) {
          if(!skip && doAbove && (!extbg || pixel.priority > above->priority)) *above = pixel;
          if(!skip && doBelow && (!extbg || pixel.priority > below->priority)) *below = pixel;
          above++;
          below++;
        } else {
          int p = (x * outScale + (xs / sampScale)) * 4;
          sampTmp[p + 0] += pixel.priority;
          sampTmp[p + 1] += pixel.color >> 16 & 255;
          sampTmp[p + 2] += pixel.color >>  8 & 255;
          sampTmp[p + 3] += pixel.color >>  0 & 255;
          if((ys + 1) % sampScale == 0 && (xs + 1) % sampScale == 0) {
            uint div = sampScale * sampScale;
            uint8 priority = sampTmp[p + 0] / div;
            uint32 color = (sampTmp[p + 1] / div) << 16
                         | (sampTmp[p + 2] / div) <<  8
                         | (sampTmp[p + 3] / div) <<  0;
            if(!ppu.hdTrueColor()) {
              uint r = (color >>  0 & 255) * 31 / 255;
              uint g = (color >>  8 & 255) * 31 / 255;
              uint b = (color >> 16 & 255) * 31 / 255;
              color = b * 255 / 31 << 16 | g * 255 / 31 << 8 | r * 255 / 31;
            }
            if(!skip && doAbove && (!extbg || priority > above->priority)) *above = {source, priority, color};
            if(!skip && doBelow && (!extbg || priority > below->priority)) *below = {source, priority, color};
            above++;
            below++;
            sampTmp[p + 0] = sampTmp[p + 1] = sampTmp[p + 2] = sampTmp[p + 3] = 0;
          }
        }
      }
    }
  }

  delete[] sampTmp;
}

auto PPU::Line::lerp(float pa, float va, float pb, float vb, float pr) -> float {
  if(va == vb || pr == pa) return va;
  if(pr == pb) return vb;
  return va + (vb - va) / (pb - pa) * (pr - pa);
}
