#include <sfc/sfc.hpp>
#include <cmath>

namespace SuperFamicom {

extern PPU& ppubase;

#define PPU PPUhd
#define ppu ppuhd

PPU ppu;
#include "io.cpp"
#include "line.cpp"
#include "background.cpp"
#include "mode7.cpp"
#include "gpu-mode7.hpp"
#include "mode7hd.cpp"
#include "object.cpp"
#include "window.cpp"
#include "serialization.cpp"

auto PPU::interlace() const -> bool { return ppubase.display.interlace; }
auto PPU::overscan() const -> bool { return ppubase.display.overscan; }
auto PPU::vdisp() const -> uint { return ppubase.display.vdisp; }
auto PPU::hires() const -> bool { return latch.hires; }
auto PPU::hd() const -> bool { return latch.hd; }
auto PPU::ss() const -> bool { return latch.ss; }
#undef ppu
auto PPU::hdScale() const -> uint { return configuration.hacks.ppu.hdMode7.scale; }
auto PPU::hdPerspective() const -> bool { return true; }
auto PPU::hdSupersample() const -> uint {
  return HDMode7::cpuSampleScale(
    configuration.hacks.ppu.hdMode7.gpuSupersample,
    configuration.hacks.ppu.hdMode7.ssFactor,
    configuration.hacks.ppu.hdMode7.supersample
  );
}
auto PPU::gpuSupersample() const -> bool {
  return configuration.hacks.ppu.hdMode7.gpuSupersample;
}
auto PPU::gpuSsFactor() const -> uint {
  return HDMode7::gpuSampleScale(
    configuration.hacks.ppu.hdMode7.ssFactor,
    configuration.hacks.ppu.hdMode7.supersample
  );
}
auto PPU::hdMosaic() const -> bool { return configuration.hacks.ppu.hdMode7.mosaic; }
auto PPU::hdTrueColor() const -> bool { return configuration.hacks.ppu.hdTrueColor; }
auto PPU::deinterlace() const -> bool { return configuration.hacks.ppu.hdDeinterlace; }
auto PPU::renderCycle() const -> uint { return configuration.hacks.ppu.renderCycle; }
auto PPU::noVRAMBlocking() const -> bool { return configuration.hacks.ppu.noVRAMBlocking; }
#define ppu ppuhd

PPU::PPU() {
  output = new uint32_t[2304 * 2160]();

  for(uint l : range(16)) {
    lightTable[l] = new uint32_t[32768];
    for(uint r : range(32)) {
      for(uint g : range(32)) {
        for(uint b : range(32)) {
          double luma = (double)l / 15.0;
          uint ar = (uint)(luma * r * 255.0 / 31.0 + 0.5);
          uint ag = (uint)(luma * g * 255.0 / 31.0 + 0.5);
          uint ab = (uint)(luma * b * 255.0 / 31.0 + 0.5);
          lightTable[l][r << 10 | g << 5 | b << 0] = ab << 16 | ag << 8 | ar << 0;
        }
      }
    }
  }

  for(uint y : range(240)) {
    lines[y].y = y;
  }
}

PPU::~PPU() {
  delete[] output;
  for(uint l : range(16)) delete[] lightTable[l];
}

auto PPU::synchronizeCPU() -> void {
  if(ppubase.clock >= 0) scheduler.resume(cpu.thread);
}

auto PPU::Enter() -> void {
  while(true) {
    scheduler.synchronize();
    ppu.main();
  }
}

auto PPU::step(uint clocks) -> void {
  tick(clocks);
  ppubase.clock += clocks;
  synchronizeCPU();
}

auto PPU::main() -> void {
  scanline();

  if(system.frameCounter == 0 && !system.runAhead) {
    uint y = vcounter();
    if(y >= 1 && y <= 239) {
      step(renderCycle());
      bool mosaicEnable = io.bg1.mosaicEnable || io.bg2.mosaicEnable || io.bg3.mosaicEnable || io.bg4.mosaicEnable;
      if(y == 1) {
        io.mosaic.counter = mosaicEnable ? io.mosaic.size + 1 : 0;
      }
      if(io.mosaic.counter && !--io.mosaic.counter) {
        io.mosaic.counter = mosaicEnable ? io.mosaic.size + 0 : 0;
      }
      lines[y].cache();
    }
  }

  step(hperiod() - hcounter());
}

auto PPU::scanline() -> void {
  if(vcounter() == 0) {
    if(latch.overscan && !io.overscan) {
      //when disabling overscan, clear the overscan area that won't be rendered to:
      for(uint y = 1; y <= 240; y++) {
        if(y >= 8 && y <= 231) continue;
        auto output = ppu.output + y * 1024;
        memory::fill<uint32>(output, 1024);
      }
    }

    ppubase.display.interlace = io.interlace;
    ppubase.display.overscan = io.overscan;
    latch.overscan = io.overscan;
    latch.hires = false;
    latch.hd = false;
    latch.ss = false;
    io.obj.timeOver = false;
    io.obj.rangeOver = false;
  }

  if(vcounter() > 0 && vcounter() < vdisp()) {
    latch.hires |= io.pseudoHires || io.bgMode == 5 || io.bgMode == 6;
    // HD PPU always keeps HD output. Official Fast supersampling averages down to 240p;
    // here supersampling is extra samples into the HD framebuffer instead.
    latch.hd |= io.bgMode == 7 && hdScale() > 1;
    latch.ss = false;
  }

  if(vcounter() == vdisp()) {
    if(!io.displayDisable) oamAddressReset();
  }

  if(vcounter() == 240) {
    Line::flush();
  }
}

auto PPU::refresh() -> void {
  if(system.frameCounter == 0 && !system.runAhead) {
    auto output = this->output;
    uint pitch, width, height;
    if(!hd()) {
      pitch  = 512 << !interlace();
      width  = 256 << hires();
      height = 240 << interlace();
    } else {
      pitch  = 256 * hdScale();
      width  = 256 * hdScale();
      height = 240 * hdScale();
    }

    //clear the areas of the screen that won't be rendered:
    //previous video frames may have drawn data here that would now be stale otherwise.
    if(!latch.overscan && pitch != frame.pitch && width != frame.width && height != frame.height) {
      for(uint y : range(240)) {
        if(y >= 8 && y <= 230) continue;  //these scanlines are always rendered.
        auto output = this->output + (!hd() ? (y * 1024 + (interlace() && field() ? 512 : 0)) : (y * 256 * hdScale() * hdScale()));
        auto width = (!hd() ? (!hires() ? 256 : 512) : (256 * hdScale() * hdScale()));
        memory::fill<uint32>(output, width);
      }
    }

    platform->videoFrame(output, pitch * sizeof(uint32), width, height, hd() ? hdScale() : 1);

    if(auto dump = getenv("BSNES_DUMP_FRAME")) {
      static uint dumped = 0;
      if(++dumped >= 4) {
        if(auto fp = fopen(dump, "wb")) {
          fprintf(fp, "P6\n%u %u\n255\n", width, height);
          auto src = output;
          for(uint row : range(height)) {
            for(uint col : range(width)) {
              uint32 c = src[col];
              fputc(c >>  0 & 255, fp);
              fputc(c >>  8 & 255, fp);
              fputc(c >> 16 & 255, fp);
            }
            src += pitch;
          }
          fclose(fp);
        }
        if(auto lg = fopen("/tmp/bsnes-hd-m7.txt", "w")) {
          fprintf(lg, "hd=%d ss=%d scale=%u persp=%d ssamp=%d mosaic=%d groups=%d\n",
            (int)hd(), (int)ss(), hdScale(), (int)hdPerspective(), (int)hdSupersample(), (int)hdMosaic(),
            mode7LineGroups.count);
          for(int i = 0; i < mode7LineGroups.count; i++) {
            fprintf(lg, "group %d start=%d end=%d lerp=%d..%d a0=%d a1=%d\n", i,
              mode7LineGroups.startLine[i], mode7LineGroups.endLine[i],
              mode7LineGroups.startLerpLine[i], mode7LineGroups.endLerpLine[i],
              (int16)lines[mode7LineGroups.startLerpLine[i]].io.mode7.a,
              (int16)lines[mode7LineGroups.endLerpLine[i]].io.mode7.a);
          }
          fclose(lg);
        }
        _exit(0);
      }
    }

    frame.pitch  = pitch;
    frame.width  = width;
    frame.height = height;
  }
  if(system.frameCounter++ >= system.frameSkip) system.frameCounter = 0;
}

auto PPU::prepareGpuMode7() -> void {
  auto table = lightTable[io.displayBrightness];
  if(!table) table = lightTable[15];
  auto pack = [&](uint palette) -> uint32 {
    uint32 color = 0;
    if(palette && table) {
      if(io.col.directColor) {
        uint16 dc = (palette << 2 & 0x001c) + (palette << 4 & 0x0380) + (palette << 7 & 0x6000);
        color = table[dc & 0x7fff];
      } else {
        color = table[cgram[palette] & 0x7fff];
      }
      if(!hdTrueColor()) {
        uint r = (color >>  0 & 255) * 31 / 255;
        uint g = (color >>  8 & 255) * 31 / 255;
        uint b = (color >> 16 & 255) * 31 / 255;
        color = b * 255 / 31 << 16 | g * 255 / 31 << 8 | r * 255 / 31;
      }
      color |= 0xff000000u;
    }
    return color;
  };
  uint32* map = gpuMode7.map;
  for(uint py = 0; py < 1024; py++) {
    for(uint px = 0; px < 1024; px++) {
      uint tile = vram[(py >> 3 & 127) * 128 + (px >> 3 & 127)] & 0xff;
      uint palette = vram[(((py & 7) << 3) + (px & 7)) + (tile << 6)] >> 8;
      *map++ = pack(palette);
    }
  }
  uint32* t0 = gpuMode7.tile0;
  for(uint py = 0; py < 8; py++) {
    for(uint px = 0; px < 8; px++) {
      uint palette = vram[((py & 7) << 3) + (px & 7)] >> 8;
      *t0++ = pack(palette);
    }
  }
}

auto PPU::load() -> bool {
  return true;
}

auto PPU::power(bool reset) -> void {
  PPUcounter::reset();
  memory::fill<uint32>(output, 1024 * 960);

  function<uint8 (uint, uint8)> reader{&PPU::readIO, this};
  function<void  (uint, uint8)> writer{&PPU::writeIO, this};
  bus.map(reader, writer, "00-3f,80-bf:2100-213f");

  if(!reset) {
    for(auto& word : vram) word = 0x0000;
    for(auto& color : cgram) color = 0x0000;
    for(auto& object : objects) object = {};
  }

  latch = {};
  io = {};
  updateVideoMode();

  #undef ppu
  ItemLimit = !configuration.hacks.ppu.hdNoSpriteLimit ? 32 : 128;
  TileLimit = !configuration.hacks.ppu.hdNoSpriteLimit ? 34 : 128;

  Line::start = 0;
  Line::count = 0;

  frame = {};
}

}
