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

inline auto colorWindowBits(bool mathWin, bool aboveWin) -> std::uint8_t {
  return std::uint8_t((mathWin ? 1 : 0) | (aboveWin ? 2 : 0));
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
