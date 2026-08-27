// Sanity tests for HD GPU Mode 7 packing and the Mode 7 fragment shader.
// Run: make -C tests/hd-ppu run
//
// These exist because a GLSL reserved-word compile failure silently dropped
// GPU SS (CPU sampScale stays 1), and because color-math RGB was uploaded as BGR.

#include "gpu-mode7.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

static auto compileFragment(const std::string& source, std::string& log) -> bool {
  GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
  const char* p = source.c_str();
  glShaderSource(shader, 1, &p, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  if(length > 1) {
    log.resize(size_t(length));
    glGetShaderInfoLog(shader, length, &length, log.data());
    log.resize(size_t(length));
  }
  glDeleteShader(shader);
  return ok == GL_TRUE;
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
}

static auto testShaderSource(const std::string& path) -> void {
  auto file = readFile(path);
  CHECK(!file.empty());
  auto frag = extractRString(file, "OpenGLMode7FragmentShader");
  CHECK(!frag.empty());
  CHECK(frag.find("#version 150") != std::string::npos);

  auto body = stripComments(frag);
  CHECK(!hasIdent(body, "packed"));  // reserved in GLSL; broke GPU SS
  CHECK(hasIdent(body, "applyMath") || frag.find("flags & 1") != std::string::npos);
  CHECK(frag.find("math.yzw") != std::string::npos);
  CHECK(frag.find("mode7Window") != std::string::npos);

#ifdef HD_PPU_GL
  static bool tried = false, haveGL = false;
  if(!tried) {
    tried = true;
    haveGL = makeGL32();
    if(!haveGL) std::printf("skip: no EGL OpenGL 3.2 context (source checks still ran)\n");
  }
  if(haveGL) {
    std::string log;
    bool ok = compileFragment(frag, log);
    if(!ok) std::fprintf(stderr, "shader log:\n%s\n", log.c_str());
    CHECK(ok);
  }
#else
  std::printf("skip: HD_PPU_GL not built (source checks still ran)\n");
#endif
}

auto main(int argc, char** argv) -> int {
  const char* shaderPath = argc > 1 ? argv[1] : "../../ruby/video/opengl/shaders.hpp";
  testPacking();
  testShaderSource(shaderPath);
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
