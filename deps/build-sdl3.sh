#!/bin/sh
# SDL3 is not in Linux Mint 22 / Ubuntu 24.04 repos. Build a local prefix.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
deps="$root/deps"
tag="${1:-release-3.4.16}"
mkdir -p "$deps"
cd "$deps"
if [ ! -d sdl3-src/.git ]; then
  git clone --depth 1 --branch "$tag" https://github.com/libsdl-org/SDL.git sdl3-src
fi
cmake -S sdl3-src -B sdl3-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$deps/sdl3-prefix" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF
cmake --build sdl3-build -j"$(nproc)"
cmake --install sdl3-build
echo "SDL3 installed to $deps/sdl3-prefix"
PKG_CONFIG_PATH="$deps/sdl3-prefix/lib/pkgconfig:$deps/sdl3-prefix/lib64/pkgconfig" pkg-config --modversion sdl3
