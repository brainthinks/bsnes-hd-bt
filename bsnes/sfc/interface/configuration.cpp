Configuration configuration;

auto Configuration::process(Markup::Node document, bool load) -> void {
  #define bind(type, path, name) \
    if(load) { \
      if(auto node = document[path]) name = node.type(); \
    } else { \
      document(path).setValue(name); \
    } \

  bind(natural, "System/CPU/Version", system.cpu.version);
  bind(natural, "System/PPU1/Version", system.ppu1.version);
  bind(natural, "System/PPU1/VRAM/Size", system.ppu1.vram.size);
  bind(natural, "System/PPU2/Version", system.ppu2.version);
  bind(text,    "System/Serialization/Method", system.serialization.method);

  bind(boolean, "Video/AspectCorrection", video.aspectCorrection);
  bind(boolean, "Video/Overscan", video.overscan);
  bind(boolean, "Video/BlurEmulation", video.blurEmulation);
  bind(boolean, "Video/ColorEmulation", video.colorEmulation);
  bind(natural, "Video/Luminance", video.luminance);
  bind(natural, "Video/Saturation", video.saturation);
  bind(natural, "Video/Gamma", video.gamma);

  bind(boolean, "Hacks/Hotfixes", hacks.hotfixes);
  bind(text,    "Hacks/Entropy", hacks.entropy);
  bind(natural, "Hacks/CPU/Overclock", hacks.cpu.overclock);
  bind(boolean, "Hacks/CPU/FastMath", hacks.cpu.fastMath);
  bind(boolean, "Hacks/PPU/Fast", hacks.ppu.fast);
  bind(boolean, "Hacks/PPU/HD", hacks.ppu.hd);
  bind(boolean, "Hacks/PPU/Deinterlace", hacks.ppu.deinterlace);
  bind(natural, "Hacks/PPU/RenderCycle", hacks.ppu.renderCycle);
  bind(boolean, "Hacks/PPU/NoSpriteLimit", hacks.ppu.noSpriteLimit);
  bind(boolean, "Hacks/PPU/HD-Deinterlace", hacks.ppu.hdDeinterlace);
  bind(boolean, "Hacks/PPU/HD-NoSpriteLimit", hacks.ppu.hdNoSpriteLimit);
  bind(boolean, "Hacks/PPU/HD-TrueColor", hacks.ppu.hdTrueColor);
  bind(boolean, "Hacks/PPU/NoVRAMBlocking", hacks.ppu.noVRAMBlocking);
  bind(natural, "Hacks/PPU/Mode7/Scale", hacks.ppu.mode7.scale);
  bind(boolean, "Hacks/PPU/Mode7/Perspective", hacks.ppu.mode7.perspective);
  bind(boolean, "Hacks/PPU/Mode7/Supersample", hacks.ppu.mode7.supersample);
  bind(boolean, "Hacks/PPU/Mode7/Mosaic", hacks.ppu.mode7.mosaic);
  bind(natural, "Hacks/PPU/HDMode7/Scale", hacks.ppu.hdMode7.scale);
  bind(boolean, "Hacks/PPU/HDMode7/Perspective", hacks.ppu.hdMode7.perspective);
  bind(boolean, "Hacks/PPU/HDMode7/Supersample", hacks.ppu.hdMode7.supersample);
  bind(natural, "Hacks/PPU/HDMode7/SsFactor", hacks.ppu.hdMode7.ssFactor);
  bind(boolean, "Hacks/PPU/HDMode7/Mosaic", hacks.ppu.hdMode7.mosaic);
  bind(boolean, "Hacks/PPU/HDMode7/GpuSupersample", hacks.ppu.hdMode7.gpuSupersample);
  bind(natural, "Hacks/PPU/HDMode7/WsMode", hacks.ppu.hdMode7.wsMode);
  bind(natural, "Hacks/PPU/HDMode7/Widescreen", hacks.ppu.hdMode7.widescreen);
  bind(natural, "Hacks/PPU/HDMode7/Wsbg1", hacks.ppu.hdMode7.wsbg1);
  bind(natural, "Hacks/PPU/HDMode7/Wsbg2", hacks.ppu.hdMode7.wsbg2);
  bind(natural, "Hacks/PPU/HDMode7/Wsbg3", hacks.ppu.hdMode7.wsbg3);
  bind(natural, "Hacks/PPU/HDMode7/Wsbg4", hacks.ppu.hdMode7.wsbg4);
  bind(natural, "Hacks/PPU/HDMode7/WsObj", hacks.ppu.hdMode7.wsobj);
  bind(natural, "Hacks/PPU/HDMode7/IgWin", hacks.ppu.hdMode7.igwin);
  bind(natural, "Hacks/PPU/HDMode7/IgWinX", hacks.ppu.hdMode7.igwinx);
  bind(natural, "Hacks/PPU/HDMode7/WsBgCol", hacks.ppu.hdMode7.wsBgCol);
  bind(boolean, "Hacks/DSP/Fast", hacks.dsp.fast);
  bind(boolean, "Hacks/DSP/Cubic", hacks.dsp.cubic);
  bind(boolean, "Hacks/DSP/EchoShadow", hacks.dsp.echoShadow);
  bind(boolean, "Hacks/Coprocessor/DelayedSync", hacks.coprocessor.delayedSync);
  bind(boolean, "Hacks/Coprocessor/PreferHLE", hacks.coprocessor.preferHLE);
  bind(natural, "Hacks/SA1/Overclock", hacks.sa1.overclock);
  bind(natural, "Hacks/SuperFX/Overclock", hacks.superfx.overclock);

  #undef bind
}

auto Configuration::read() -> string {
  Markup::Node document;
  process(document, false);
  return BML::serialize(document, " ");
}

auto Configuration::read(string name) -> string {
  auto document = BML::unserialize(read());
  return document[name].text();
}

auto Configuration::write(string configuration) -> bool {
  *this = {};

  if(auto document = BML::unserialize(configuration)) {
    return process(document, true), true;
  }

  return false;
}

auto Configuration::write(string name, string value) -> bool {
  if(SuperFamicom::system.loaded() && name.beginsWith("System/")) return false;

  auto document = BML::unserialize(read());
  if(auto node = document[name]) {
    node.setValue(value);
    return process(document, true), true;
  }

  return false;
}
