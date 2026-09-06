auto EnhancementSettings::create() -> void {
  setCollapsible();
  setVisible(false);

  runAheadLabel.setText("Run-Ahead").setFont(Font().setBold());
  runAhead0.setText("Disabled").onActivate([&] {
    settings.emulator.runAhead.frames = 0;
  });
  runAhead1.setText("One Frame").onActivate([&] {
    settings.emulator.runAhead.frames = 1;
  });
  runAhead2.setText("Two Frames").onActivate([&] {
    settings.emulator.runAhead.frames = 2;
  });
  runAhead3.setText("Three Frames").onActivate([&] {
    settings.emulator.runAhead.frames = 3;
  });
  runAhead4.setText("Four Frames").onActivate([&] {
    settings.emulator.runAhead.frames = 4;
  });
  if(settings.emulator.runAhead.frames == 0) runAhead0.setChecked();
  if(settings.emulator.runAhead.frames == 1) runAhead1.setChecked();
  if(settings.emulator.runAhead.frames == 2) runAhead2.setChecked();
  if(settings.emulator.runAhead.frames == 3) runAhead3.setChecked();
  if(settings.emulator.runAhead.frames == 4) runAhead4.setChecked();
  runAheadSpacer.setColor({192, 192, 192});

  overclockingLabel.setText("Overclocking").setFont(Font().setBold());
  overclockingLayout.setSize({3, 3});
  overclockingLayout.column(0).setAlignment(1.0);
  overclockingLayout.column(1).setAlignment(0.5);

  cpuLabel.setText("CPU:");
  cpuClock.setLength(301).setPosition((settings.emulator.hack.cpu.overclock - 100)).onChange([&] {
    settings.emulator.hack.cpu.overclock = cpuClock.position() + 100;
    emulator->configure("Hacks/CPU/Overclock", settings.emulator.hack.cpu.overclock);
    cpuValue.setText({settings.emulator.hack.cpu.overclock, "%"});
  }).doChange();

  sa1Label.setText("SA-1:");
  sa1Clock.setLength(301).setPosition((settings.emulator.hack.sa1.overclock - 100)).onChange([&] {
    settings.emulator.hack.sa1.overclock = sa1Clock.position() + 100;
    emulator->configure("Hacks/SA1/Overclock", settings.emulator.hack.sa1.overclock);
    sa1Value.setText({settings.emulator.hack.sa1.overclock, "%"});
  }).doChange();

  sfxLabel.setText("SuperFX:");
  sfxClock.setLength(141).setPosition((settings.emulator.hack.superfx.overclock - 100) / 5).onChange([&] {
    settings.emulator.hack.superfx.overclock = sfxClock.position() * 5 + 100;
    emulator->configure("Hacks/SuperFX/Overclock", settings.emulator.hack.superfx.overclock);
    sfxValue.setText({settings.emulator.hack.superfx.overclock, "%"});
  }).doChange();

  overclockingSpacer.setColor({192, 192, 192});
  dspSpacer.setColor({192, 192, 192});
  coprocessorHeaderSpacer.setColor({192, 192, 192});

  ppuLabel.setText("PPU (video)").setFont(Font().setBold());
  auto updateScanlineOptions = [&] {
    bool scanline = settings.emulator.hack.ppu.fast || settings.emulator.hack.ppu.hd;
    noSpriteLimit.setEnabled(scanline);
    deinterlace.setEnabled(scanline);
    mode7Layout.setEnabled(scanline);
  };
  ppuRendererLabel.setText("Renderer:");
  ppuRenderer.append(ComboButtonItem().setText("Accurate"));
  ppuRenderer.append(ComboButtonItem().setText("Fast"));
  ppuRenderer.append(ComboButtonItem().setText("HD"));
  {
    uint index = 1;
    if(settings.emulator.hack.ppu.hd) index = 2;
    else if(!settings.emulator.hack.ppu.fast) index = 0;
    ppuRenderer.item(index).setSelected();
  }
  auto activeMode7 = [&]() -> decltype(settings.emulator.hack.ppu.mode7)& {
    return settings.emulator.hack.ppu.hd ? settings.emulator.hack.ppu.hdMode7 : settings.emulator.hack.ppu.mode7;
  };
  auto mode7Path = [&](string name) -> string {
    return {settings.emulator.hack.ppu.hd ? "Hacks/PPU/HDMode7/" : "Hacks/PPU/Mode7/", name};
  };
  ppuRenderer.onChange([&] {
    if(loadingMode7) return;
    if(auto item = ppuRenderer.selected()) {
      uint index = item.offset();
      settings.emulator.hack.ppu.fast = index == 1;
      settings.emulator.hack.ppu.hd = index == 2;
      ppuRendererUpdate.setText(index != program.ppuRendererActive ? "Change" : "Reload");
      // Do not update sibling widgets here: GTK crashes if the combo
      // layout is mutated while still inside this changed handler.
      mode7Refresh.setInterval(1);
      mode7Refresh.setEnabled(true);
    }
  });
  ppuRendererUpdate.setText("Change").onActivate([&] { ppuRendererChange(); });
  updateScanlineOptions();
  deinterlace.setText("Deinterlace").onToggle([&] {
    if(loadingMode7) return;
    if(settings.emulator.hack.ppu.hd) {
      settings.emulator.hack.ppu.hdDeinterlace = deinterlace.checked();
      emulator->configure("Hacks/PPU/HD-Deinterlace", settings.emulator.hack.ppu.hdDeinterlace);
    } else {
      settings.emulator.hack.ppu.deinterlace = deinterlace.checked();
      emulator->configure("Hacks/PPU/Deinterlace", settings.emulator.hack.ppu.deinterlace);
    }
  });
  noSpriteLimit.setText("No sprite limit").onToggle([&] {
    if(loadingMode7) return;
    if(settings.emulator.hack.ppu.hd) {
      settings.emulator.hack.ppu.hdNoSpriteLimit = noSpriteLimit.checked();
      emulator->configure("Hacks/PPU/HD-NoSpriteLimit", settings.emulator.hack.ppu.hdNoSpriteLimit);
    } else {
      settings.emulator.hack.ppu.noSpriteLimit = noSpriteLimit.checked();
    }
  });
  hdTrueColor.setCollapsible();
  hdTrueColor.setText("24-bit color").setToolTip(
    "When enabled, HD blends in 8-bit per channel.\n"
    "When disabled, color math matches the 15-bit CGRAM path (more banding)."
  ).onToggle([&] {
    if(loadingMode7) return;
    settings.emulator.hack.ppu.hdTrueColor = hdTrueColor.checked();
    emulator->configure("Hacks/PPU/HD-TrueColor", settings.emulator.hack.ppu.hdTrueColor);
  });

  mode7Label.setText("HD Mode 7").setFont(Font().setBold());
  mode7SamplerLabel.setCollapsible();
  mode7Sampler.setCollapsible();
  mode7SsFactorLabel.setCollapsible();
  mode7SsFactor.setCollapsible();
  mode7Perspective.setCollapsible();
  mode7Supersample.setCollapsible();
  mode7Mosaic.setCollapsible();
  mode7ScaleLabel.setText("Scale:");
  mode7Scale.append(ComboButtonItem().setText( "240p").setAttribute("multiplier", 1));
  mode7Scale.append(ComboButtonItem().setText( "480p").setAttribute("multiplier", 2));
  mode7Scale.append(ComboButtonItem().setText( "720p").setAttribute("multiplier", 3));
  mode7Scale.append(ComboButtonItem().setText( "960p").setAttribute("multiplier", 4));
  mode7Scale.append(ComboButtonItem().setText("1200p").setAttribute("multiplier", 5));
  mode7Scale.append(ComboButtonItem().setText("1440p").setAttribute("multiplier", 6));
  mode7Scale.append(ComboButtonItem().setText("1680p").setAttribute("multiplier", 7));
  mode7Scale.append(ComboButtonItem().setText("1920p").setAttribute("multiplier", 8));
  mode7Scale.onChange([&] {
    if(loadingMode7) return;
    if(auto item = mode7Scale.selected()) {
      activeMode7().scale = item.attribute("multiplier").natural();
      emulator->configure(mode7Path("Scale"), activeMode7().scale);
    }
  });
  mode7Perspective.setText("Perspective correction").onToggle([&] {
    if(loadingMode7) return;
    activeMode7().perspective = mode7Perspective.checked();
    emulator->configure(mode7Path("Perspective"), activeMode7().perspective);
  });
  mode7Supersample.setText("Supersampling").onToggle([&] {
    if(loadingMode7) return;
    activeMode7().supersample = mode7Supersample.checked();
    emulator->configure(mode7Path("Supersample"), activeMode7().supersample);
  });
  mode7SsFactorLabel.setText("Supersampling:");
  mode7SsFactor.append(ComboButtonItem().setText("Off").setAttribute("factor", 1));
  for(uint n = 2; n <= 16; n++) {
    mode7SsFactor.append(ComboButtonItem().setText({n, "×"}).setAttribute("factor", n));
  }
  mode7SsFactor.onChange([&] {
    if(loadingMode7) return;
    if(auto item = mode7SsFactor.selected()) {
      auto& m7 = activeMode7();
      m7.ssFactor = item.attribute("factor").natural();
      m7.supersample = m7.ssFactor > 1;
      emulator->configure(mode7Path("SsFactor"), m7.ssFactor);
      emulator->configure(mode7Path("Supersample"), m7.supersample);
    }
  });
  mode7SamplerLabel.setText("Sampler:");
  mode7Sampler.append(ComboButtonItem().setText("CPU"));
  mode7Sampler.append(ComboButtonItem().setText("GPU"));
  mode7Sampler.setToolTip(
    "CPU: extra Mode 7 samples on the CPU.\n"
    "GPU: extra Mode 7 samples on OpenGL 3.2 (required). Default."
  );
  mode7Sampler.onChange([&] {
    if(loadingMode7) return;
    bool gpu = false;
    if(auto item = mode7Sampler.selected()) gpu = item.offset() == 1;
    if(gpu && settings.emulator.hack.ppu.hd && !program.videoSupportsHdGpu()) {
      MessageDialog({
        "Error: the ", video.driver(), " video driver does not support HD PPU.\n"
        "GPU sampling requires OpenGL 3.2."
      }).setAlignment(*settingsWindow).error();
      loadingMode7 = true;
      mode7Sampler.item(0).setSelected();
      loadingMode7 = false;
      gpu = false;
    }
    settings.emulator.hack.ppu.hdMode7.gpuSupersample = gpu;
    emulator->configure("Hacks/PPU/HDMode7/GpuSupersample", gpu);
  });
  mode7Mosaic.setText("HD->SD Mosaic").onToggle([&] {
    if(loadingMode7) return;
    activeMode7().mosaic = mode7Mosaic.checked();
    emulator->configure(mode7Path("Mosaic"), activeMode7().mosaic);
  });
  mode7WsModeLabel.setText("Widescreen:");
  mode7WsMode.append(ComboButtonItem().setText("Off").setAttribute("mode", 0));
  mode7WsMode.append(ComboButtonItem().setText("Mode 7").setAttribute("mode", 1));
  mode7WsMode.append(ComboButtonItem().setText("All").setAttribute("mode", 2));
  mode7WsMode.onChange([&] {
    if(loadingMode7) return;
    if(auto item = mode7WsMode.selected()) {
      settings.emulator.hack.ppu.hdMode7.wsMode = item.attribute("mode").natural();
      emulator->configure("Hacks/PPU/HDMode7/WsMode", settings.emulator.hack.ppu.hdMode7.wsMode);
    }
  });
  mode7WsAspectLabel.setText("Aspect:");
  mode7WsAspect.append(ComboButtonItem().setText("16:9").setAttribute("ws", 1609));
  mode7WsAspect.append(ComboButtonItem().setText("16:10").setAttribute("ws", 1610));
  mode7WsAspect.append(ComboButtonItem().setText("21:9").setAttribute("ws", 2109));
  mode7WsAspect.append(ComboButtonItem().setText("32:9").setAttribute("ws", 3209));
  mode7WsAspect.append(ComboButtonItem().setText("2:1").setAttribute("ws", 201));
  mode7WsAspect.append(ComboButtonItem().setText("4:3").setAttribute("ws", 403));
  mode7WsAspect.onChange([&] {
    if(loadingMode7) return;
    if(auto item = mode7WsAspect.selected()) {
      settings.emulator.hack.ppu.hdMode7.widescreen = item.attribute("ws").natural();
      emulator->configure("Hacks/PPU/HDMode7/Widescreen", settings.emulator.hack.ppu.hdMode7.widescreen);
    }
  });
  auto fillWsBg = [&](ComboButton& combo) {
    static const char* labels[] = {
      "Off", "On",
      "<40", ">40", "<80", ">80", "<120", ">120", "<160", ">160", "<200", ">200",
      "crop", "cropAuto", "disable", "autoHor", "autoHor&Ver"
    };
    for(uint n : range(17)) {
      combo.append(ComboButtonItem().setText(labels[n]).setAttribute("conf", n));
    }
  };
  auto bindWsBg = [&](ComboButton& combo, uint& value, string key) {
    combo.onChange([&, key] {
      if(loadingMode7) return;
      if(auto item = combo.selected()) {
        value = item.attribute("conf").natural();
        emulator->configure({"Hacks/PPU/HDMode7/", key}, value);
      }
    });
  };
  mode7WsBg1Label.setText("BG1:");
  mode7WsBg2Label.setText("BG2:");
  mode7WsBg3Label.setText("BG3:");
  mode7WsBg4Label.setText("BG4:");
  fillWsBg(mode7WsBg1);
  fillWsBg(mode7WsBg2);
  fillWsBg(mode7WsBg3);
  fillWsBg(mode7WsBg4);
  mode7WsBg1.setToolTip("Widescreen for background layer 1. autoHor&Ver (default) keeps screen-locked HUDs at 256. On walks the tilemap into extra columns.");
  mode7WsBg2.setToolTip("Widescreen for background layer 2.");
  mode7WsBg3.setToolTip("Widescreen for background layer 3.");
  mode7WsBg4.setToolTip("Widescreen for background layer 4.");
  bindWsBg(mode7WsBg1, settings.emulator.hack.ppu.hdMode7.wsbg1, "Wsbg1");
  bindWsBg(mode7WsBg2, settings.emulator.hack.ppu.hdMode7.wsbg2, "Wsbg2");
  bindWsBg(mode7WsBg3, settings.emulator.hack.ppu.hdMode7.wsbg3, "Wsbg3");
  bindWsBg(mode7WsBg4, settings.emulator.hack.ppu.hdMode7.wsbg4, "Wsbg4");
  mode7Refresh.onActivate([&] {
    mode7Refresh.setEnabled(false);
    reloadMode7Widgets();
  });
  reloadMode7Widgets();
  ppuRendererChanged();

  dspLabel.setText("DSP (audio)").setFont(Font().setBold());
  fastDSP.setText("Fast mode").setChecked(settings.emulator.hack.dsp.fast).onToggle([&] {
    settings.emulator.hack.dsp.fast = fastDSP.checked();
    emulator->configure("Hacks/DSP/Fast", settings.emulator.hack.dsp.fast);
  });
  cubicInterpolation.setText("Cubic interpolation").setChecked(settings.emulator.hack.dsp.cubic).onToggle([&] {
    settings.emulator.hack.dsp.cubic = cubicInterpolation.checked();
    emulator->configure("Hacks/DSP/Cubic", settings.emulator.hack.dsp.cubic);
  });

  coprocessorLabel.setText("Coprocessors").setFont(Font().setBold());
  coprocessorDelayedSyncOption.setText("Fast mode").setChecked(settings.emulator.hack.coprocessor.delayedSync).onToggle([&] {
    settings.emulator.hack.coprocessor.delayedSync = coprocessorDelayedSyncOption.checked();
  });
  coprocessorPreferHLEOption.setText("Prefer HLE").setChecked(settings.emulator.hack.coprocessor.preferHLE).setToolTip(
    "When checked, less accurate HLE emulation will always be used when available.\n"
    "When unchecked, HLE will only be used when LLE firmware is missing."
  ).onToggle([&] {
    settings.emulator.hack.coprocessor.preferHLE = coprocessorPreferHLEOption.checked();
  });
  coprocessorSpacer.setColor({192, 192, 192});

  gameLabel.setText("Game Enhancements").setFont(Font().setBold());
  hotfixes.setText("Hotfixes").setToolTip({
    "Even commercially licensed and officially released software sometimes shipped with bugs.\n"
    "This option will correct certain issues that occurred even on real hardware."
  }).setChecked(settings.emulator.hack.hotfixes).onToggle([&] {
    settings.emulator.hack.hotfixes = hotfixes.checked();
  });

  note.setText("Note: use Change/Reload to apply a new renderer. HD Mode 7 options apply immediately while HD is active.");
}

