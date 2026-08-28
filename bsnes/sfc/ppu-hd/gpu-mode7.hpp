#pragma once

#include <cstdint>

// GPU Mode 7 uniform packing. Used by ppu-hd and tests/hd-ppu.
// The lines texture is GL_RGBA32F; decode() packs 0x00RRGGBB (R in bits 16-23).

namespace HDMode7 {

inline auto rgbFromPacked(std::uint32_t color00RRGGBB, float out[3]) -> void {
  out[0] = float(color00RRGGBB >> 16 & 255) / 255.0f;
  out[1] = float(color00RRGGBB >>  8 & 255) / 255.0f;
  out[2] = float(color00RRGGBB >>  0 & 255) / 255.0f;
}

// colorWindow byte: bit 0 = color-math window, bit 1 = above window,
// bit 2 = BG1 is visible (not clip-windowed) so GPU may replace COL.
constexpr std::uint8_t bg1VisibleBit = 4;

inline auto colorWindowBits(bool mathWin, bool aboveWin, bool bg1Visible = false) -> std::uint8_t {
  return std::uint8_t((mathWin ? 1 : 0) | (aboveWin ? 2 : 0) | (bg1Visible ? bg1VisibleBit : 0));
}

// Atlas rebuild is keyed only on the Mode 7 tilemap (low bytes of vram
// words 0..16383). Character bytes, OBJ CHR, and CGRAM all change during
// explosions; hashing any of them called glGenerateMipmap every debris
// frame and stalled a 60 FPS present. Brightness is a shader uniform.
constexpr std::uint32_t mode7VramWords = 16384;

inline auto mapContentHash(
  bool trueColor,
  bool directColor,
  const std::uint16_t* vram
) -> std::uint64_t {
  std::uint64_t hash = (trueColor ? 1u : 0u) | (directColor ? 2u : 0u);
  for(std::uint32_t n = 0; n < mode7VramWords; n++) hash = hash * 0x100000001b3ull ^ (vram[n] & 0xff);
  return hash;
}

inline auto mathFlags(bool enable, bool subtract, bool halve, bool blendMode) -> float {
  if(!enable) return 0.0f;
  float math = 1.0f;
  if(subtract) math += 2.0f;
  if(halve) math += 4.0f;
  if(blendMode) math += 8.0f;
  return math;
}

inline auto gpuSampleScale(std::uint32_t ssFactor, bool legacySupersample) -> std::uint32_t {
  std::uint32_t factor = ssFactor;
  if(factor < 2 && legacySupersample) factor = 2;
  if(factor < 1) factor = 1;
  if(factor > 16) factor = 16;
  return factor;
}

// When the GPU sampler is selected, the CPU Mode 7 path must not also
// supersample. If the GPU shader fails to compile, this yields 1x samples
// (visible banding) unless the driver reports the failure.
inline auto cpuSampleScale(bool gpuSupersample, std::uint32_t ssFactor, bool legacySupersample) -> std::uint32_t {
  if(gpuSupersample) return 1;
  return gpuSampleScale(ssFactor, legacySupersample);
}

}
