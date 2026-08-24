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
    memory::fill<float>(ppu.gpuMode7.lines, 240 * 16);
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

  uint curr = 0, prev = 0;
  if(hd) for(uint x : range(256 * scale * scale)) {
    uint32 color = pixel(x / scale & 255, above[x], below[x]);
    if(ppu.gpuSupersample()) {
      uint8 src = above[x].source;
      if(src == Source::OBJ1 || src == Source::OBJ2) color |= 0xff000000u;
      else color &= 0x00ffffffu;
    }
    *output++ = color;
  } else if(width == 256) for(uint x : range(256)) {
    *output++ = pixel(x, above[x], below[x]);
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
  return table[color & 0x7fff];
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
