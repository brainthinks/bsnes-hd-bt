compiler ?= g++
flags := -std=c++17 -O2 -Wall -Wextra -I../../bsnes/sfc/ppu-hd -I../../bsnes
libs :=
shader := ../../ruby/video/opengl/shaders.hpp

ifeq ($(shell test -f /usr/include/EGL/egl.h && echo yes),yes)
  flags += -DHD_PPU_GL
  libs += -lEGL -lGL
endif

.PHONY: all run clean

all: out/test

out/test: test.cpp ../../bsnes/sfc/ppu-hd/gpu-mode7.hpp ../../bsnes/emulator/hdtoolkit.hpp $(shader)
	mkdir -p out
	$(compiler) $(flags) -o $@ test.cpp $(libs)

run: out/test
	./out/test $(shader)

clean:
	rm -rf out
