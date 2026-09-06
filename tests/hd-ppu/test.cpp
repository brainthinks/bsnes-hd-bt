// Sanity tests for HD GPU Mode 7 packing and the Mode 7 fragment shader.
// Run: make -C tests/hd-ppu run
//
// These exist because GPU SS can fail silently (CPU sampScale stays 1):
// a GLSL reserved-word compile miss, unused `ss` optimized out, a vblank
// flush wiping Mode 7 lines, palette-alpha discard, and window math
// zeroing the SMK floor. The GL render path draws a checker and checks
// that 1× and 8× SS differ and that luma 0 does not black the image.

#include "gpu-mode7.hpp"
#include "emulator/hdtoolkit.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

#define CHECK(cond) do { \
  if(cond) { passed++; } \
  else { failed++; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while(0)

static auto near(float a, float b) -> bool {
  return std::fabs(a - b) < 1.0e-5f;
}

static auto readFile(const std::string& path) -> std::string {
  std::ifstream in(path, std::ios::binary);
  if(!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static auto extractRString(const std::string& src, const std::string& name) -> std::string {
  const std::string key = name + " = R\"(";
  auto a = src.find(key);
  if(a == std::string::npos) return {};
  a += key.size();
  auto b = src.find(")\";", a);
  if(b == std::string::npos) return {};
  return src.substr(a, b - a);
}

static auto stripComments(std::string s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for(size_t i = 0; i < s.size();) {
    if(i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
      while(i < s.size() && s[i] != '\n') i++;
      continue;
    }
    if(i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
      i += 2;
      while(i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
      i += 2;
      continue;
    }
    out.push_back(s[i++]);
  }
  return out;
}

static auto hasIdent(const std::string& src, const char* ident) -> bool {
  const size_t n = std::strlen(ident);
  for(size_t i = 0; i + n <= src.size(); i++) {
    if(std::memcmp(src.data() + i, ident, n) != 0) continue;
    char before = i == 0 ? ' ' : src[i - 1];
    char after = i + n == src.size() ? ' ' : src[i + n];
    auto isIdent = [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    if(!isIdent(before) && !isIdent(after)) return true;
  }
  return false;
}

#ifdef HD_PPU_GL
#include <EGL/egl.h>
#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#endif
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

static auto makeGL32() -> bool {
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(dpy == EGL_NO_DISPLAY) return false;
  if(!eglInitialize(dpy, nullptr, nullptr)) return false;
  if(!eglBindAPI(EGL_OPENGL_API)) return false;
  EGLint cfgAttr[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_NONE
  };
  EGLConfig cfg{};
  EGLint n = 0;
  if(!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) return false;
  EGLint ctxAttr[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 2,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_NONE
  };
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  if(ctx == EGL_NO_CONTEXT) return false;
  EGLint pbuf[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbuf);
  if(surf == EGL_NO_SURFACE) return false;
  return eglMakeCurrent(dpy, surf, surf, ctx) == EGL_TRUE;
}

static bool gHaveGL = false, gTriedGL = false;
static auto ensureGL() -> bool {
  if(!gTriedGL) {
    gTriedGL = true;
    gHaveGL = makeGL32();
    if(!gHaveGL) std::printf("skip: no EGL OpenGL 3.2 context (source checks still ran)\n");
  }
  return gHaveGL;
}

static auto compileShader(GLenum type, const std::string& source, std::string& log) -> GLuint {
  GLuint shader = glCreateShader(type);
  const char* p = source.c_str();
  glShaderSource(shader, 1, &p, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  if(length > 1) {
    log.append(source == "" ? "" : "");
    std::string s(size_t(length), '\0');
    glGetShaderInfoLog(shader, length, &length, s.data());
    s.resize(size_t(length));
    log += s;
  }
  if(ok != GL_TRUE) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static auto compileFragment(const std::string& source, std::string& log) -> bool {
  GLuint shader = compileShader(GL_FRAGMENT_SHADER, source, log);
  if(shader) glDeleteShader(shader);
  return shader != 0;
}

static auto linkMode7(const std::string& vert, const std::string& frag, std::string& log) -> GLuint {
  GLuint vs = compileShader(GL_VERTEX_SHADER, vert, log);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag, log);
  if(!vs || !fs) {
    if(vs) glDeleteShader(vs);
    if(fs) glDeleteShader(fs);
    return 0;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindFragDataLocation(prog, 0, "fragColor");
  glLinkProgram(prog);
  GLint ok = GL_FALSE;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  GLint length = 0;
  glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
  if(length > 1) {
    std::string s(size_t(length), '\0');
    glGetProgramInfoLog(prog, length, &length, s.data());
    s.resize(size_t(length));
    log += s;
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  if(ok != GL_TRUE) {
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}
#endif

static auto testPacking() -> void {
  float rgb[3];

  HDMode7::rgbFromPacked(0x00FF0000u, rgb);
  CHECK(near(rgb[0], 1.0f) && near(rgb[1], 0.0f) && near(rgb[2], 0.0f));

  HDMode7::rgbFromPacked(0x000000FFu, rgb);
  CHECK(near(rgb[0], 0.0f) && near(rgb[1], 0.0f) && near(rgb[2], 1.0f));

  HDMode7::rgbFromPacked(0x0000FF00u, rgb);
  CHECK(near(rgb[0], 0.0f) && near(rgb[1], 1.0f) && near(rgb[2], 0.0f));

  // F-Zero fog subtract yellow ~ (0.478, 0.478, 0) must not swap to cyan.
  HDMode7::rgbFromPacked(0x007A7A00u, rgb);
  CHECK(near(rgb[0], rgb[1]) && rgb[2] < 0.01f && rgb[0] > 0.4f);

  CHECK(HDMode7::mathFlags(false, true, true, true) == 0.0f);
  CHECK(HDMode7::mathFlags(true, false, false, false) == 1.0f);
  CHECK(HDMode7::mathFlags(true, true, false, false) == 3.0f);   // F-Zero race
  CHECK(HDMode7::mathFlags(true, false, true, false) == 5.0f);
  CHECK(HDMode7::mathFlags(true, false, false, true) == 9.0f);

  CHECK(HDMode7::colorWindowBits(true, true) == 3);
  CHECK(HDMode7::colorWindowBits(true, false) == 1);
  CHECK(HDMode7::colorWindowBits(false, true) == 2);
  CHECK(HDMode7::colorWindowBits(false, false) == 0);
  CHECK(HDMode7::colorWindowBits(false, false, true) == HDMode7::bg1VisibleBit);
  CHECK(HDMode7::colorWindowBits(true, true, true) == (3 | HDMode7::bg1VisibleBit));
  CHECK(HDMode7::bg1VisibleBit == 4);

  std::uint16_t vram[32768] = {};
  auto h0 = HDMode7::mapContentHash(true, false, vram);
  vram[16384] = 0x1234;  // OBJ CHR in upper VRAM
  CHECK(HDMode7::mapContentHash(true, false, vram) == h0);
  vram[0] = 0xff00;      // Mode 7 CHR (SMK loads this after the tilemap)
  CHECK(HDMode7::mapContentHash(true, false, vram) != h0);
  auto hChr = HDMode7::mapContentHash(true, false, vram);
  vram[0] = 0x0001;      // tilemap low byte
  CHECK(HDMode7::mapContentHash(true, false, vram) != hChr);
  CHECK(HDMode7::mode7VramWords == 16384);

  for(int v = 0; v <= 3; v++) {
    float r = float(v) / 255.0f;
    int back = int(r * 255.0f + 0.5f);
    CHECK(back == v);
  }

  CHECK(HDMode7::cpuSampleScale(true, 16, true) == 1);
  CHECK(HDMode7::cpuSampleScale(false, 16, true) == 16);
  CHECK(HDMode7::cpuSampleScale(false, 1, true) == 2);
  CHECK(HDMode7::cpuSampleScale(false, 0, false) == 1);
  CHECK(HDMode7::gpuSampleScale(16, false) == 16);
  CHECK(HDMode7::gpuSampleScale(8, true) == 8);
  CHECK(HDMode7::gpuSampleScale(1, false) == 1);

  CHECK(HDMode7::lineFloats == 24);
  CHECK(HDMode7::lineValidIndex == 15);
  CHECK(HDMode7::gpuFlushClearsAll(0) == true);
  CHECK(HDMode7::gpuFlushClearsAll(1) == true);
  CHECK(HDMode7::gpuFlushClearsAll(2) == false);
  CHECK(HDMode7::gpuFlushClearsAll(200) == false);
  CHECK(HDMode7::lineIsValid(HDMode7::packLineValid(false)));
  CHECK(HDMode7::lineIsValid(HDMode7::packLineValid(true)));
  CHECK(!HDMode7::lineIsValid(0.0f));
  CHECK(!HDMode7::lineIsValid(1.0f));
  CHECK(HDMode7::packLineValid(false) < 2.5f);
  CHECK(HDMode7::packLineValid(true) > 2.5f);

  CHECK(HdToolkit::determineWsExt(0, false, false) == 0);
  CHECK(HdToolkit::determineWsExt(64, false, false) == 64);
  CHECK(HdToolkit::determineWsExt(1609, false, false) == 64);
  CHECK(HdToolkit::determineWsExt(1609, false, true) == 64);
  CHECK(HdToolkit::determineWsExt(403, false, false) == 16);

  auto hud = HdToolkit::decideWsBg(16, 20, 0, 0, 0, 40, false);
  CHECK(hud.extra == 0);
  CHECK(!hud.disable);
  auto skyOn = HdToolkit::decideWsBg(1, 20, 0, 0, 0, 40, false);
  CHECK(skyOn.extra == 40);
  auto off = HdToolkit::decideWsBg(0, 20, 0, 0, 0, 40, false);
  CHECK(off.extra == 0);
  auto scrolled = HdToolkit::decideWsBg(16, 20, 0, 8, 0, 40, false);
  CHECK(scrolled.extra == 40);
  auto below80 = HdToolkit::decideWsBg(5, 100, 0, 0, 0, 40, false);
  CHECK(below80.extra == 40);
  auto above80 = HdToolkit::decideWsBg(5, 20, 0, 0, 0, 40, false);
  CHECK(above80.extra == 0);
  auto crop = HdToolkit::decideWsBg(12, 20, 0, 0, 0, 40, false);
  CHECK(crop.extra == -8);
  auto disabled = HdToolkit::decideWsBg(14, 20, 0, 0, 0, 40, false);
  CHECK(disabled.disable);
  auto overrideOff = HdToolkit::decideWsBg(1, 20, 0, 0, 0, 40, true);
  CHECK(overrideOff.extra == 0);

  // A panorama stored as three 256-pixel windows of seven tile rows each. The
  // second tilemap half holds, at the same rows, the window 256 pixels on, so
  // the loop closes after 768 pixels. Distinct column data keeps matches from
  // being an artefact of an empty sky.
  unsigned short panorama[32768] = {};
  constexpr unsigned map = 0x7800;
  auto fillHalf = [&](unsigned first, unsigned half, unsigned scene) {
    for(unsigned y = 0; y < 7; y++) for(unsigned x = 0; x < 32; x++) {
      panorama[map + half * 1024 + (first + y) * 32 + x] = 1 + scene * 256 + y * 32 + x;
    }
  };
  auto build = [&](unsigned windows, unsigned base) {
    for(unsigned n = 0; n < 32768; n++) panorama[n] = 0;
    for(unsigned band = 0; band < windows; band++) {
      fillHalf(base + band * 7, 0, band);
      fillHalf(base + band * 7, 1, (band + 1) % windows);
    }
  };

  build(3, 11);
  auto grid = HdToolkit::panoramaGrid(panorama, map, 32, 0, 11, 17);
  CHECK(grid.count == 3);
  CHECK(grid.base == 11);
  CHECK(grid.height == 7);
  CHECK(grid.lastSpan == 256);
  CHECK(grid.length() == 768);

  // A column one pixel left of the picture belongs to the window before this
  // one, which for the first window is the last: two windows further down.
  int h = 0, v = 0;
  CHECK(HdToolkit::panoramaAdjust(grid, 11, -1, h, v));
  CHECK(h == 256 && v == 2 * 7 * 8);
  CHECK(HdToolkit::panoramaAdjust(grid, 18, -1, h, v));
  CHECK(h == 256 && v == -7 * 8);
  // 512 pixels on is two windows on, wrapping back to the first.
  CHECK(HdToolkit::panoramaAdjust(grid, 25, 512, h, v));
  CHECK(h == -512 && v == -7 * 8);
  // Inside 0..255 the answer is the window itself: no adjustment at all.
  CHECK(HdToolkit::panoramaAdjust(grid, 11, 100, h, v));
  CHECK(h == 0 && v == 0);

  // A loop that is not a whole number of windows: F-Zero's nearest layer closes
  // after three and a half, so the last window's second half repeats the first
  // window's beginning and the step back from the first window is 128 pixels.
  // Build it from the panorama itself so both halves stay consistent.
  {
    for(unsigned n = 0; n < 32768; n++) panorama[n] = 0;
    constexpr unsigned tiles = 112;  //896 pixels
    for(unsigned window = 0; window < 4; window++) {
      for(unsigned y = 0; y < 7; y++) for(unsigned x = 0; x < 32; x++) {
        unsigned row = 4 + window * 7 + y;
        panorama[map + row * 32 + x] = 1 + y * 256 + (window * 32 + x) % tiles;
        panorama[map + 1024 + row * 32 + x] = 1 + y * 256 + (window * 32 + 32 + x) % tiles;
      }
    }
  }
  auto shortLoop = HdToolkit::panoramaGrid(panorama, map, 32, 0, 4, 10);
  CHECK(shortLoop.base == 4);
  CHECK(shortLoop.count == 4);
  CHECK(shortLoop.lastSpan == 128);
  CHECK(shortLoop.length() == 896);
  CHECK(HdToolkit::panoramaAdjust(shortLoop, 4, -1, h, v));
  CHECK(h == 128 && v == 3 * 7 * 8);
  CHECK(HdToolkit::panoramaAdjust(shortLoop, 11, -1, h, v));
  CHECK(h == 256 && v == -7 * 8);

  // Super Mario Kart's shape: four four-row windows on a 64x64 tilemap, in
  // mode 0 on BG3. Rows 32..63 of such a map live at a separate word offset.
  {
    for(unsigned n = 0; n < 32768; n++) panorama[n] = 0;
    constexpr unsigned tiles = 128;  //1024 pixels
    auto put = [&](unsigned half, unsigned row, unsigned col, unsigned short v) {
      unsigned off = (row & 31) * 32 + (col & 31);
      if(half) off += 1024;
      if(row & 32) off += 2048;
      panorama[(map + off) & 0x7fff] = v;
    };
    for(unsigned window = 0; window < 4; window++) {
      for(unsigned y = 0; y < 4; y++) for(unsigned x = 0; x < 32; x++) {
        unsigned row = 40 + window * 4 + y;   //rows 40..55, past the 32-row split
        put(0, row, x, 1 + y * 256 + (window * 32 + x) % tiles);
        put(1, row, x, 1 + y * 256 + (window * 32 + 32 + x) % tiles);
      }
    }
  }
  auto wide = HdToolkit::panoramaGrid(panorama, map, 64, 2048, 40, 44);
  CHECK(wide.base == 40);
  CHECK(wide.height == 4);
  CHECK(wide.count == 4);
  CHECK(wide.lastSpan == 256);
  CHECK(wide.length() == 1024);
  CHECK(HdToolkit::panoramaAdjust(wide, 40, -1, h, v));
  CHECK(h == 256 && v == 3 * 4 * 8);
  CHECK(HdToolkit::panoramaAdjust(wide, 44, -1, h, v));
  CHECK(h == 256 && v == -4 * 8);
  // A 64-row map is not searched as if it were 32 rows.
  CHECK(HdToolkit::panoramaGrid(panorama, map, 32, 0, 8, 12).count == 0);

  // An ordinary background is not a panorama.
  unsigned short plain[32768] = {};
  for(unsigned n = 0; n < 2048; n++) plain[map + n] = 1 + n;
  CHECK(HdToolkit::panoramaGrid(plain, map, 32, 0, 11, 17).count == 0);
  CHECK(!HdToolkit::panoramaAdjust(HdToolkit::panoramaGrid(plain, map, 32, 0, 11, 17), 11, -1, h, v));
  // A band straddling the tilemap's vertical wrap is rejected.
  CHECK(HdToolkit::panoramaGrid(panorama, map, 32, 0, 30, 3).count == 0);

}

static auto testShaderSource(const std::string& path) -> void {
  auto file = readFile(path);
  CHECK(!file.empty());
  auto frag = extractRString(file, "OpenGLMode7FragmentShader");
  CHECK(!frag.empty());
  CHECK(frag.find("#version 150") != std::string::npos);
  auto mapVert = extractRString(file, "OpenGLMode7MapVertexShader");
  auto mapFrag = extractRString(file, "OpenGLMode7MapFragmentShader");
  CHECK(!mapVert.empty());
  CHECK(!mapFrag.empty());
  CHECK(mapFrag.find("mode7Vram") != std::string::npos);
  CHECK(mapFrag.find("mode7Palette") != std::string::npos);
  CHECK(mapFrag.find("gl_FragCoord") != std::string::npos);

  auto body = stripComments(frag);
  CHECK(!hasIdent(body, "packed"));  // reserved in GLSL; broke GPU SS
  CHECK(hasIdent(body, "applyMath") || frag.find("flags & 1") != std::string::npos);
  CHECK(frag.find("math.yzw") != std::string::npos);
  CHECK(frag.find("mode7Window") != std::string::npos);
  CHECK(frag.find("mode7Luma") != std::string::npos);
  CHECK(frag.find("decodeVram") != std::string::npos);
  CHECK(frag.find("return decodeVram") != std::string::npos);
  CHECK(frag.find("vec4(c.rgb, 1.0)") != std::string::npos);
  CHECK(frag.find("for(int j = 0; j < n; j++)") != std::string::npos);
  CHECK(frag.find("integrateM7") != std::string::npos);
  CHECK(frag.find("exp(-2.0") == std::string::npos);
  CHECK(frag.find("1.0 + 2.0 * taper") == std::string::npos);
  CHECK(frag.find("j < 16") == std::string::npos);
  CHECK(frag.find("int n = ss") != std::string::npos);
  CHECK(frag.find("snesW") != std::string::npos);
  CHECK(frag.find("texCoord.x * snesW - ws") != std::string::npos);
  CHECK(frag.find("snes.x < 0.0 || snes.x >= 256.0") != std::string::npos);
  CHECK(frag.find("luma < 1.0 / 15.0") != std::string::npos);
  // SMK floor went black when a window bit zeroed the texel.
  CHECK(frag.find("if(!aboveWin) rgb") == std::string::npos);
  CHECK(frag.find("rgb = vec3(0.0)") == std::string::npos);

#ifdef HD_PPU_GL
  if(ensureGL()) {
    std::string log;
    bool ok = compileFragment(frag, log);
    if(!ok) std::fprintf(stderr, "shader log:\n%s\n", log.c_str());
    CHECK(ok);
    log.clear();
    bool mapOk = compileFragment(mapFrag, log);
    if(!mapOk) std::fprintf(stderr, "map shader log:\n%s\n", log.c_str());
    CHECK(mapOk);
    log.clear();
    auto vert = extractRString(file, "OpenGLOutputVertexShader");
    CHECK(!vert.empty());
    GLuint prog = linkMode7(vert, frag, log);
    if(!prog) std::fprintf(stderr, "link log:\n%s\n", log.c_str());
    CHECK(prog != 0);
    if(prog) {
      glUseProgram(prog);
      auto loc = [&](const char* n) {
        GLint l = glGetUniformLocation(prog, n);
        std::printf("uniform %s loc=%d\n", n, l);
        return l;
      };
      CHECK(loc("ss") >= 0);
      CHECK(loc("mode7Vram") >= 0);
      CHECK(loc("mode7Palette") >= 0);
      CHECK(loc("mode7Lines") >= 0);
      CHECK(loc("mode7Luma") >= 0);
      CHECK(loc("source[0]") >= 0 || loc("source") >= 0);
      glDeleteProgram(prog);
    }
  }
#else
  std::printf("skip: HD_PPU_GL not built (source checks still ran)\n");
#endif
}

static auto testColorRamps() -> void {
  std::vector<float> lines(240 * HDMode7::lineFloats, 0.0f);
  std::vector<std::uint8_t> windows(240 * 256, 3);
  auto setup = [&] {
    std::fill(lines.begin(), lines.end(), 0.0f);
    std::fill(windows.begin(), windows.end(), 3);
    const float colors[] = {1.0f, 1.0f, .8f, .8f, .6f, .6f, .4f, .4f, .9f, .9f};
    for(int y = 0; y < 10; y++) {
      float* p = lines.data() + y * HDMode7::lineFloats;
      p[9] = 10;
      p[15] = HDMode7::packLineValid(false);
      p[16] = 3;
      p[17] = p[18] = colors[y];
    }
  };
  auto red = [&](int y) { return lines[y * HDMode7::lineFloats + 17]; };
  setup();
  HDMode7::reconstructColorRamps(lines.data(), windows.data());
  CHECK(std::abs(red(1) - .9f) < 1e-6f);
  CHECK(std::abs(red(3) - .7f) < 1e-6f);
  CHECK(std::abs(red(5) - .5f) < 1e-6f);
  CHECK(red(7) == .4f);  // colour direction reversal is not a fog ramp
  CHECK(red(9) == .9f);  // do not blend into an invalid/non-Mode-7 line
  setup();
  windows[3 * 256 + 42] = 0;
  HDMode7::reconstructColorRamps(lines.data(), windows.data());
  CHECK(red(3) == .8f);
  setup();
  lines[4 * HDMode7::lineFloats + 16] = 1;  // subtract -> add
  HDMode7::reconstructColorRamps(lines.data(), windows.data());
  CHECK(red(3) == .8f);
  setup();
  lines[4 * HDMode7::lineFloats + 9] = 11;  // new projection group
  HDMode7::reconstructColorRamps(lines.data(), windows.data());
  CHECK(red(3) == .8f);
}

#ifdef HD_PPU_GL
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif

static const char* kFullscreenVert = R"(
#version 150
out Vertex { vec2 texCoord; } vertexOut;
void main() {
  float x = (gl_VertexID == 1 || gl_VertexID == 3) ? 1.0 : 0.0;
  float y = (gl_VertexID == 2 || gl_VertexID == 3) ? 1.0 : 0.0;
  gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
  vertexOut.texCoord = vec2(x, y);
}
)";

struct Mode7Draw {
  GLuint prog = 0;
  GLuint fbo = 0, color = 0, vao = 0;
  GLuint src = 0, lines = 0, win = 0, vram = 0, pal = 0, tile0 = 0;
  int w = 64, h = 48;
};

static auto makeTex2D(GLenum internal, int w, int h, GLenum fmt, GLenum type, const void* data) -> GLuint {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, type, data);
  return t;
}

static auto bindUnit(GLuint prog, const char* name, int unit, GLuint tex) -> void {
  GLint loc = glGetUniformLocation(prog, name);
  if(loc < 0) return;
  glUniform1i(loc, unit);
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, tex);
}

static auto drawMode7(Mode7Draw& d, int ss, float luma) -> std::vector<std::uint8_t> {
  glBindFramebuffer(GL_FRAMEBUFFER, d.fbo);
  glViewport(0, 0, d.w, d.h);
  glDisable(GL_BLEND);
  glUseProgram(d.prog);
  auto set1i = [&](const char* n, int v) {
    GLint loc = glGetUniformLocation(d.prog, n);
    if(loc >= 0) glUniform1i(loc, v);
  };
  auto set1f = [&](const char* n, float v) {
    GLint loc = glGetUniformLocation(d.prog, n);
    if(loc >= 0) glUniform1f(loc, v);
  };
  auto set4f = [&](const char* n, float a, float b, float c, float e) {
    GLint loc = glGetUniformLocation(d.prog, n);
    if(loc >= 0) glUniform4f(loc, a, b, c, e);
  };
  set1i("ss", ss);
  set1f("lineOrigin", 0.0f);
  set1f("mode7Luma", luma);
  set4f("sourceSize", 256.0f, 224.0f, 1.0f / 256.0f, 1.0f / 224.0f);
  set4f("targetSize", float(d.w), float(d.h), 1.0f / float(d.w), 1.0f / float(d.h));
  bindUnit(d.prog, "source[0]", 0, d.src);
  bindUnit(d.prog, "source", 0, d.src);
  bindUnit(d.prog, "mode7Lines", 2, d.lines);
  bindUnit(d.prog, "mode7Tile0", 3, d.tile0);
  bindUnit(d.prog, "mode7Window", 4, d.win);
  bindUnit(d.prog, "mode7Vram", 5, d.vram);
  bindUnit(d.prog, "mode7Palette", 6, d.pal);
  glBindVertexArray(d.vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  std::vector<std::uint8_t> px(size_t(d.w) * size_t(d.h) * 4);
  glReadPixels(0, 0, d.w, d.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  return px;
}

static auto uniqueRGB(const std::vector<std::uint8_t>& px) -> int {
  std::set<unsigned> s;
  for(size_t i = 0; i + 3 < px.size(); i += 4) {
    s.insert(unsigned(px[i]) << 16 | unsigned(px[i + 1]) << 8 | unsigned(px[i + 2]));
  }
  return int(s.size());
}

static auto countNonBlack(const std::vector<std::uint8_t>& px) -> int {
  int n = 0;
  for(size_t i = 0; i + 3 < px.size(); i += 4) {
    if(px[i] | px[i + 1] | px[i + 2]) n++;
  }
  return n;
}

static auto firstRGB(const std::vector<std::uint8_t>& px) -> void {
  if(px.size() >= 3) std::printf(" first=(%u,%u,%u)", px[0], px[1], px[2]);
}

static auto testMode7Render(const std::string& frag) -> void {
  if(!ensureGL()) return;

  std::string log;
  GLuint prog = linkMode7(kFullscreenVert, frag, log);
  if(!prog) {
    std::fprintf(stderr, "render link log:\n%s\n", log.c_str());
    CHECK(prog != 0);
    return;
  }

  Mode7Draw d;
  d.prog = prog;
  d.w = 64;
  d.h = 48;
  glGenVertexArrays(1, &d.vao);
  glBindVertexArray(d.vao);

  std::vector<std::uint32_t> cpu(256 * 224, 0);
  d.src = makeTex2D(GL_RGBA8, 256, 224, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, cpu.data());

  std::vector<float> lines(240 * HDMode7::lineFloats, 0.0f);
  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[0] = 256.0f; p[3] = 256.0f;
    p[4] = 256.0f; p[7] = 256.0f;
    p[8] = 0.0f; p[9] = 239.0f;
    p[15] = HDMode7::packLineValid(false);
  }
  d.lines = makeTex2D(GL_RGBA32F, 6, 240, GL_RGBA, GL_FLOAT, lines.data());

  std::vector<std::uint8_t> win(240 * 256, 0);
  d.win = makeTex2D(GL_R8, 256, 240, GL_RED, GL_UNSIGNED_BYTE, win.data());

  std::vector<std::uint8_t> vram(128 * 128 * 4, 0);
  for(int n = 0; n < 128 * 128; n++) vram[n * 4 + 3] = 255;
  for(int py = 0; py < 8; py++) {
    for(int px = 0; px < 8; px++) {
      int word = (py << 3) + px;
      vram[word * 4 + 1] = std::uint8_t(((px + py) & 1) ? 2 : 1);
    }
  }
  d.vram = makeTex2D(GL_RGBA8, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, vram.data());

  std::uint32_t pal[256] = {};
  pal[1] = 0xFF0000FFu;
  pal[2] = 0xFF00FF00u;
  d.pal = makeTex2D(GL_RGBA8, 256, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pal);
  d.tile0 = makeTex2D(GL_RGBA8, 8, 8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pal);

  glGenTextures(1, &d.color);
  glBindTexture(GL_TEXTURE_2D, d.color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.w, d.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &d.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, d.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, d.color, 0);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

  auto ss1 = drawMode7(d, 1, 1.0f);
  auto ss8 = drawMode7(d, 8, 1.0f);
  auto luma0 = drawMode7(d, 8, 0.0f);

  int u1 = uniqueRGB(ss1), u8 = uniqueRGB(ss8);
  int lit1 = countNonBlack(ss1), lit8 = countNonBlack(ss8), lit0 = countNonBlack(luma0);
  std::printf("render ss1 unique=%d lit=%d", u1, lit1);
  firstRGB(ss1);
  std::printf("; ss8 unique=%d lit=%d", u8, lit8);
  firstRGB(ss8);
  std::printf("; luma0 lit=%d\n", lit0);

  CHECK(lit1 > d.w * d.h / 4);
  CHECK(lit8 > d.w * d.h / 4);
  CHECK(u1 >= 2);
  // Unused `ss` is optimized out: 1× and 8× then match (SMK looked nearest).
  CHECK(ss1 != ss8);
  CHECK(lit0 > d.w * d.h / 4);
  CHECK(luma0 == ss8);
  // A minified blue/green checker must stay a stable average rather than acquiring coloured
  // sampling beats when the filter support exceeds the tap budget.
  int farErrors = 0;
  for(size_t i = 0; i < ss8.size(); i += 4) {
    if(std::abs(int(ss8[i + 1]) - 128) > 1
    || std::abs(int(ss8[i + 2]) - 128) > 1) farErrors++;
  }
  CHECK(farErrors == 0);

  // Extreme vertical minification: each output pixel spans about 149
  // raw texels. A 12x12 point grid aliases; interval integration must
  // converge to the checker average without a mipmap or a CPU atlas.
  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[0] = p[4] = 16.0f;
    p[3] = p[7] = 8192.0f;
  }
  glBindTexture(GL_TEXTURE_2D, d.lines);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, 240, GL_RGBA, GL_FLOAT, lines.data());
  auto distant = drawMode7(d, 12, 1.0f);
  int maxDistantError = 0;
  for(size_t i = 0; i < distant.size(); i += 4) {
    maxDistantError = std::max(maxDistantError, std::abs(int(distant[i + 1]) - 128));
    maxDistantError = std::max(maxDistantError, std::abs(int(distant[i + 2]) - 128));
  }
  std::printf("integrated minification max channel error=%d\n", maxDistantError);
  CHECK(maxDistantError <= 1);
  distant = drawMode7(d, 2, 1.0f);
  maxDistantError = 0;
  for(size_t i = 0; i < distant.size(); i += 4)
    maxDistantError = std::max(maxDistantError, std::abs(int(distant[i + 1]) - 128));
  CHECK(maxDistantError <= 1);

  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[0] = p[4] = -8192.0f;
    p[3] = p[7] = 16.0f;
  }
  glBindTexture(GL_TEXTURE_2D, d.lines);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, 240, GL_RGBA, GL_FLOAT, lines.data());
  distant = drawMode7(d, 12, 1.0f);
  maxDistantError = 0;
  for(size_t i = 0; i < distant.size(); i += 4)
    maxDistantError = std::max(maxDistantError, std::abs(int(distant[i + 1]) - 128));
  CHECK(maxDistantError <= 1);  // negative traversal and wrapped coordinates
  // Transpose the compression: rotated scenes need the same integration
  // quality along X rather than assuming every Mode 7 surface is a floor.
  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[0] = p[4] = 8192.0f;
    p[3] = p[7] = 16.0f;
  }
  glBindTexture(GL_TEXTURE_2D, d.lines);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, 240, GL_RGBA, GL_FLOAT, lines.data());
  distant = drawMode7(d, 12, 1.0f);
  maxDistantError = 0;
  for(size_t i = 0; i < distant.size(); i += 4)
    maxDistantError = std::max(maxDistantError, std::abs(int(distant[i + 1]) - 128));
  CHECK(maxDistantError <= 1);

  // Magnification must retain raw-texel box supersampling, rather than
  // bilinear/gaussian blur. Compare every pixel with an independent CPU
  // integral over the requested 12x12 sample grid of a two-colour map.
  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[0] = p[3] = p[4] = p[7] = 16.0f;
  }
  glBindTexture(GL_TEXTURE_2D, d.lines);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, 240, GL_RGBA, GL_FLOAT, lines.data());
  auto near = drawMode7(d, 12, 1.0f);
  int nearErrors = 0;
  for(int y = 0; y < d.h; y++) for(int x = 0; x < d.w; x++) {
    int green = 0;
    for(int j = 0; j < 12; j++) for(int i = 0; i < 12; i++) {
      double sx = (x + (i + 0.5) / 12.0) * 256.0 / d.w;
      double sy = (y + (j + 0.5) / 12.0) * 224.0 / d.h;
      int tx = int(std::floor((sx - 0.5) / 16.0));
      int ty = int(std::floor((sy - 0.5) / 16.0));
      green += (tx + ty) & 1;
    }
    int expected = (green * 255 + 72) / 144;
    size_t offset = (y * d.w + x) * 4;
    if(near[offset] != 0 || std::abs(int(near[offset + 1]) - expected) > 1
    || std::abs(int(near[offset + 2]) - (255 - expected)) > 1) nearErrors++;
  }
  CHECK(nearErrors == 0);
  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[0] = p[3] = p[4] = p[7] = 256.0f;
  }

  // A constant texture isolates HDMA fog from texture antialiasing.
  // At output rows between SNES lines the ramp must retain intermediate
  // 24-bit colours, but a window or Mode 7 boundary must stay discrete.
  pal[1] = pal[2] = 0xffffffffu;
  glBindTexture(GL_TEXTURE_2D, d.pal);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pal);
  std::fill(win.begin(), win.end(), 3);
  glBindTexture(GL_TEXTURE_2D, d.win);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_RED, GL_UNSIGNED_BYTE, win.data());
  for(int y = 0; y < 240; y++) {
    float* p = lines.data() + y * HDMode7::lineFloats;
    p[16] = 3.0f;
    p[17] = p[18] = p[19] = float(y & 1) * 0.5f;
  }
  auto uploadLines = [&] {
    glBindTexture(GL_TEXTURE_2D, d.lines);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, 240, GL_RGBA, GL_FLOAT, lines.data());
  };
  uploadLines();
  auto fog = drawMode7(d, 1, 1.0f);
  // First output row samples y=224/48/2=2+1/3: white minus 1/6.
  CHECK(fog[0] >= 211 && fog[0] <= 214);
  CHECK(fog[0] == fog[1] && fog[1] == fog[2]);
  lines[3 * HDMode7::lineFloats + 15] = 0.0f;
  uploadLines();
  auto edge = drawMode7(d, 1, 1.0f);
  CHECK(edge[0] == 255);
  lines[3 * HDMode7::lineFloats + 15] = HDMode7::packLineValid(false);
  uploadLines();
  std::fill(win.begin() + 3 * 256, win.begin() + 4 * 256, 0);
  glBindTexture(GL_TEXTURE_2D, d.win);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_RED, GL_UNSIGNED_BYTE, win.data());
  edge = drawMode7(d, 1, 1.0f);
  CHECK(edge[0] == 255);

  glDeleteProgram(prog);
  glDeleteFramebuffers(1, &d.fbo);
  GLuint texs[] = {d.color, d.src, d.lines, d.win, d.vram, d.pal, d.tile0};
  glDeleteTextures(7, texs);
  glDeleteVertexArrays(1, &d.vao);
}
#endif

static auto testViewportSource() -> void {
  auto vp = readFile("../../bsnes/target-bsnes/program/viewport.cpp");
  CHECK(!vp.empty());
  // 4:3 bars on a 16:9 dump: viewportSize ignored the framebuffer width.
  CHECK(vp.find("width / scale") != std::string::npos);
  CHECK(vp.find("uint videoWidth = 256 *") == std::string::npos);
  CHECK(vp.find("snesW <= 256") != std::string::npos);
}

auto main(int argc, char** argv) -> int {
  const char* shaderPath = argc > 1 ? argv[1] : "../../ruby/video/opengl/shaders.hpp";
  testPacking();
  testColorRamps();
  testShaderSource(shaderPath);
  testViewportSource();
#ifdef HD_PPU_GL
  auto file = readFile(shaderPath);
  auto frag = extractRString(file, "OpenGLMode7FragmentShader");
  if(!frag.empty()) testMode7Render(frag);
#endif
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
