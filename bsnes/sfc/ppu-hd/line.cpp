uint PPU::Line::start = 0;
uint PPU::Line::count = 0;

auto PPU::Line::flush() -> void {
  if(ppu.luminance != configuration.video.luminance
  || ppu.saturation != configuration.video.saturation
  || ppu.gamma != configuration.video.gamma) {
    uint luminance = configuration.video.luminance;
    uint saturation = configuration.video.saturation;
    uint gamma = configuration.video.gamma;
    ppu.luminance = luminance;
    ppu.saturation = saturation;
    ppu.gamma = gamma;
    for(uint l : range(16)) {
      if(ppu.lightTable[l]) delete[] ppu.lightTable[l];
      ppu.lightTable[l] = new uint32_t[32768];
      for(uint r : range(32)) {
        for(uint g : range(32)) {
          for(uint b : range(32)) {
            double dr = r * 255.0 / 31.0;
            double dg = g * 255.0 / 31.0;
            double db = b * 255.0 / 31.0;

            if(saturation != 100) {
              double satVal = saturation / 100.0;
              double grayInv = (dr + dg + db) / 3 * max(0.0, 1.0 - satVal);
              dr = dr * satVal + grayInv;
              dg = dg * satVal + grayInv;
              db = db * satVal + grayInv;
            }

            if(gamma != 100) {
              double gamVal = gamma / 100.0;
              double reciprocal = 1.0 / 127.0;
              dr = dr > 127.0 ? dr : 127.0 * pow(dr * reciprocal, gamVal);
              dg = dg > 127.0 ? dg : 127.0 * pow(dg * reciprocal, gamVal);
              db = db > 127.0 ? db : 127.0 * pow(db * reciprocal, gamVal);
            }

            if(luminance != 100) {
              double lumVal = luminance / 100.0;
              dr *= lumVal;
              dg *= lumVal;
              db *= lumVal;
            }

            double lVal = l / 15.0;
            dr *= lVal;
            dg *= lVal;
            db *= lVal;

            int ar = dr + 0.5;
            int ag = dg + 0.5;
            int ab = db + 0.5;
            if(ar > 255) ar = 255;
            if(ag > 255) ag = 255;
            if(ab > 255) ab = 255;
            if(ar < 0) ar = 0;
            if(ag < 0) ag = 0;
            if(ab < 0) ab = 0;

            ppu.lightTable[l][r << 10 | g << 5 | b << 0] = ab << 16 | ag << 8 | ar << 0;
          }
        }
      }
    }
  }

  if(Line::count) {
    ppu.gpuMode7.active = false;
    memory::fill<float>(ppu.gpuMode7.lines, 240 * 24);
    memory::fill<uint8>(ppu.gpuMode7.colorWindow, 240 * 256);
    if(ppu.hdScale() > 1) cacheMode7HD();
    #pragma omp parallel for if(Line::count >= 8)
    for(uint y = 0; y < Line::count; y++) {
      if(ppu.deinterlace()) {
        if(!ppu.interlace()) {
          //some games enable interlacing in 240p mode, just force these to even fields
          ppu.lines[Line::start + y].render(0);
        } else {
          //for actual interlaced frames, render both fields every farme for 480i -> 480p
          ppu.lines[Line::start + y].render(0);
          ppu.lines[Line::start + y].render(1);
        }
      } else {
        //standard 240p (progressive) and 480i (interlaced) rendering
        ppu.lines[Line::start + y].render(ppu.field());
      }
    }
    Line::start = 0;
    Line::count = 0;
    if(ppu.gpuMode7.active) ppu.prepareGpuMode7();
    if(auto path = getenv("BSNES_DUMP_MATH")) {
      static bool dumped = false;
      if(!dumped && ppu.gpuMode7.active) {
        dumped = true;
        if(auto fp = fopen(path, "w")) {
          fprintf(fp, "ss=%u trueColor=%d active=1\n", ppu.gpuMode7.ss, (int)ppu.hdTrueColor());
          for(uint y : range(240)) {
            float* p = ppu.gpuMode7.lines + y * 24;
            if(p[15] < 1.5f) continue;
            uint8* w = ppu.gpuMode7.colorWindow + y * 256;
            uint mathN = 0, aboveN = 0;
            for(uint x : range(256)) {
              if(w[x] & 1) mathN++;
              if(w[x] & 2) aboveN++;
            }
            fprintf(fp, "y=%u pack=%.0f valid=%.0f math=%.0f fx=%.3f,%.3f,%.3f bd=%.3f,%.3f,%.3f sub=%.0f winMath=%u winAbove=%u\n",
              y, p[14], p[15], p[16], p[17], p[18], p[19], p[20], p[21], p[22], p[23], mathN, aboveN);
          }
          fclose(fp);
        }
      }
    }
  }
}

