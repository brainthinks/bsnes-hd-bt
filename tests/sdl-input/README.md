# SDL3 input regression tests

Run `make -C tests/sdl-input run` for SDL virtual gamepads. This compiles
and exercises the actual joypad driver without GTK or a ROM. It checks
repeated attach/removal, button input, release of held buttons, stable
binding identity, zero idle controller enumerations, and worker restart.

On Linux, `make -C tests/sdl-input run-udev` additionally exercises kernel
hotplug via `/dev/uinput`. The current user must have access to that device
and the generated `/dev/input/event*` nodes. It temporarily creates a test
gamepad and sends a South-button press; run with games closed. Existing
controllers are not disconnected. At least one of the four slots must be free.

The udev test matters: SDL virtual devices alone bypass Linux discovery and
cannot catch a missing event pump. In SDL 3.4.16, replacing `SDL_PumpEvents`
with `SDL_UpdateGamepads` reproduces the attach timeout. With the fix, six
kernel-device attachments across two worker lifetimes took 49–70 ms on the
development machine; virtual attachments took 6–9 ms. These timings exclude
a physical Bluetooth radio/pairing handshake.

Discovery is driven by SDL add/remove events. The worker still samples
buttons and services SDL every 8 ms, but no longer enumerates the controller
list every tick. Only a failed gamepad open schedules a one-second retry.
SDL events must continue to be initialized and pumped on the same input
worker; this Xlib backend must not initialize SDL video on the GTK thread.

## Pro 2 physical Bluetooth reconnect

The synthetic tests do not reproduce controller firmware feature-report stalls.
A physical Pro 2 (`2dc8:6006`, DirectInput mode) reconnect was traced on the
development machine with SDL 3.4.16. Before the device-specific fallback,
`HIDIOCGFEATURE(64)` blocked for **15.558 seconds** and the worker's event pump
spent **15.729 seconds** inside SDL. This matches the reported delay.

On Linux, the driver now defaults this VID/PID to SDL's evdev path by excluding
it from HIDAPI enumeration. Other controllers still use HIDAPI. In the verified
physical reconnect, the gamepad became available approximately 140 ms after its
first evdev open, and real face-button presses were recorded 91 ms later. There
were no HID feature-report requests. These are driver observations, not a
measurement of the radio connection handshake.

Related upstream report: https://github.com/libsdl-org/SDL/issues/14549

This bypasses SDL's enhanced 8BitDo protocol (including its sensor/rumble extras)
for this device ID; standard buttons, sticks, triggers, and d-pad use SDL's
existing Pro 2 mapping. The canonical bsnes binding identity is unchanged.
An explicitly configured `SDL_HIDAPI_IGNORE_DEVICES` overrides the default;
setting it to an empty string opts back into HIDAPI for future firmware testing.
