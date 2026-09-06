#include <emulator/hdtoolkit.hpp>
#include <emulator/hdtrace.hpp>
uint PPU::Line::start = 0;
uint PPU::Line::count = 0;

auto PPU::Line::flush() -> void {
  ppu.wsExt = HdToolkit::determineWsExt((int)ppu.widescreenRaw(),
    configuration.video.overscan, configuration.video.aspectCorrection);

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
    const bool gpu = ppu.gpuSupersample();
    // writeVRAM/writeOAM also flush. Wiping all 240 GPU lines there dropped
    // Mode 7 matrices before videoFrame, so SS never ran (CPU 1× only).
    if(gpu && HDMode7::gpuFlushClearsAll(Line::start)) {
      ppu.gpuMode7.active = false;
      memory::fill<float>(ppu.gpuMode7.lines, 240 * 24);
      memory::fill<uint8>(ppu.gpuMode7.colorWindow, 240 * 256);
    } else if(gpu) {
      for(uint i = 0; i < Line::count; i++) {
        uint y = ppu.lines[Line::start + i].y;
        if(y < 240) {
          memory::fill<float>(ppu.gpuMode7.lines + y * 24, 24);
          memory::fill<uint8>(ppu.gpuMode7.colorWindow + y * 256, 256);
        }
      }
    }
    if(ppu.hdScale() > 1) cacheMode7HD();
    cacheBackgroundPanoramas();
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
    if(gpu) {
      ppu.gpuMode7.active = false;
      for(uint y = 0; y < 240; y++) {
        if(HDMode7::lineIsValid(ppu.gpuMode7.lines[y * HDMode7::lineFloats + HDMode7::lineValidIndex])) {
          ppu.gpuMode7.active = true;
          break;
        }
      }
    }
    if(auto dump = getenv("BSNES_DUMP_GPU")) {
      static bool once = false;
      if(!once && ppu.gpuMode7.active) {
        once = true;
        if(auto fp = fopen("/tmp/bsnes-hd-bg.txt", "w")) {
          fprintf(fp, "ws=%u ov=%d groups=%d conf=%u,%u,%u,%u\n",
            ppu.widescreen(), (int)ppu.wsOverride(), ppu.mode7LineGroups.count,
            ppu.wsbg(0), ppu.wsbg(1), ppu.wsbg(2), ppu.wsbg(3));
          auto dumpBg = [&](uint y) {
            auto& L = ppu.lines[y];
            fprintf(fp, "y=%u mode=%u m7=%d |", y, (unsigned)L.io.bgMode,
              (int)(L.io.bg1.tileMode == TileMode::Mode7));
            PPU::IO::Background* bgs[4] = {&L.io.bg1, &L.io.bg2, &L.io.bg3, &L.io.bg4};
            for(uint i = 0; i < 4; i++) {
              auto& b = *bgs[i];
              auto d = HdToolkit::decideWsBg(ppu.wsbg(i), (int)y, b.tileSize, b.hoffset, b.voffset,
                (int)ppu.widescreen(), ppu.wsOverride());
              fprintf(fp, " BG%u tm=%u ts=%u ss=%u sa=%u h=%u v=%u ab=%d be=%d extra=%d windows=%d/len=%d;",
                i + 1, (unsigned)b.tileMode, (unsigned)b.tileSize, (unsigned)b.screenSize,
                (unsigned)b.screenAddress, (unsigned)b.hoffset, (unsigned)b.voffset,
                (int)b.aboveEnable, (int)b.belowEnable, d.extra,
                L.panorama[i].count ? (int)L.panorama[i].count : -1,
              L.panorama[i].count ? L.panorama[i].length() : -1);
            }
            fputc('\n', fp);
          };
          dumpBg(20);
          dumpBg(40);
          dumpBg(48);
          dumpBg(80);
          dumpBg(160);
          {
            auto dumpTiles = [&](const char* name, PPU::IO::Background& b, uint y) {
              auto& L = ppu.lines[y];
              uint hmask = (256u << b.tileSize << !!(b.screenSize & 1)) - 1;
              uint vmask = (256u << b.tileSize << !!(b.screenSize & 2)) - 1;
              fprintf(fp, "%s y=%u hmask=%u vmask=%u h=%u v=%u\n", name, y, hmask, vmask,
                (unsigned)b.hoffset, (unsigned)b.voffset);
              for(int x : {-64, 0, 128, 255, 256, 320, 383}) {
                uint hoffs = (uint)(x + (int)b.hoffset) & hmask;
                uint voffs = ((uint)y + b.voffset) & vmask;
                uint tile = L.getTile(b, hoffs, voffs);
                fprintf(fp, "  x=%d hoffs=%u tile=%u ch=%u\n",
                  x, hoffs, tile, tile & 0x3ff);
              }
            };
            dumpTiles("BG1", ppu.lines[20].io.bg1, 20);
            dumpTiles("BG2", ppu.lines[20].io.bg2, 20);
            dumpTiles("BG1", ppu.lines[40].io.bg1, 40);
            dumpTiles("BG2", ppu.lines[40].io.bg2, 40);
            auto& w1 = ppu.lines[20].io.bg1.window;
            auto& w2 = ppu.lines[20].io.bg2.window;
            auto& wc = ppu.lines[20].io.window;
            fprintf(fp, "win BG1 en=%d/%d inv=%d/%d mask=%u above=%d\n",
              (int)w1.oneEnable, (int)w1.twoEnable, (int)w1.oneInvert, (int)w1.twoInvert,
              (unsigned)w1.mask, (int)w1.aboveEnable);
            fprintf(fp, "win geom one=%u-%u two=%u-%u\n",
              (unsigned)wc.oneLeft, (unsigned)wc.oneRight, (unsigned)wc.twoLeft, (unsigned)wc.twoRight);
            fprintf(fp, "win BG2 oneEn=%d twoEn=%d above=%d\n",
              (int)w2.oneEnable, (int)w2.twoEnable, (int)w2.aboveEnable);
          }
          fclose(fp);
        }
        (void)dump;
      }
    }
    //BSNES_TRACE_BG=<path>: one line per frame, so hoffset/voffset/pan can be
    //plotted against heading. BSNES_DUMP_VRAM_AT="120,180" with BSNES_TRACE_BG
    //also writes the whole 64x32 tilemap at those frames.
    if(auto path = getenv("BSNES_TRACE_BG")) {
      static FILE* fp = nullptr;
      if(!fp) {
        fp = fopen(path, "w");
        if(fp) fprintf(fp, "#frame m7(a,b,c,d) heading | per-line BG state\n");
      }
      if(fp) {
        uint f = HdTrace::frame();
        auto& m7 = ppu.lines[100].io.mode7;
        double a = (int16)m7.a, c = (int16)m7.c;
        double heading = atan2(c, a) * 180.0 / 3.14159265358979;
        if(heading < 0) heading += 360.0;
        fprintf(fp, "f=%u a=%d b=%d c=%d d=%d hdg=%.2f\n",
          f, (int)(int16)m7.a, (int)(int16)m7.b, (int)(int16)m7.c, (int)(int16)m7.d, heading);
        {
          uint hits = 0; int lo = 0, hi = 0; uint firstLine = 0, firstBg = 0;
          for(uint yy = 0; yy < 240; yy++) {
            for(uint bg = 0; bg < 4; bg++) {
              for(uint side = 0; side < 2; side++) {
                if(side) continue;
                auto& g = ppu.lines[yy].panorama[bg];
                if(!g.count) continue;
                int pan = g.length();
                if(!hits) { firstLine = yy; firstBg = bg + 1; }
                hits++;
                if(pan < lo) lo = pan;
                if(pan > hi) hi = pan;
              }
            }
          }
          fprintf(fp, "  PANORAMA lines=%u length=%d..%d firstLine=%u firstBg=%u\n", hits, lo, hi, firstLine, firstBg);
        }
        for(uint y : {8u, 16u, 20u, 24u, 32u, 40u}) {
          auto& L = ppu.lines[y];
          PPU::IO::Background* bgs[4] = {&L.io.bg1, &L.io.bg2, &L.io.bg3, &L.io.bg4};
          fprintf(fp, "  y=%u mode=%u", y, (unsigned)L.io.bgMode);
          for(uint i = 0; i < 4; i++) {
            auto& b = *bgs[i];
            fprintf(fp, " | BG%u tm=%u ts=%u ss=%u sa=%u h=%u v=%u en=%d%d windows=%d/len=%d",
              i + 1, (unsigned)b.tileMode, (unsigned)b.tileSize, (unsigned)b.screenSize,
              (unsigned)b.screenAddress, (unsigned)b.hoffset, (unsigned)b.voffset,
              (int)b.aboveEnable, (int)b.belowEnable,
              L.panorama[i].count ? (int)L.panorama[i].count : -1,
              L.panorama[i].count ? L.panorama[i].length() : -1);
          }
          fputc('\n', fp);
        }
        //BSNES_DUMP_VRAM_BIN=<prefix>: raw VRAM + CGRAM at the same frames, so
        //the tilemap can be decoded and rendered offline.
        if(HdTrace::wantDumpVram(f)) {
          if(auto prefix = getenv("BSNES_DUMP_VRAM_BIN")) {
            char path[512];
            snprintf(path, sizeof(path), "%s-%06u.bin", prefix, f);
            if(auto bp = fopen(path, "wb")) {
              fwrite(ppu.vram, 2, 32768, bp);
              fwrite(ppu.cgram, 2, 256, bp);
              fclose(bp);
            }
          }
        }
        if(HdTrace::wantDumpVram(f)) {
          for(uint i = 0; i < 2; i++) {
            auto& b = i ? ppu.lines[20].io.bg2 : ppu.lines[20].io.bg1;
            fprintf(fp, "VRAM f=%u BG%u sa=%u ss=%u ts=%u td=%u\n",
              f, i + 1, (unsigned)b.screenAddress, (unsigned)b.screenSize,
              (unsigned)b.tileSize, (unsigned)b.tiledataAddress);
            for(uint half = 0; half < 2; half++) {
              for(uint row = 0; row < 32; row++) {
                fprintf(fp, "  h%u r%02u:", half, row);
                for(uint col = 0; col < 32; col++) {
                  uint word = ppu.vram[(b.screenAddress + half * 1024 + row * 32 + col) & 0x7fff];
                  fprintf(fp, " %04x", word);
                }
                fputc('\n', fp);
              }
            }
          }
        }
        fflush(fp);
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
  auto lw = ppu.lineWidth();
  auto output = ppu.output + (!hd
  ? (ppu.widescreen() ? y * lw : (y * 1024 + (ppu.interlace() && field() ? 512 : 0)))
  : (y * lw * scale * scale)
  );
  auto width = (!hd
  ? (ppu.widescreen() ? lw : (!ppu.hires() ? 256 : 512))
  : (lw * scale * scale));

  if(io.displayDisable) {
    memory::fill<uint32>(output, width);
    return;
  }

  bool hires = io.pseudoHires || io.bgMode == 5 || io.bgMode == 6;
  uint32 aboveColor = decode(cgram[0]);
  uint32 belowColor = hires ? decode(cgram[0]) : decode(io.col.fixedColor);
  uint xa =  (hd || ss) && ppu.interlace() && field() ? lw * scale * scale / 2 : 0;
  uint xb = !(hd || ss)
    ? (ppu.widescreen() ? lw : 256)
    : (ppu.interlace() && !field() ? lw * scale * scale / 2 : lw * scale * scale);
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

  auto tagGpu = [&](uint32 color, uint8 src, int snesX) -> uint32 {
    if(!ppu.gpuSupersample()) return color;
    if(src == Source::OBJ1 || src == Source::OBJ2) return color | 0xff000000u;
    if(src == Source::BG1) {
      // HUD scanlines can be tiled BG1 (F-Zero sky). Only Mode 7 lines
      // belong to the GPU sampler; otherwise the CPU tile walk shows.
      if(this->y < 240 && HDMode7::lineIsValid(
        ppu.gpuMode7.lines[this->y * HDMode7::lineFloats + HDMode7::lineValidIndex]
      )) return color & 0x00ffffffu;
      return (color & 0x00ffffffu) | 0x80000000u;
    }
    uint wx = ppu.winXad(snesX);
    if(src == Source::COL && ppu.gpuMode7.active
    && (ppu.gpuMode7.colorWindow[this->y * 256 + wx] & HDMode7::bg1VisibleBit)) {
      return color & 0x00ffffffu;
    }
    return (color & 0x00ffffffu) | 0x80000000u;
  };

  uint curr = 0, prev = 0;
  int ws = (int)ppu.widescreen();
  if(hd) for(uint x : range(lw * scale * scale)) {
    int snesX = int(x / scale) - ws;
    uint32 color = pixel((uint)ppu.winXad(snesX), above[x], below[x]);
    *output++ = tagGpu(color, above[x].source, snesX);
  } else if(ppu.widescreen()) {
    int ws = (int)ppu.widescreen();
    for(uint x : range(lw)) {
      int snesX = (int)x - ws;
      uint32 color = pixel(ppu.winXad(snesX), above[x], below[x]);
      *output++ = tagGpu(color, above[x].source, snesX);
    }
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

auto PPU::Line::plotAbove(int x, uint8 source, uint8 priority, uint32 color) -> void {
  if(ppu.hd()) return plotHD(above, x, source, priority, color, false, false);
  int bx = x + (int)ppu.widescreen();
  if(bx < 0 || bx >= (int)ppu.lineWidth()) return;
  if(priority > above[bx].priority) above[bx] = {source, priority, color};
}

auto PPU::Line::plotBelow(int x, uint8 source, uint8 priority, uint32 color) -> void {
  if(ppu.hd()) return plotHD(below, x, source, priority, color, false, false);
  int bx = x + (int)ppu.widescreen();
  if(bx < 0 || bx >= (int)ppu.lineWidth()) return;
  if(priority > below[bx].priority) below[bx] = {source, priority, color};
}

//todo: name these variables more clearly ...
auto PPU::Line::plotHD(Pixel* pixel, int x, uint8 source, uint8 priority, uint32 color, bool hires, bool subpixel) -> void {
  auto scale = ppu.hdScale();
  int lw = (int)ppu.lineWidth();
  int bx = x + (int)ppu.widescreen();
  if(bx < 0 || bx >= lw) return;
  int xss = hires && subpixel ? scale / 2 : 0;
  int ys = ppu.interlace() && field() ? scale / 2 : 0;
  if(priority > pixel[bx * scale + xss + ys * lw * scale].priority) {
    Pixel p = {source, priority, color};
    int xsm = hires && !subpixel ? scale / 2 : scale;
    int ysm = ppu.interlace() && !field() ? scale / 2 : scale;
    for(int xs = xss; xs < xsm; xs++) {
      pixel[bx * scale + xs + ys * lw * scale] = p;
    }
    int size = sizeof(Pixel) * (xsm - xss);
    Pixel* source = &pixel[bx * scale + xss + ys * lw * scale];
    for(int yst = ys + 1; yst < ysm; yst++) {
      memcpy(&pixel[bx * scale + xss + yst * lw * scale], source, size);
    }
  }
}