auto PPU::Line::cache() -> void {
  uint y = ppu.vcounter();
  if(ppu.io.displayDisable || y >= ppu.vdisp()) {
    io.displayDisable = true;
  } else {
    memcpy(&io, &ppu.io, sizeof(io));
    memcpy(&cgram, &ppu.cgram, sizeof(cgram));
  }
  if(!Line::count) Line::start = y;
  Line::count++;
}

auto PPU::Line::render(bool fieldID) -> void {
  this->fieldID = fieldID;
  uint y = this->y + (!ppu.latch.overscan ? 7 : 0);

  auto hd = ppu.hd();
  auto ss = ppu.ss();
  auto scale = ppu.hdScale();
  auto output = ppu.output + (!hd
  ? (y * 1024 + (ppu.interlace() && field() ? 512 : 0))
  : (y * 256 * scale * scale)
  );
  auto width = (!hd
  ? (!ppu.hires() ? 256 : 512)
  : (256 * scale * scale));

  if(io.displayDisable) {
    memory::fill<uint32>(output, width);
    return;
  }

  bool hires = io.pseudoHires || io.bgMode == 5 || io.bgMode == 6;
  uint32 aboveColor = decode(cgram[0]);
  uint32 belowColor = hires ? decode(cgram[0]) : decode(io.col.fixedColor);
  uint xa =  (hd || ss) && ppu.interlace() && field() ? 256 * scale * scale / 2 : 0;
  uint xb = !(hd || ss) ? 256 : ppu.interlace() && !field() ? 256 * scale * scale / 2 : 256 * scale * scale;
  for(uint x = xa; x < xb; x++) {
    above[x] = {Source::COL, 0, aboveColor};
    below[x] = {Source::COL, 0, belowColor};
  }

  //hack: generally, renderBackground/renderObject ordering do not matter.
  //but for HD mode 7, a larger grid of pixels are generated, and so ordering ends up mattering.
  //as a hack for Mohawk & Headphone Jack, we reorder things for BG2 to render properly.
  //longer-term, we need to devise a better solution that can work for every game.
  renderBackground(io.bg1, Source::BG1);
  if(io.extbg == 0) renderBackground(io.bg2, Source::BG2);
  renderBackground(io.bg3, Source::BG3);
  renderBackground(io.bg4, Source::BG4);
  renderObject(io.obj);
  if(io.extbg == 1) renderBackground(io.bg2, Source::BG2);
  renderWindow(io.col.window, io.col.window.aboveMask, windowAbove);
  renderWindow(io.col.window, io.col.window.belowMask, windowBelow);

  auto tagGpu = [&](uint32 color, uint8 src, uint snesX) -> uint32 {
    if(!ppu.gpuSupersample()) return color;
    if(src == Source::OBJ1 || src == Source::OBJ2) return color | 0xff000000u;
    if(src == Source::BG1) return color & 0x00ffffffu;
    if(src == Source::COL && ppu.gpuMode7.active
    && (ppu.gpuMode7.colorWindow[this->y * 256 + snesX] & HDMode7::bg1VisibleBit)) {
      return color & 0x00ffffffu;
    }
    return (color & 0x00ffffffu) | 0x80000000u;
  };

  uint curr = 0, prev = 0;
  if(hd) for(uint x : range(256 * scale * scale)) {
    uint32 color = pixel(x / scale & 255, above[x], below[x]);
    *output++ = tagGpu(color, above[x].source, x / scale & 255);
  } else if(width == 256) for(uint x : range(256)) {
    uint32 color = pixel(x, above[x], below[x]);
    *output++ = tagGpu(color, above[x].source, x);
  } else if(!hires) for(uint x : range(256)) {
    auto color = pixel(x, above[x], below[x]);
    *output++ = color;
    *output++ = color;
  } else if(!configuration.video.blurEmulation) for(uint x : range(256)) {
    *output++ = pixel(x, below[x], above[x]);
    *output++ = pixel(x, above[x], below[x]);
  } else for(uint x : range(256)) {
    curr = pixel(x, below[x], above[x]);
    *output++ = (prev + curr - ((prev ^ curr) & 0x00010101)) >> 1;
    prev = curr;
    curr = pixel(x, above[x], below[x]);
    *output++ = (prev + curr - ((prev ^ curr) & 0x00010101)) >> 1;
    prev = curr;
  }
}

auto PPU::Line::pixel(uint x, Pixel above, Pixel below) const -> uint32 {
  if(!windowAbove[x]) above.color = 0x000000;
  if(!windowBelow[x]) return above.color;
  if(!io.col.enable[above.source]) return above.color;
  if(!io.col.blendMode) return blend(above.color, decode(io.col.fixedColor), io.col.halve && windowAbove[x]);
  return blend(above.color, below.color, io.col.halve && windowAbove[x] && below.source != Source::COL);
}

