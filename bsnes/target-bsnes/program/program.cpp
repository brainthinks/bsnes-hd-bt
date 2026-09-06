#include "../bsnes.hpp"
#include "platform.cpp"
#include "game.cpp"
#include "game-pak.cpp"
#include "game-rom.cpp"
#include "paths.cpp"
#include "states.cpp"
#include "movies.cpp"
#include "rewind.cpp"
#include "video.cpp"
#include "audio.cpp"
#include "input.cpp"
#include "utility.cpp"
#include "patch.cpp"
#include "hacks.cpp"
#include "filter.cpp"
#include "viewport.cpp"
Program program;

auto Program::create() -> void {
  Emulator::platform = this;

  presentation.create();
  presentation.setVisible();
  presentation.viewport.setFocused();

  settingsWindow.create();
  videoSettings.create();
  audioSettings.create();
  inputSettings.create();
  hotkeySettings.create();
  pathSettings.create();
  emulatorSettings.create();
  enhancementSettings.create();
  compatibilitySettings.create();
  driverSettings.create();

  toolsWindow.create();
  cheatFinder.create();
  cheatDatabase.create();
  cheatWindow.create();
  cheatEditor.create();
  stateWindow.create();
  stateManager.create();
  manifestViewer.create();

  if(settings.general.crashed) {
    MessageDialog(
      "Driver crash detected. Hardware drivers have been disabled.\n"
      "Please reconfigure drivers in the advanced settings panel."
    ).setAlignment(*presentation).information();
    settings.video.driver = "None";
    settings.audio.driver = "None";
    settings.input.driver = "None";
  }

  settings.general.crashed = true;
  settings.save();
  updateVideoDriver(presentation);
  updateAudioDriver(presentation);
  updateInputDriver(presentation);
  settings.general.crashed = false;
  settings.save();

  driverSettings.videoDriverChanged();
  driverSettings.audioDriverChanged();
  driverSettings.inputDriverChanged();

  if(settings.emulator.hack.ppu.hd
  && settings.emulator.hack.ppu.hdMode7.gpuSupersample
  && !videoSupportsHdGpu()) {
    selectFastPpuDueToDriver(presentation);
  } else {
    ppuRendererActive = settings.emulator.hack.ppu.hd ? 2
      : settings.emulator.hack.ppu.fast ? 1 : 0;
    enhancementSettings.ppuRendererChanged();
  }

  if(gameQueue) load();
  if(startFullScreen && emulator->loaded()) {
    toggleVideoFullScreen();
  }
  Application::onMain({&Program::main, this});
}

auto Program::main() -> void {
  updateStatus();
  video.poll();

  if(Application::modal()) {
    audio.clear();
    return;
  }

  inputManager.poll();
  inputManager.pollHotkeys();
  if(auto after = getenv("BSNES_TOGGLE_FS_AFTER")) {
    static uint frames = 0;
    uint want = (uint)max(1, atoi(after));
    if(++frames == want && emulator->loaded() && !video.fullScreen()) {
      toggleVideoFullScreen();
    }
  }

  static bool previouslyInactive = true;
  bool currentlyInactive = inactive();

  // check if emulator has transitioned from active to inactive or vice versa
  if(previouslyInactive != currentlyInactive) {
    previouslyInactive = currentlyInactive;
    Application::setScreenSaver(currentlyInactive || settings.general.screenSaver);
  }

  if(currentlyInactive) {
    audio.clear();
    usleep(20 * 1000);
    if(settings.emulator.runAhead.frames == 0) viewportRefresh();
    return;
  }

  rewindRun();

  HdTrace::advanceFrame();
  if(auto limit = HdTrace::quitAfter(); limit && HdTrace::frame() > limit) return program.quit();

  //BSNES_TIME_FRAME=1: where the wall clock goes, for measurement runs
  static uint64 runNs = 0, loopNs = 0, lastEnd = 0;
  bool timing = HdTrace::timing();
  uint64 t0 = 0;
  if(timing) {
    t0 = chrono::nanosecond();
    if(lastEnd) loopNs += t0 - lastEnd;
  }

  if(!settings.emulator.runAhead.frames || fastForwarding || rewinding) {
    emulator->run();
  } else {
    emulator->setRunAhead(true);
    emulator->run();
    auto state = emulator->serialize(0);
    if(settings.emulator.runAhead.frames >= 2) emulator->run();
    if(settings.emulator.runAhead.frames >= 3) emulator->run();
    if(settings.emulator.runAhead.frames >= 4) emulator->run();
    emulator->setRunAhead(false);
    emulator->run();
    state.setMode(serializer::Mode::Load);
    emulator->unserialize(state);
  }

  if(timing) {
    uint64 t1 = chrono::nanosecond();
    runNs += t1 - t0;
    lastEnd = t1;
    if(HdTrace::frame() % 60 == 0) {
      fprintf(stderr, "[time] frame=%u run=%.1fms/frame outside=%.1fms/frame\n",
        HdTrace::frame(), runNs / 1e6 / 60, loopNs / 1e6 / 60);
      runNs = loopNs = 0;
    }
  }

  if(emulatorSettings.autoSaveMemory.checked()) {
    auto currentTime = chrono::timestamp();
    if(currentTime - autoSaveTime >= settings.emulator.autoSaveMemory.interval) {
      autoSaveTime = currentTime;
      emulator->save();
    }
  }
}

auto Program::quit() -> void {
  //make closing the program feel more responsive
  presentation.setVisible(false);
  Application::processEvents();

  //in case the emulator was closed prior to initialization completing:
  settings.general.crashed = false;

  unload();
  settings.save();
  video.reset();
  audio.reset();
  input.reset();

  #if defined(PLATFORM_WINDOWS)
  //in rare cases, when Application::exit() calls exit(0), a crash will occur.
  //this seems to be due to the internal state of certain ruby drivers.
  auto processID = GetCurrentProcessId();
  auto handle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, true, processID);
  TerminateProcess(handle, 0);
  #endif

  Application::exit();
}