auto EnhancementSettings::reloadMode7Widgets() -> void {
  loadingMode7 = true;
  auto& m7 = settings.emulator.hack.ppu.hd ? settings.emulator.hack.ppu.hdMode7 : settings.emulator.hack.ppu.mode7;
  bool hd = settings.emulator.hack.ppu.hd;
  for(uint n = 1; n <= 8; n++) {
    if(m7.scale == n) mode7Scale.item(n - 1).setSelected();
  }
  mode7Perspective.setChecked(hd ? true : m7.perspective);
  mode7Perspective.setVisible(!hd);
  mode7Supersample.setChecked(m7.supersample);
  mode7Mosaic.setChecked(m7.mosaic);
  mode7Supersample.setVisible(!hd);
  mode7SsFactorLabel.setVisible(hd);
  mode7SsFactor.setVisible(hd);
  mode7SamplerLabel.setVisible(hd);
  mode7Sampler.setVisible(hd);
  mode7Sampler.item(settings.emulator.hack.ppu.hdMode7.gpuSupersample ? 1 : 0).setSelected();
  mode7Mosaic.setVisible(!hd);
  mode7WsModeLabel.setVisible(hd);
  mode7WsMode.setVisible(hd);
  mode7WsAspectLabel.setVisible(hd);
  mode7WsAspect.setVisible(hd);
  mode7WsBg1Label.setVisible(hd);
  mode7WsBg1.setVisible(hd);
  mode7WsBg2Label.setVisible(hd);
  mode7WsBg2.setVisible(hd);
  mode7WsBg3Label.setVisible(hd);
  mode7WsBg3.setVisible(hd);
  mode7WsBg4Label.setVisible(hd);
  mode7WsBg4.setVisible(hd);
  {
    uint mode = settings.emulator.hack.ppu.hdMode7.wsMode;
    if(mode > 2) mode = 0;
    mode7WsMode.item(mode).setSelected();
    uint ws = settings.emulator.hack.ppu.hdMode7.widescreen;
    for(uint n : range(mode7WsAspect.itemCount())) {
      if(mode7WsAspect.item(n).attribute("ws").natural() == ws) {
        mode7WsAspect.item(n).setSelected();
      }
    }
    auto selectBg = [&](ComboButton& combo, uint value) {
      if(value > 16) value = 16;
      combo.item(value).setSelected();
    };
    selectBg(mode7WsBg1, settings.emulator.hack.ppu.hdMode7.wsbg1);
    selectBg(mode7WsBg2, settings.emulator.hack.ppu.hdMode7.wsbg2);
    selectBg(mode7WsBg3, settings.emulator.hack.ppu.hdMode7.wsbg3);
    selectBg(mode7WsBg4, settings.emulator.hack.ppu.hdMode7.wsbg4);
  }
  uint factor = m7.ssFactor < 1 ? 1 : m7.ssFactor;
  for(uint n : range(mode7SsFactor.itemCount())) {
    if(mode7SsFactor.item(n).attribute("factor").natural() == factor) {
      mode7SsFactor.item(n).setSelected();
    }
  }
  mode7Label.setText(hd ? "HD Mode 7 (HD PPU)" : "HD Mode 7 (Fast PPU)");
  deinterlace.setChecked(hd ? settings.emulator.hack.ppu.hdDeinterlace : settings.emulator.hack.ppu.deinterlace);
  noSpriteLimit.setChecked(hd ? settings.emulator.hack.ppu.hdNoSpriteLimit : settings.emulator.hack.ppu.noSpriteLimit);
  hdTrueColor.setVisible(hd);
  hdTrueColor.setChecked(settings.emulator.hack.ppu.hdTrueColor);
  bool scanline = settings.emulator.hack.ppu.fast || settings.emulator.hack.ppu.hd;
  deinterlace.setEnabled(scanline);
  noSpriteLimit.setEnabled(scanline);
  mode7Layout.setEnabled(scanline);
  ppuScanlineLayout.resize();
  loadingMode7 = false;
  mode7Layout.resize();
  mode7WsLayout.resize();
  mode7WsBgLayout.resize();
}

auto EnhancementSettings::ppuRendererChanged() -> void {
  string name = "Fast";
  if(program.ppuRendererActive == 2) name = "HD";
  else if(program.ppuRendererActive == 0) name = "Accurate";
  ppuRendererActiveLabel.setText({"Active renderer: ", name});
  uint selected = program.ppuRendererActive;
  if(auto item = ppuRenderer.selected()) selected = item.offset();
  ppuRendererUpdate.setText(selected != program.ppuRendererActive ? "Change" : "Reload");
}

auto EnhancementSettings::ppuRendererChange() -> void {
  program.applyPPURenderer(settingsWindow);
}
