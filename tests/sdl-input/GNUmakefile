CXX ?= c++
sdl_prefix := $(abspath ../../deps/sdl3-prefix)
pkg_config := PKG_CONFIG_PATH="$(sdl_prefix)/lib/pkgconfig:$(sdl_prefix)/lib64/pkgconfig:$$PKG_CONFIG_PATH" pkg-config
sdl_flags := $(shell $(pkg_config) --cflags sdl3)
sdl_libs := $(shell $(pkg_config) --libs sdl3)
flags := -std=c++17 -O2 -pthread -I../.. $(sdl_flags)

.PHONY: all run run-udev clean
all: out/test

out/test: test.cpp ../../ruby/input/joypad/sdl.cpp
	mkdir -p out
	$(CXX) $(flags) $< $(sdl_libs) -Wl,-rpath,$(sdl_prefix)/lib -Wl,-rpath,$(sdl_prefix)/lib64 -o $@

run: out/test
	./out/test

run-udev: out/test
	./out/test --uinput

clean:
	rm -rf out
