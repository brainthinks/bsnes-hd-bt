struct Configuration {
  auto read() -> string;
  auto read(string) -> string;
  auto write(string) -> bool;
  auto write(string, string) -> bool;

  struct System {
    struct CPU {
      uint version = 2;
    } cpu;
    struct PPU1 {
      uint version = 1;
      struct VRAM {
        uint size = 0x10000;
      } vram;
    } ppu1;
    struct PPU2 {
      uint version = 3;
    } ppu2;
    struct Serialization {
      string method = "Fast";
    } serialization;
  } system;

  struct Video {
    bool blurEmulation = true;
    bool colorEmulation = true;
    uint luminance = 100;
    uint saturation = 100;
    uint gamma = 100;
  } video;

  struct Hacks {
    bool hotfixes = true;
    string entropy = "Low";
    struct CPU {
      uint overclock = 100;
      bool fastMath = false;
    } cpu;
    struct PPU {
      bool fast = true;
      bool hd = false;
      bool deinterlace = true;
      bool noSpriteLimit = false;
      bool noVRAMBlocking = false;
      bool hdDeinterlace = true;
      bool hdNoSpriteLimit = true;
      bool hdTrueColor = true;
      uint renderCycle = 512;
      struct Mode7 {
        uint scale = 1;
        bool perspective = true;
        bool supersample = false;
        uint ssFactor = 1;
        bool mosaic = true;
        bool gpuSupersample = false;
      } mode7, hdMode7{5, true, true, 4, false, true};
    } ppu;
    struct DSP {
      bool fast = true;
      bool cubic = false;
      bool echoShadow = false;
    } dsp;
    struct Coprocessor {
      bool delayedSync = true;
      bool preferHLE = false;
    } coprocessor;
    struct SA1 {
      uint overclock = 100;
    } sa1;
    struct SuperFX {
      uint overclock = 100;
    } superfx;
  } hacks;

private:
  auto process(Markup::Node document, bool load) -> void;
};

extern Configuration configuration;