auto PPU::Line::blend(uint x, uint y, bool halve) const -> uint32 {
  if(!ppu.hdTrueColor()) {
    uint16 a = uint16(x >> 0 & 255) >> 3 | uint16(x >> 8 & 255) >> 3 << 5 | uint16(x >> 16 & 255) >> 3 << 10;
    uint16 b = uint16(y >> 0 & 255) >> 3 | uint16(y >> 8 & 255) >> 3 << 5 | uint16(y >> 16 & 255) >> 3 << 10;
    uint16 out;
    if(!io.col.mathMode) {
      if(!halve) {
        uint sum = a + b;
        uint carry = (sum - ((a ^ b) & 0x0421)) & 0x8420;
        out = (sum - carry) | (carry - (carry >> 5));
      } else {
        out = (a + b - ((a ^ b) & 0x0421)) >> 1;
      }
    } else {
      uint diff = a - b + 0x8420;
      uint borrow = (diff - ((a ^ b) & 0x8420)) & 0x8420;
      if(!halve) {
        out = (diff - borrow) & (borrow - (borrow >> 5));
      } else {
        out = (((diff - borrow) & (borrow - (borrow >> 5))) & 0x7bde) >> 1;
      }
    }
    uint r = (out >>  0 & 31) * 255 / 31;
    uint g = (out >>  5 & 31) * 255 / 31;
    uint b8 = (out >> 10 & 31) * 255 / 31;
    return b8 << 16 | g << 8 | r;
  }
  if(!io.col.mathMode) {  //add
    if(!halve) {
      uint sum = x + y;
      uint carry = (sum - ((x ^ y) & 0x00010101)) & 0x01010100;
      return (sum - carry) | (carry - (carry >> 8));
    } else {
      return (x + y - ((x ^ y) & 0x00010101)) >> 1;
    }
  } else {  //sub
    uint diff = x - y + 0x01010100;
    uint borrow = (diff - ((x ^ y) & 0x01010100)) & 0x01010100;
    if(!halve) {
      return   (diff - borrow) & (borrow - (borrow >> 8));
    } else {
      return (((diff - borrow) & (borrow - (borrow >> 8))) & 0x00fefefe) >> 1;
    }
  }
}

auto PPU::Line::decode(uint15 color) const -> uint32 {
  auto table = ppu.lightTable[io.displayBrightness];
  if(!table) return 0;
  uint32 c = table[color & 0x7fff];
  if(ppu.hdTrueColor()) return c;
  uint r = (c >>  0 & 255) * 31 / 255;
  uint g = (c >>  8 & 255) * 31 / 255;
  uint b = (c >> 16 & 255) * 31 / 255;
  return b * 255 / 31 << 16 | g * 255 / 31 << 8 | r * 255 / 31;
}

auto PPU::Line::directColor(uint paletteIndex, uint paletteColor) const -> uint16 {
  //paletteIndex = bgr
  //paletteColor = BBGGGRRR
  //output       = 0 BBb00 GGGg0 RRRr0
  return (paletteColor << 2 & 0x001c) + (paletteIndex <<  1 & 0x0002)   //R
       + (paletteColor << 4 & 0x0380) + (paletteIndex <<  5 & 0x0040)   //G
       + (paletteColor << 7 & 0x6000) + (paletteIndex << 10 & 0x1000);  //B
}

auto PPU::Line::plotAbove(uint x, uint8 source, uint8 priority, uint32 color) -> void {
  if(ppu.hd()) return plotHD(above, x, source, priority, color, false, false);
  if(priority > above[x].priority) above[x] = {source, priority, color};
}

auto PPU::Line::plotBelow(uint x, uint8 source, uint8 priority, uint32 color) -> void {
  if(ppu.hd()) return plotHD(below, x, source, priority, color, false, false);
  if(priority > below[x].priority) below[x] = {source, priority, color};
}

//todo: name these variables more clearly ...
auto PPU::Line::plotHD(Pixel* pixel, uint x, uint8 source, uint8 priority, uint32 color, bool hires, bool subpixel) -> void {
  auto scale = ppu.hdScale();
  int xss = hires && subpixel ? scale / 2 : 0;
  int ys = ppu.interlace() && field() ? scale / 2 : 0;
  if(priority > pixel[x * scale + xss + ys * 256 * scale].priority) {
    Pixel p = {source, priority, color};
    int xsm = hires && !subpixel ? scale / 2 : scale;
    int ysm = ppu.interlace() && !field() ? scale / 2 : scale;
    for(int xs = xss; xs < xsm; xs++) {
      pixel[x * scale + xs + ys * 256 * scale] = p;
    }
    int size = sizeof(Pixel) * (xsm - xss);
    Pixel* source = &pixel[x * scale + xss + ys * 256 * scale];
    for(int yst = ys + 1; yst < ysm; yst++) {
      memcpy(&pixel[x * scale + xss + yst * 256 * scale], source, size);
    }
  }
}
