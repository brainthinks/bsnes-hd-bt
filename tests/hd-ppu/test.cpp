// Sanity tests for HD GPU Mode 7 packing and the Mode 7 fragment shader.
// Run: make -C tests/hd-ppu run
//
// These exist because GPU SS can fail silently (CPU sampScale stays 1):
// a GLSL reserved-word compile miss, unused `ss` optimized out, a vblank
// flush wiping Mode 7 lines, palette-alpha discard, and window math
// zeroing the SMK floor. The GL render path draws a checker and checks
// that 1× and 8× SS differ and that luma 0 does not black the image.

#include "gpu-mode7.hpp"

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
  CHECK(frag.find("j < 16") == std::string::npos);
  CHECK(frag.find("int n = ss") != std::string::npos);
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

  glDeleteProgram(prog);
  glDeleteFramebuffers(1, &d.fbo);
  GLuint texs[] = {d.color, d.src, d.lines, d.win, d.vram, d.pal, d.tile0};
  glDeleteTextures(7, texs);
  glDeleteVertexArrays(1, &d.vao);
}
#endif

auto main(int argc, char** argv) -> int {
  const char* shaderPath = argc > 1 ? argv[1] : "../../ruby/video/opengl/shaders.hpp";
  testPacking();
  testShaderSource(shaderPath);
#ifdef HD_PPU_GL
  auto file = readFile(shaderPath);
  auto frag = extractRString(file, "OpenGLMode7FragmentShader");
  if(!frag.empty()) testMode7Render(frag);
#endif
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
