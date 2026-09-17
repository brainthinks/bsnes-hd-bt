#include <sfc/sfc.hpp>
#include <emulator/hdtrace.hpp>
#include <emulator/ramtrace.hpp>

namespace SuperFamicom {

PPU& ppubase = ppu;

#define PPU PPUfast
#define ppu ppufast

PPU ppu;
#include "io.cpp"
#include "line.cpp"
#include "background.cpp"
#include "mode7.cpp"
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
auto PPU::hdScale() const -> uint { return configuration.hacks.ppu.mode7.scale; }
auto PPU::hdPerspective() const -> bool { return configuration.hacks.ppu.mode7.perspective; }
auto PPU::hdSupersample() const -> bool { return configuration.hacks.ppu.mode7.supersample; }
auto PPU::hdMosaic() const -> bool { return configuration.hacks.ppu.mode7.mosaic; }
auto PPU::deinterlace() const -> bool { return configuration.hacks.ppu.deinterlace; }
auto PPU::renderCycle() const -> uint { return configuration.hacks.ppu.renderCycle; }
auto PPU::noVRAMBlocking() const -> bool { return configuration.hacks.ppu.noVRAMBlocking; }
#define ppu ppufast

PPU::PPU() {
  output = new uint16_t[2304 * 2160]();

  for(uint l : range(16)) {
    lightTable[l] = new uint16_t[32768];
    for(uint r : range(32)) {
      for(uint g : range(32)) {
        for(uint b : range(32)) {
          double luma = (double)l / 15.0;
          uint ar = (luma * r + 0.5);
          uint ag = (luma * g + 0.5);
          uint ab = (luma * b + 0.5);
          lightTable[l][r << 10 | g << 5 | b << 0] = ab << 10 | ag << 5 | ar << 0;
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
        memory::fill<uint16>(output, 1024);
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
    //supersampling and EXTBG mode are not compatible, so disable supersampling in EXTBG mode
    latch.hd |= io.bgMode == 7 && hdScale() > 1 && (hdSupersample() == 0 || io.extbg == 1);
    latch.ss |= io.bgMode == 7 && hdScale() > 1 && (hdSupersample() == 1 && io.extbg == 0);
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
        memory::fill<uint16>(output, width);
      }
    }

    if(auto device = controllerPort2.device) device->draw(output, pitch * sizeof(uint16), width, height);
    platform->videoFrame(output, pitch * sizeof(uint16), width, height, hd() ? hdScale() : 1);

    //BSNES_FRAME_DIR + BSNES_FRAME_AT: write the chosen frames out as PPM.
    //The same hook the HD renderer has, because this is the renderer a headless
    //run actually uses - and a port with no reference picture can only be
    //judged by eye, which is how a wrong one survives.
    //
    //Five bits a channel rather than the HD path's packed 32-bit, widened by
    //repeating the high bits into the low ones: that maps 31 to 255 exactly,
    //where a plain shift would stop at 248 and tint every bright pixel.
    //
    //Red is the HIGH five bits here, not the low ones. This buffer is not in
    //CGRAM's order: lightTable maps an index of r<<10|g<<5|b to a value of
    //b<<10|g<<5|r, so it swaps the outer channels on the way through, and
    //target-bsnes/program/video.cpp reads the result back as r = color >> 10.
    //Taking the low bits for red instead produces a picture that looks
    //plausible and has red and blue exchanged - which, held up beside a port as
    //the reference, condemns the port for the reference's mistake.
    //Number the picture the way the recording numbers its frames, when one is
    //being made. Otherwise a dump and a frame record with the same number are
    //different moments - off by however many frames passed before recording
    //began - and comparing them looks like a renderer that is nearly right
    //rather than like a misalignment, which is a far harder mistake to see.
    unsigned dumpFrame = RamTrace::recorder().recording()
                       ? RamTrace::recorder().recordedFrames()
                       : HdTrace::frame();
    if(HdTrace::wantFrameDump(dumpFrame)) {
      char path[512];
      snprintf(path, sizeof(path), "%s/frame-%06u.ppm", getenv("BSNES_FRAME_DIR"), dumpFrame);
      if(auto fp = fopen(path, "wb")) {
        fprintf(fp, "P6\n%u %u\n255\n", width, height);
        auto src = output;
        for(uint row : range(height)) {
          for(uint col : range(width)) {
            uint16 c = src[col];
            uint r = c >> 10 & 31, g = c >>  5 & 31, b = c >>  0 & 31;
            fputc(r << 3 | r >> 2, fp);
            fputc(g << 3 | g >> 2, fp);
            fputc(b << 3 | b >> 2, fp);
          }
          src += pitch;
        }
        fclose(fp);
      }
    }

    //BSNES_FRAME_HASHES=<path>: 32 bytes a frame, SHA-256 of the same 256 by
    //224 RGB the PPM dump writes (8-row overscan crop, 5-bit channels widened
    //the same way). A stream, so a 20k-frame run is 640 KiB instead of a
    //directory of pictures. Compared against FZERO_FRAME_HASHES.
    if(auto hashes = getenv("BSNES_FRAME_HASHES")) {
      if(!hd() && width == 256 && height == 240) {
        static FILE* fp = nullptr;
        static bool failed = false;
        if(!fp && !failed) {
          fp = fopen(hashes, "wb");
          if(!fp) {
            failed = true;
            fprintf(stderr, "BSNES_FRAME_HASHES: cannot write %s\n", hashes);
          }
        }
        if(fp) {
          Hash::SHA256 sha;
          auto src = output + 8 * pitch;
          for(uint row : range(224)) {
            for(uint col : range(256)) {
              uint16 c = src[col];
              uint r = c >> 10 & 31, g = c >>  5 & 31, b = c >>  0 & 31;
              sha.input((uint8_t)(r << 3 | r >> 2));
              sha.input((uint8_t)(g << 3 | g >> 2));
              sha.input((uint8_t)(b << 3 | b >> 2));
            }
            src += pitch;
          }
          auto digest = sha.output();
          fwrite(digest.data(), 1, digest.size(), fp);
          fflush(fp);
        }
      }
    }

    frame.pitch  = pitch;
    frame.width  = width;
    frame.height = height;
  }
  if(system.frameCounter++ >= system.frameSkip) system.frameCounter = 0;
}

auto PPU::load() -> bool {
  return true;
}

auto PPU::power(bool reset) -> void {
  PPUcounter::reset();
  memory::fill<uint16>(output, 1024 * 960);

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
  ItemLimit = !configuration.hacks.ppu.noSpriteLimit ? 32 : 128;
  TileLimit = !configuration.hacks.ppu.noSpriteLimit ? 34 : 128;

  Line::start = 0;
  Line::count = 0;

  frame = {};
}

}
