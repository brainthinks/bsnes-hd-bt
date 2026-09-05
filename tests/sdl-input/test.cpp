#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <ruby/ruby.hpp>
using namespace nall;
using namespace ruby;

// Use the real driver without creating an X11 window.
namespace ruby {
auto Input::doChange(shared_pointer<HID::Device>, uint, uint, int16_t, int16_t) -> void {}
}
static std::atomic<unsigned> enumerations{0};
static auto countedGamepads(int* count) -> SDL_JoystickID* {
  enumerations++;
  return SDL_GetGamepads(count);
}
#define SDL_GetGamepads countedGamepads
#include <ruby/input/joypad/sdl.cpp>
#undef SDL_GetGamepads

static auto require(bool ok, const char* message) -> void {
  if(!ok) { fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError()); exit(1); }
}
template<typename F> static auto waitFor(F condition, int timeout = 1000) -> double {
  auto start = std::chrono::steady_clock::now();
  while(!condition()) {
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if(ms >= timeout) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

struct TestPad {
  bool kernel;
  int fd = -1;
  SDL_JoystickID id = 0;
  SDL_Joystick* joystick = nullptr;
  TestPad(bool kernel) : kernel(kernel) {
    if(kernel) {
      fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
      require(fd >= 0, "open /dev/uinput (run with device access)");
      require(ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0, "enable buttons");
      for(int button : {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR})
        require(ioctl(fd, UI_SET_KEYBIT, button) == 0, "define button");
      require(ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0, "enable axes");
      for(int axis : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
        require(ioctl(fd, UI_SET_ABSBIT, axis) == 0, "define axis");
        uinput_abs_setup setup{};
        setup.code = axis;
        setup.absinfo.minimum = axis >= ABS_HAT0X ? -1 : (axis == ABS_Z || axis == ABS_RZ ? 0 : -32768);
        setup.absinfo.maximum = axis >= ABS_HAT0X ? 1 : 32767;
        require(ioctl(fd, UI_ABS_SETUP, &setup) == 0, "configure axis");
      }
      uinput_setup setup{};
      snprintf(setup.name, sizeof(setup.name), "bsnes SDL hotplug regression test");
      setup.id = {BUS_USB, 0x1209, 0xb5e5, 1};
      require(ioctl(fd, UI_DEV_SETUP, &setup) == 0, "configure device");
      require(ioctl(fd, UI_DEV_CREATE) == 0, "create kernel gamepad");
    } else {
      SDL_VirtualJoystickDesc desc{};
      SDL_INIT_INTERFACE(&desc);
      desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
      desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
      desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
      desc.name = "bsnes SDL hotplug regression test";
      id = SDL_AttachVirtualJoystick(&desc);
      require(id != 0, "attach virtual gamepad");
      joystick = SDL_OpenJoystick(id);
      require(joystick, "open virtual joystick for test input");
    }
  }
  auto press() -> void {
    if(kernel) {
      input_event events[2]{};
      events[0].type = EV_KEY; events[0].code = BTN_SOUTH; events[0].value = 1;
      events[1].type = EV_SYN; events[1].code = SYN_REPORT;
      require(write(fd, events, sizeof(events)) == sizeof(events), "send button");
    } else require(SDL_SetJoystickVirtualButton(joystick, 0, true), "send virtual button");
  }
  ~TestPad() {
    if(kernel) { ioctl(fd, UI_DEV_DESTROY); close(fd); }
    else { SDL_CloseJoystick(joystick); SDL_DetachVirtualJoystick(id); }
  }
};

int main(int argc, char** argv) {
  const bool kernel = argc > 1 && !strcmp(argv[1], "--uinput");
  Input input;
  InputJoypadSDL driver(input);
  for(int restart = 0; restart < 2; restart++) {
    driver.initialize();
    require(waitFor([&] { return enumerations.load() > 0; }, 5000) >= 0, "worker initialized");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    unsigned slot = 0;
    {
      std::lock_guard<std::mutex> lock(driver.mutex);
      while(slot < driver.SlotCount && driver.published[slot].present) slot++;
    }
    require(slot < driver.SlotCount, "free test slot");
    auto snapshot = [&] {
      std::lock_guard<std::mutex> lock(driver.mutex);
      return driver.published[slot];
    };
    shared_pointer<HID::Joypad> hid;
    for(int cycle = 0; cycle < 3; cycle++) {
      {
        TestPad pad(kernel);
        double attached = waitFor([&] { return snapshot().present; });
        require(attached >= 0, "attach recognized within 1 second");
        vector<shared_pointer<HID::Device>> devices;
        driver.poll(devices);
        if(!hid) hid = driver.hids[slot];
        require(hid == driver.hids[slot], "reconnect retains binding identity");
        pad.press();
        require(waitFor([&] { return snapshot().button[0] != 0; }) >= 0, "button input after reconnect");
        driver.poll(devices);
        unsigned scans = enumerations.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        require(enumerations.load() == scans, "no enumeration while topology is unchanged");
        printf("%s restart %d cycle %d: attach %.1f ms; idle enumerations 0\n", kernel ? "udev" : "virtual", restart, cycle, attached);
      }
      double removed = waitFor([&] { return !snapshot().present; });
      require(removed >= 0, "removal recognized within 1 second");
      vector<shared_pointer<HID::Device>> devices;
      driver.poll(devices);
      require(hid->buttons().input(0).value() == 0, "disconnect releases held button");
      printf("  removal %.1f ms; held button released\n", removed);
    }
    driver.terminate();
    enumerations = 0;
  }
  puts("PASS: hotplug, input, release, stable bindings, idle discovery, and worker restart");
}
