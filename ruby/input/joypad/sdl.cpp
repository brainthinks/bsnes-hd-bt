#pragma once

// SDL3 Gamepad driver. All SDL gamepad/joystick calls run on a worker
// thread: Bluetooth / hidapi open and close can block for seconds and
// would freeze GTK ("Hiro is not responding") on the UI thread.
//
// HID product 0x5343 is this virtual layout (not the old SDL2 raw
// joystick id 0x3), so leftover DualSense button indices cannot fire
// SNES face buttons or hotkeys. Slot 0 is device id 0x5343.

struct InputJoypadSDL {
  Input& input;
  InputJoypadSDL(Input& input) : input(input) {}

  static constexpr uint SlotCount = 4;
  static constexpr uint ButtonCount = 15;
  static constexpr uint16_t CanonicalProductID = 0x5343;  // 'SC'

  struct Snapshot {
    bool present = false;
    int16_t axis[4] = {};
    int16_t hat[2] = {};
    int16_t trigger[2] = {};
    uint8_t button[ButtonCount] = {};
  };

  struct WorkerPad {
    SDL_JoystickID instance = 0;
    SDL_Gamepad* handle = nullptr;
    uint slot = 0;
    int16_t stickHeldX = 0;
    int16_t stickHeldY = 0;
    bool ignoreStickX = false;
    bool ignoreStickY = false;
  };

  shared_pointer<HID::Joypad> hids[SlotCount];
  Snapshot published[SlotCount];
  std::mutex mutex;
  std::thread worker;
  std::atomic<bool> workerStop{false};
  std::atomic<uint32_t> rumbleBits{0};

  auto assign(shared_pointer<HID::Joypad> hid, uint groupID, uint inputID, int16_t value) -> void {
    auto& group = hid->group(groupID);
    if(group.input(inputID).value() == value) return;
    input.doChange(hid, groupID, inputID, group.input(inputID).value(), value);
    group.input(inputID).setValue(value);
  }

  auto makeHid(uint slot) -> shared_pointer<HID::Joypad> {
    shared_pointer<HID::Joypad> hid{new HID::Joypad};
    hid->setVendorID(HID::Joypad::GenericVendorID);
    hid->setProductID(CanonicalProductID);
    hid->setPathID(slot);
    hid->axes().append("LeftX");
    hid->axes().append("LeftY");
    hid->axes().append("RightX");
    hid->axes().append("RightY");
    hid->hats().append("PadX");
    hid->hats().append("PadY");
    hid->triggers().append("L2");
    hid->triggers().append("R2");
    static const char* buttons[] = {
      "South", "East", "West", "North",
      "Back", "Guide", "Start",
      "LeftStick", "RightStick",
      "LeftShoulder", "RightShoulder",
      "DpadUp", "DpadDown", "DpadLeft", "DpadRight"
    };
    for(auto n : range(ButtonCount)) hid->buttons().append(buttons[n]);
    hid->setRumble(true);
    return hid;
  }

  auto applySnapshot(shared_pointer<HID::Joypad> hid, const Snapshot& snap) -> void {
    if(!snap.present) {
      for(auto n : range(4)) assign(hid, HID::Joypad::GroupID::Axis, n, 0);
      assign(hid, HID::Joypad::GroupID::Hat, 0, 0);
      assign(hid, HID::Joypad::GroupID::Hat, 1, 0);
      assign(hid, HID::Joypad::GroupID::Trigger, 0, 0);
      assign(hid, HID::Joypad::GroupID::Trigger, 1, 0);
      for(auto n : range(ButtonCount)) assign(hid, HID::Joypad::GroupID::Button, n, 0);
      return;
    }
    for(auto n : range(4)) assign(hid, HID::Joypad::GroupID::Axis, n, snap.axis[n]);
    assign(hid, HID::Joypad::GroupID::Hat, 0, snap.hat[0]);
    assign(hid, HID::Joypad::GroupID::Hat, 1, snap.hat[1]);
    assign(hid, HID::Joypad::GroupID::Trigger, 0, snap.trigger[0]);
    assign(hid, HID::Joypad::GroupID::Trigger, 1, snap.trigger[1]);
    for(auto n : range(ButtonCount)) {
      assign(hid, HID::Joypad::GroupID::Button, n, snap.button[n] ? 1 : 0);
    }
  }

