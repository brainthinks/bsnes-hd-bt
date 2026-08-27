#if defined(DISPLAY_XORG)
  #include <GL/gl.h>
  #include <GL/glext.h>
  #include <GL/glx.h>
  #ifndef glGetProcAddress
    #define glGetProcAddress(name) (*glXGetProcAddress)((const GLubyte*)(name))
  #endif
#elif defined(DISPLAY_QUARTZ)
  #include <OpenGL/gl3.h>
#elif defined(DISPLAY_WINDOWS)
  #include <GL/gl.h>
  #include <GL/glext.h>
  #ifndef glGetProcAddress
    #define glGetProcAddress(name) wglGetProcAddress(name)
  #endif
#else
  #error "ruby::OpenGL3: unsupported platform"
#endif

#include "bind.hpp"
#include "shaders.hpp"
#include "utility.hpp"

struct OpenGL;

struct OpenGLTexture {
  auto getFormat() const -> GLuint;
  auto getType() const -> GLuint;

  GLuint texture = 0;
  uint width = 0;
  uint height = 0;
  GLuint format = GL_RGBA8;
  GLuint filter = GL_LINEAR;
  GLuint wrap = GL_CLAMP_TO_BORDER;
};

struct OpenGLSurface : OpenGLTexture {
  auto allocate() -> void;
  auto size(uint width, uint height) -> void;
  auto release() -> void;
  auto render(uint sourceWidth, uint sourceHeight, uint targetX, uint targetY, uint targetWidth, uint targetHeight) -> void;

  GLuint program = 0;
  GLuint framebuffer = 0;
  GLuint vao = 0;
  GLuint vbo[3] = {0, 0, 0};
  GLuint vertex = 0;
  GLuint geometry = 0;
  GLuint fragment = 0;
  uint32_t* buffer = nullptr;
};

struct OpenGLProgram : OpenGLSurface {
  auto bind(OpenGL* instance, const Markup::Node& node, const string& pathname) -> void;
  auto parse(OpenGL* instance, string& source) -> void;
  auto release() -> void;

  uint phase = 0;   //frame counter
  uint modulo = 0;  //frame counter modulus
  uint absoluteWidth = 0;
  uint absoluteHeight = 0;
  double relativeWidth = 0;
  double relativeHeight = 0;
  vector<OpenGLTexture> pixmaps;
};

struct OpenGL : OpenGLProgram {
  auto setShader(const string& pathname) -> void;
  auto allocateHistory(uint size) -> void;
  auto clear() -> void;
  auto lock(uint32_t*& data, uint& pitch) -> bool;
  auto output() -> void;
  auto initialize(const string& shader) -> bool;
  auto terminate() -> void;

  vector<OpenGLProgram> programs;
  vector<OpenGLTexture> history;
  GLuint inputFormat = GL_RGBA8;
  uint outputX = 0;
  uint outputY = 0;
  uint outputWidth = 0;
  uint outputHeight = 0;
  struct Setting {
    string name;
    string value;
    bool operator< (const Setting& source) const { return name <  source.name; }
    bool operator==(const Setting& source) const { return name == source.name; }
    Setting() = default;
    Setting(const string& name) : name(name) {}
    Setting(const string& name, const string& value) : name(name), value(value) {}
  };
  set<Setting> settings;
  bool initialized = false;

  auto setMode7Gpu(bool enable, uint ss, float lineOrigin, const uint32_t* map, const float* lines, const uint32_t* tile0, const uint8_t* colorWindow) -> void;
  auto outputMode7() -> bool;
  GLuint mode7Program = 0;
  GLuint mode7Vertex = 0;
  GLuint mode7Fragment = 0;
  GLuint mode7MapTex = 0;
  GLuint mode7LineTex = 0;
  GLuint mode7Tile0Tex = 0;
  GLuint mode7WindowTex = 0;
  bool mode7Gpu = false;
  uint mode7Ss = 1;
  float mode7LineOrigin = 0;
  const uint32_t* mode7Map = nullptr;
  const float* mode7Lines = nullptr;
  const uint32_t* mode7Tile0 = nullptr;
  const uint8_t* mode7Window = nullptr;
};

#include "texture.hpp"
#include "surface.hpp"
#include "program.hpp"
#include "main.hpp"
