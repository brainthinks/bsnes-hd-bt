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

// Atlas rebuild keys on Mode 7 vram words 0..16383 (tilemap low + CHR high).
// Upper vram (16384..32767) is OBJ; hashing it rebuilt mips on explosions.
// Super Mario Kart writes CHR after the tilemap; ignoring high bytes left
// the atlas empty (backdrop showing through as a flat track).
constexpr std::uint32_t mode7VramWords = 16384;

inline auto mapContentHash(
  bool trueColor,
  bool directColor,
  const std::uint16_t* vram
) -> std::uint64_t {
  std::uint64_t hash = (trueColor ? 1u : 0u) | (directColor ? 2u : 0u);
  for(std::uint32_t n = 0; n < mode7VramWords; n++) hash = hash * 0x100000001b3ull ^ vram[n];
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

// 6 RGBA32F texels per scanline. of.w (index 15) marks a Mode 7 line.
constexpr int lineFloats = 24;
constexpr int lineValidIndex = 15;
constexpr float lineValidMin = 1.5f;

inline auto packLineValid(bool vflip) -> float {
  return (vflip ? 1.0f : 0.0f) + 2.0f;
}

inline auto lineIsValid(float ofw) -> bool {
  return ofw >= lineValidMin;
}

// Reconstruct short quantized fixed-colour ramps in the GPU's 240 scanline
// uniforms. This never filters texels or alters emulated IO. Call with raw
// colours each frame; the fragment shader interpolates the resulting knots.
inline auto reconstructColorRamps(float* lines, const std::uint8_t* windows) -> void {
  float raw[240][3];
  for(int y = 0; y < 240; y++) for(int c = 0; c < 3; c++) raw[y][c] = lines[y * lineFloats + 17 + c];
  auto compatible = [&](int a, int b) {
    const float* p = lines + a * lineFloats;
    const float* q = lines + b * lineFloats;
    if(!lineIsValid(p[15]) || !lineIsValid(q[15]) || p[16] != q[16]) return false;
    if((int(p[16]) & 9) != 1) return false;  // fixed-colour math only
    for(int c = 8; c < 16; c++) if(p[c] != q[c]) return false;
    for(int x = 0; x < 256; x++) if(windows[a * 256 + x] != windows[b * 256 + x]) return false;
    return true;
  };
  auto equal = [&](int a, int b) {
    return raw[a][0] == raw[b][0] && raw[a][1] == raw[b][1] && raw[a][2] == raw[b][2];
  };
  for(int first = 0; first < 240;) {
    int next = first + 1;
    while(next < 240 && compatible(first, next) && equal(first, next)) next++;
    int count = next - first;
    bool ramp = count > 1 && count <= 8 && next < 240 && compatible(first, next);
    if(ramp && first > 0 && compatible(first - 1, first)) {
      float direction = 0.0f;
      for(int c = 0; c < 3; c++) {
        float product = (raw[first][c] - raw[first - 1][c]) * (raw[next][c] - raw[first][c]);
        if(product < 0.0f) ramp = false;
        direction += product;
      }
      if(direction <= 0.0f) ramp = false;
    }
    if(ramp) for(int y = first + 1; y < next; y++) {
      float t = float(y - first) / float(count);
      for(int c = 0; c < 3; c++)
        lines[y * lineFloats + 17 + c] = raw[first][c] + (raw[next][c] - raw[first][c]) * t;
    }
    first = next;
  }
}

// writeVRAM/writeOAM flush with start > 1. Clearing all 240 lines there
// dropped Mode 7 matrices before present (SMK looked like Fast nearest).
inline auto gpuFlushClearsAll(std::uint32_t start) -> bool {
  return start <= 1;
}

}