  // Same digital threshold bsnes uses for joypad axis Lo/Hi (16384).
  // Release is lower so a held stick does not chatter; after a d-pad
  // press, ignore the stick until it recenters so 8BitDo's fake stick
  // overshoot cannot fire the opposite direction.
  static constexpr int16_t StickPress = 16384;
  static constexpr int16_t StickRelease = 8192;

  auto stickHat(int16_t value, int16_t& held, bool& ignore) -> int16_t {
    if(ignore) {
      if(value > -StickRelease && value < StickRelease) ignore = false;
      else return 0;
    }
    if(held < 0) {
      if(value <= -StickRelease) return -32767;
      held = 0;
    } else if(held > 0) {
      if(value >= StickRelease) return +32767;
      held = 0;
    }
    if(value <= -StickPress) { held = -1; return -32767; }
    if(value >= StickPress) { held = +1; return +32767; }
    return 0;
  }

  auto readPad(WorkerPad& pad, Snapshot& snap) -> void {
    auto handle = pad.handle;
    snap.present = true;
    snap.axis[0] = (int16_t)SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_LEFTX);
    snap.axis[1] = (int16_t)SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_LEFTY);
    snap.axis[2] = (int16_t)SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_RIGHTX);
    snap.axis[3] = (int16_t)SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_RIGHTY);
    snap.trigger[0] = (int16_t)SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    snap.trigger[1] = (int16_t)SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    int16_t hatX = 0, hatY = 0;
    bool dpadX = false, dpadY = false;
    if(SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  { hatX = -32767; dpadX = true; }
    if(SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) { hatX = +32767; dpadX = true; }
    if(SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_DPAD_UP))    { hatY = -32767; dpadY = true; }
    if(SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  { hatY = +32767; dpadY = true; }
    if(dpadX) pad.ignoreStickX = true;
    if(dpadY) pad.ignoreStickY = true;
    if(!dpadX) hatX = stickHat(snap.axis[0], pad.stickHeldX, pad.ignoreStickX);
    if(!dpadY) hatY = stickHat(snap.axis[1], pad.stickHeldY, pad.ignoreStickY);
    snap.hat[0] = hatX;
    snap.hat[1] = hatY;
    for(auto n : range(ButtonCount)) {
      snap.button[n] = SDL_GetGamepadButton(handle, (SDL_GamepadButton)n) ? 1 : 0;
    }
  }

  auto workerLoop() -> void {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "0");
    #if defined(__linux__)
    // Pro 2 in DirectInput mode: SDL's enhanced-mode feature report can block
    // HIDIOCGFEATURE for 15 seconds on Bluetooth reconnect (SDL issue #14549).
    // Let SDL's evdev driver handle this VID/PID without the feature probe.
    // Other devices retain HIDAPI; an explicit SDL hint can override this default.
    SDL_SetHintWithPriority(SDL_HINT_HIDAPI_IGNORE_DEVICES,
      "0x2dc8/0x6006", SDL_HINT_DEFAULT);
    #endif
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Gamepad initialization failed: %s", SDL_GetError());
      return;
    }

    // This Xlib backend uses SDL only for input. Initializing SDL events here
    // makes this worker SDL's event thread (SDL_IsMainThread), not GTK's thread.
    // Keep the pump here too: Linux udev discovery runs in event maintenance,
    // which SDL_UpdateGamepads alone does not service.
    SDL_assert(SDL_IsMainThread());

    WorkerPad pads[SlotCount] = {};

    bool devicesChanged = true;  //enumerate once at startup
    Uint64 retryAt = 0;
    while(!workerStop.load()) {
      SDL_PumpEvents();  //also updates gamepad state
      SDL_Event events[32];
      int eventCount;
      // Drain input events even though values come from snapshots; otherwise
      // motion events eventually fill the queue and hide hotplug notifications.
      while((eventCount = SDL_PeepEvents(events, 32, SDL_GETEVENT,
        SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED)) > 0) {
        for(int n = 0; n < eventCount; n++) {
          if(events[n].type == SDL_EVENT_GAMEPAD_ADDED
          || events[n].type == SDL_EVENT_GAMEPAD_REMOVED) devicesChanged = true;
        }
      }

      if(devicesChanged || (retryAt && SDL_GetTicks() >= retryAt)) {
        devicesChanged = false;
        retryAt = 0;
        int count = 0;
        auto ids = SDL_GetGamepads(&count);
        vector<SDL_JoystickID> live;
        if(ids) {
          for(int n = 0; n < count; n++) live.append(ids[n]);
          SDL_free(ids);
        }

        for(auto n : range(SlotCount)) {
          if(!pads[n].handle) continue;
          bool found = false;
          for(auto id : live) {
            if(id == pads[n].instance) { found = true; break; }
          }
          if(!found) {
            SDL_CloseGamepad(pads[n].handle);
            pads[n] = {};
          }
        }

        for(auto id : live) {
          bool owned = false;
          for(auto n : range(SlotCount)) {
            if(pads[n].instance == id) { owned = true; break; }
          }
          if(owned) continue;
          maybe<uint> slot;
          for(auto n : range(SlotCount)) {
            if(!pads[n].handle) { slot = n; break; }
          }
          if(!slot) continue;
          auto handle = SDL_OpenGamepad(id);
          if(!handle) {
            // Retry a transient open failure without reopening every 8 ms.
            retryAt = SDL_GetTicks() + 1000;
            continue;
          }
          pads[slot()] = {id, handle, slot()};
        }
      }

      auto rumble = rumbleBits.load();
      Snapshot local[SlotCount] = {};
      for(auto n : range(SlotCount)) {
        if(!pads[n].handle) continue;
        readPad(pads[n], local[n]);
        bool enable = rumble & (1u << n);
        uint16_t strength = enable ? 0xffff : 0;
        SDL_RumbleGamepad(pads[n].handle, strength, strength, enable ? 120 : 0);
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto n : range(SlotCount)) published[n] = local[n];
      }
      SDL_Delay(8);
    }

    for(auto n : range(SlotCount)) {
      if(pads[n].handle) SDL_CloseGamepad(pads[n].handle);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  }

  auto poll(vector<shared_pointer<HID::Device>>& devices) -> void {
    Snapshot local[SlotCount];
    {
      std::lock_guard<std::mutex> lock(mutex);
      for(auto n : range(SlotCount)) local[n] = published[n];
    }
    for(auto n : range(SlotCount)) {
      if(!local[n].present && !hids[n]) continue;
      if(!hids[n]) hids[n] = makeHid(n);
      applySnapshot(hids[n], local[n]);
      devices.append(hids[n]);
    }
  }

  auto rumble(uint64_t id, bool enable) -> bool {
    for(auto n : range(SlotCount)) {
      if(!hids[n] || hids[n]->id() != id) continue;
      uint32_t bit = 1u << n;
      if(enable) rumbleBits.fetch_or(bit);
      else rumbleBits.fetch_and(~bit);
      return true;
    }
    return false;
  }

  auto initialize() -> bool {
    workerStop = false;
    rumbleBits = 0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      for(auto n : range(SlotCount)) published[n] = {};
    }
    worker = std::thread([this] { workerLoop(); });
    return true;
  }

  auto terminate() -> void {
    workerStop = true;
    if(worker.joinable()) worker.join();
    rumbleBits = 0;
    for(auto n : range(SlotCount)) hids[n] = {};
    std::lock_guard<std::mutex> lock(mutex);
    for(auto n : range(SlotCount)) published[n] = {};
  }
};
