#pragma once

//Env-gated automation and tracing harness used to investigate HD PPU issues.
//Every entry point is a no-op unless its BSNES_* variable is set, so ordinary
//builds behave exactly as they did before.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace HdTrace {

  inline auto frameCounter() -> unsigned& {
    static unsigned n = 0;
    return n;
  }

  inline auto frame() -> unsigned { return frameCounter(); }
  inline auto advanceFrame() -> void { frameCounter()++; }

  //BSNES_SCRIPT_INPUT="30:right,90:b+right,150:none"
  //The named buttons are held from that frame until the next waypoint.
  //Names: up down left right b a y x l r select start none, joined with '+'.
  struct Waypoint { unsigned frame; unsigned mask; };

  inline auto buttonIndex(const char* name, unsigned length) -> int {
    static const char* names[] = {
      "up", "down", "left", "right", "b", "a", "y", "x", "l", "r", "select", "start"
    };
    for(int i = 0; i < 12; i++) {
      if(strlen(names[i]) == length && !strncmp(names[i], name, length)) return i;
    }
    return -1;  //"none" and anything unrecognised
  }

  inline auto script() -> const Waypoint* {
    static Waypoint points[64];
    static int count = -1;
    if(count < 0) {
      count = 0;
      if(auto text = getenv("BSNES_SCRIPT_INPUT")) {
        const char* p = text;
        while(*p && count < 63) {
          unsigned at = (unsigned)strtoul(p, (char**)&p, 10);
          unsigned mask = 0;
          if(*p == ':') {
            p++;
            while(*p && *p != ',') {
              const char* name = p;
              while(*p && *p != ',' && *p != '+') p++;
              int button = buttonIndex(name, (unsigned)(p - name));
              if(button >= 0) mask |= 1u << button;
              if(*p == '+') p++;
            }
          }
          points[count++] = {at, mask};
          if(*p == ',') p++;
        }
      }
      points[count] = {0xffffffffu, 0};
    }
    return count ? points : nullptr;
  }

  //BSNES_SCRIPT_FILE=<path>: one little-endian uint16 per frame, in the
  //gamepad enum's bit order -- up down left right b a y x l r select start,
  //bit 0 to bit 11. Waypoints top out at 64 and are meant to be written by
  //hand; this is for input a program worked out, where the buttons change
  //every frame and there are thousands of them. Past the end of the file
  //nothing is held.
  inline auto scriptFile() -> const uint16_t* {
    static uint16_t* frames = nullptr;
    static long count = -1;
    if(count < 0) {
      count = 0;
      if(auto path = getenv("BSNES_SCRIPT_FILE")) {
        if(auto file = fopen(path, "rb")) {
          fseek(file, 0, SEEK_END);
          long size = ftell(file);
          fseek(file, 0, SEEK_SET);
          count = size / 2;
          if(count > 0) {
            frames = new uint16_t[count];
            if(fread(frames, 2, (size_t)count, file) != (size_t)count) count = 0;
          }
          fclose(file);
        }
      }
    }
    return count > 0 ? frames : nullptr;
  }

  inline auto scriptFileCount() -> long {
    scriptFile();
    static long remembered = -1;
    if(remembered < 0) {
      remembered = 0;
      if(auto path = getenv("BSNES_SCRIPT_FILE")) {
        if(auto file = fopen(path, "rb")) {
          fseek(file, 0, SEEK_END);
          remembered = ftell(file) / 2;
          fclose(file);
        }
      }
    }
    return remembered;
  }

  //-1: no script, leave input to the hardware. 0/1: scripted button state.
  inline auto scriptedInput(unsigned input) -> int {
    if(auto frames = scriptFile()) {
      unsigned held = 0;
      long at = (long)frame();
      if(at >= 0 && at < scriptFileCount()) held = frames[at];
      return input < 12 && (held >> input & 1) ? 1 : 0;
    }
    auto points = script();
    if(!points) return -1;
    unsigned held = 0;
    for(int i = 0; points[i].frame != 0xffffffffu; i++) {
      if(frame() >= points[i].frame) held = points[i].mask;
    }
    return input < 12 && (held >> input & 1) ? 1 : 0;
  }

  //BSNES_HEADLESS=1: skip presenting the frame. Measurement runs still get the
  //PPU's own frame dumps, without waiting on the host's compositor.
  inline auto headless() -> bool {
    static int on = -1;
    if(on < 0) on = getenv("BSNES_HEADLESS") ? 1 : 0;
    return on == 1;
  }

  //BSNES_NO_PAN=1: ignore panorama layouts and use plain hardware wrapping, to
  //A/B the widescreen extension against the previous behaviour.
  inline auto noPanoramas() -> bool {
    static int on = -1;
    if(on < 0) on = getenv("BSNES_NO_PAN") ? 1 : 0;
    return on == 1;
  }

  inline auto timing() -> bool {
    static int on = -1;
    if(on < 0) on = getenv("BSNES_TIME_FRAME") ? 1 : 0;
    return on == 1;
  }

  //BSNES_SAVE_STATE_AT=<frame>:<slot>: write a save state at that frame, so a
  //hard-to-reach moment becomes a starting point like any other. The five
  //states that ship beside a ROM are wherever somebody happened to stop; this
  //makes "the flag has just dropped on a Grand Prix" reachable in one step,
  //and deterministic, which a cold boot is not.
  inline auto saveStateAt(unsigned& slot) -> unsigned {
    static int at = -1;
    static unsigned which = 1;
    if(at < 0) {
      at = 0;
      if(auto text = getenv("BSNES_SAVE_STATE_AT")) {
        char* end = nullptr;
        at = (int)strtoul(text, &end, 10);
        if(end && *end == ':') which = (unsigned)strtoul(end + 1, nullptr, 10);
      }
    }
    slot = which;
    return (unsigned)at;
  }

  inline auto quitAfter() -> unsigned {
    static unsigned n = 0xffffffffu;
    if(n == 0xffffffffu) {
      auto text = getenv("BSNES_QUIT_AFTER");
      n = text ? (unsigned)strtoul(text, nullptr, 10) : 0;
    }
    return n;
  }

  inline auto inList(const char* text, unsigned f) -> bool {
    if(!text) return false;
    const char* p = text;
    while(*p) {
      unsigned at = (unsigned)strtoul(p, (char**)&p, 10);
      if(at == f) return true;
      if(*p) p++;
    }
    return false;
  }

  //BSNES_DUMP_VRAM_AT="120,180": frames whose tilemap should be written out.
  inline auto wantDumpVram(unsigned f) -> bool {
    return inList(getenv("BSNES_DUMP_VRAM_AT"), f);
  }

  //BSNES_FRAME_AT="120,180" together with BSNES_FRAME_DIR selects frames to dump.
  inline auto wantFrameDump(unsigned f) -> bool {
    if(!getenv("BSNES_FRAME_DIR")) return false;
    return inList(getenv("BSNES_FRAME_AT"), f);
  }

  //BSNES_FRAME_HASHES=<path> is the stream form of the same pictures: 32 bytes
  //a frame, written from PPU::refresh in ppu-fast. Not gated on FRAME_AT.

}
