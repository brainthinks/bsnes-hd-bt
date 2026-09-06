#pragma once

//Env-gated automation and tracing harness used to investigate HD PPU issues.
//Every entry point is a no-op unless its BSNES_* variable is set, so ordinary
//builds behave exactly as they did before.

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

  //-1: no script, leave input to the hardware. 0/1: scripted button state.
  inline auto scriptedInput(unsigned input) -> int {
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

  //BSNES_M7_EXTMAP=<file>   render Mode 7 through an extended map
  //BSNES_M7_EXTMAP_DUMP=<file>[,factor]  write a starter file from the current
  //                                      hardware map (factor 2 or 4)
  //BSNES_M7_EXTMAP_MARK=1   tint pixels that came from the extended map
  inline auto extendedMapPath() -> const char* { return getenv("BSNES_M7_EXTMAP"); }
  inline auto extendedMapDump() -> const char* { return getenv("BSNES_M7_EXTMAP_DUMP"); }
  inline auto extendedMapMark() -> bool {
    static int on = -1;
    if(on < 0) on = getenv("BSNES_M7_EXTMAP_MARK") ? 1 : 0;
    return on == 1;
  }

  //BSNES_M7_WORLD="7e00a8-2688,7e00aa-3504": where a game keeps the player's
  //world position, so streamed Mode 7 tiles can be remembered against it.
  inline auto worldOriginDescriptor() -> const char* { return getenv("BSNES_M7_WORLD"); }
  inline auto worldCacheLog2() -> unsigned {
    auto text = getenv("BSNES_M7_WORLD_BITS");
    unsigned bits = text ? (unsigned)atoi(text) : 20;
    return bits ? bits : 20;
  }

  inline auto timing() -> bool {
    static int on = -1;
    if(on < 0) on = getenv("BSNES_TIME_FRAME") ? 1 : 0;
    return on == 1;
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

}
