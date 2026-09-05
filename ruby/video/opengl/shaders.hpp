static string OpenGLOutputVertexShader = R"(
  #version 150

  uniform vec4 targetSize;
  uniform vec4 outputSize;

  in vec2 texCoord;

  out Vertex {
    vec2 texCoord;
  } vertexOut;

  void main() {
    //center image within output window
    if(gl_VertexID == 0 || gl_VertexID == 2) {
      gl_Position.x = -(targetSize.x / outputSize.x);
    } else {
      gl_Position.x = +(targetSize.x / outputSize.x);
    }

    //center and flip vertically (buffer[0, 0] = top-left; OpenGL[0, 0] = bottom-left)
    if(gl_VertexID == 0 || gl_VertexID == 1) {
      gl_Position.y = +(targetSize.y / outputSize.y);
    } else {
      gl_Position.y = -(targetSize.y / outputSize.y);
    }

    //align image to even pixel boundary to prevent aliasing
    vec2 align = fract((outputSize.xy + targetSize.xy) / 2.0) * 2.0;
    gl_Position.xy -= align / outputSize.xy;
    gl_Position.zw = vec2(0.0, 1.0);

    vertexOut.texCoord = texCoord;
  }
)";

static string OpenGLVertexShader = R"(
  #version 150

  in vec4 position;
  in vec2 texCoord;

  out Vertex {
    vec2 texCoord;
  } vertexOut;

  void main() {
    gl_Position = position;
    vertexOut.texCoord = texCoord;
  }
)";

static string OpenGLGeometryShader = R"(
  #version 150

  layout(triangles) in;
  layout(triangle_strip, max_vertices = 3) out;

  in Vertex {
    vec2 texCoord;
  } vertexIn[];

  out Vertex {
    vec2 texCoord;
  };

  void main() {
    for(int i = 0; i < gl_in.length(); i++) {
      gl_Position = gl_in[i].gl_Position;
      texCoord = vertexIn[i].texCoord;
      EmitVertex();
    }
    EndPrimitive();
  }
)";

static string OpenGLFragmentShader = R"(
  #version 150

  uniform sampler2D source[];

  in Vertex {
    vec2 texCoord;
  };

  out vec4 fragColor;

  void main() {
    fragColor = texture(source[0], texCoord);
  }
)";

static string OpenGLMode7FragmentShader = R"(
  #version 150

  uniform sampler2D source[1];
  uniform sampler2D mode7Map;
  uniform sampler2D mode7Lines;
  uniform sampler2D mode7Tile0;
  uniform sampler2D mode7Window;
  uniform sampler2D mode7Vram;
  uniform sampler2D mode7Palette;
  uniform int ss;
  uniform float lineOrigin;
  uniform float mode7Luma;
  uniform vec4 sourceSize;
  uniform vec4 targetSize;

  in Vertex {
    vec2 texCoord;
  };

  out vec4 fragColor;

  float rec(float v) {
    return abs(v) < 1.0e-6 ? 0.0 : 1.0 / v;
  }

  float ilerp(float pa, float va, float pb, float vb, float pr) {
    if(va == vb || pr == pa) return va;
    if(pr == pb) return vb;
    float d = pb - pa;
    if(abs(d) < 1.0e-6) return va;
    return va + (vb - va) / d * (pr - pa);
  }

  vec3 applyMath(vec3 rgb, int yLine, int wx) {
    vec4 math = texelFetch(mode7Lines, ivec2(4, yLine), 0);
    vec4 back = texelFetch(mode7Lines, ivec2(5, yLine), 0);
    int flags = int(math.x + 0.5);
    int wbits = int(texelFetch(mode7Window, ivec2(wx, yLine), 0).r * 255.0 + 0.5);
    bool mathWin = (wbits & 1) != 0;
    bool aboveWin = (wbits & 2) != 0;
    // Never replace the texel with black because a window bit is off.
    // SMK's floor is Mode 7 with color-window bits that are not "clip the
    // main screen"; zeroing here painted an otherwise valid track black
    // while the CPU 1x fallback was also empty (GPU-on, no HD buffer).
    if((flags & 1) != 0 && mathWin) {
      vec3 fx = math.yzw;
      bool halve = (flags & 4) != 0 && aboveWin && (flags & 8) == 0;
      if((flags & 8) != 0 && back.w > 0.5) {
        fx = rgb;
        halve = (flags & 4) != 0 && aboveWin;
      }
      if((flags & 2) != 0) rgb = max(rgb - fx, 0.0);
      else rgb = min(rgb + fx, 1.0);
      if(halve) rgb *= 0.5;
    }
    return rgb;
  }

  bool projectM7(vec2 snes, out vec2 uv, out int repeatMode, out int yLine) {
    yLine = int(clamp(snes.y, 0.0, 239.0));
    vec4 aa = texelFetch(mode7Lines, ivec2(0, yLine), 0);
    vec4 ab = texelFetch(mode7Lines, ivec2(1, yLine), 0);
    vec4 lp = texelFetch(mode7Lines, ivec2(2, yLine), 0);
    vec4 of = texelFetch(mode7Lines, ivec2(3, yLine), 0);
    if(of.w < 1.5) {
      uv = vec2(0.0);
      repeatMode = 0;
      return false;
    }
    float ya = lp.x;
    float yb = lp.y;
    float yf = snes.y - 0.5;
    if(of.w > 2.5) yf = 255.0 - yf;
    float matA = rec(ilerp(ya, rec(aa.x), yb, rec(ab.x), yf));
    float matB = rec(ilerp(ya, rec(aa.y), yb, rec(ab.y), yf));
    float matC = rec(ilerp(ya, rec(aa.z), yb, rec(ab.z), yf));
    float matD = rec(ilerp(ya, rec(aa.w), yb, rec(ab.w), yf));
    int packBits = int(of.z + 0.5);
    float xf = snes.x - 0.5;
    if((packBits & 1) != 0) xf = 255.0 - xf;
    repeatMode = (packBits / 2) & 3;
    float hcenter = lp.z;
    float vcenter = lp.w;
    float ht = of.x;
    float vty = of.y + yf;
    float originX = matA * ht + matB * vty + hcenter * 256.0;
    float originY = matC * ht + matD * vty + vcenter * 256.0;
    uv = vec2(originX + matA * xf, originY + matC * xf) / 256.0;
    return true;
  }

  vec4 decodeVram(int px, int py) {
    int tileWord = (py >> 3 & 127) * 128 + (px >> 3 & 127);
    int tile = int(texelFetch(mode7Vram, ivec2(tileWord & 127, tileWord >> 7), 0).r * 255.0 + 0.5);
    int pixWord = (tile << 6) + ((py & 7) << 3) + (px & 7);
    int pal = int(texelFetch(mode7Vram, ivec2(pixWord & 127, pixWord >> 7), 0).g * 255.0 + 0.5);
    if(pal == 0) return vec4(0.0);
    // Palette upload is BGRA; some drivers present A=0 even when the CPU
    // packed 0xff. RGB is the Mode 7 colour — force opaque so shadeM7
    // cannot discard a valid texel (SMK floor went black that way).
    vec4 c = texelFetch(mode7Palette, ivec2(pal, 0), 0);
    return vec4(c.rgb, 1.0);
  }

  vec4 sampleMap(vec2 uv, int repeatMode, vec2 duvdx, vec2 duvdy) {
    bool oob = uv.x < 0.0 || uv.x >= 1024.0 || uv.y < 0.0 || uv.y >= 1024.0;
    if(oob) {
      if(repeatMode == 2) return vec4(0.0);
      if(repeatMode == 3) {
        ivec2 t = ivec2(int(floor(uv.x)) & 7, int(floor(uv.y)) & 7);
        return texelFetch(mode7Tile0, t, 0);
      }
    }
    int px = int(floor(uv.x));
    int py = int(floor(uv.y));
    if(repeatMode < 2) { px &= 1023; py &= 1023; }
    // Always decode live VRAM. The mip atlas is allowed to be stale or
    // empty (SMK loads CHR after the tilemap); using it as an opaque
    // yellow floor hid the track.
    return decodeVram(px, py);
  }

  vec2 wrapDiff(vec2 d) {
    if(d.x > 512.0) d.x -= 1024.0; if(d.x < -512.0) d.x += 1024.0;
    if(d.y > 512.0) d.y -= 1024.0; if(d.y < -512.0) d.y += 1024.0;
    return d;
  }

  vec4 shadeM7(vec4 texel, vec2 snes, int y) {
    if(texel.a < 0.02 && texel.r + texel.g + texel.b < 0.01) return vec4(0.0);
    int wx = int(clamp(snes.x, 0.0, 255.0));
    vec3 rgb = applyMath(texel.rgb, y, wx);
    // Palette is already lightTable[15]. Per-line brightness is a dimming
    // factor. vblank INIDISP is often 0; never let that zero the track.
    float luma = mode7Luma;
    if(luma < 1.0 / 15.0) luma = 1.0;
    rgb *= luma;
    return vec4(rgb, 1.0);
  }

  vec4 sampleM7(vec2 snes, vec2 duvdx, vec2 duvdy) {
    vec2 uv;
    int repeatMode;
    int y;
    if(!projectM7(snes, uv, repeatMode, y)) return vec4(0.0);
    return shadeM7(sampleMap(uv, repeatMode, duvdx, duvdy), snes, y);
  }

  void main() {
    vec4 spr = texture(source[0], texCoord);
    if(spr.a > 0.75) {
      fragColor = spr;
      return;
    }
    if(spr.a > 0.25) {
      fragColor = vec4(spr.rgb, 1.0);
      return;
    }
    float scale = max(sourceSize.x / 256.0, 1.0);
    float snesH = sourceSize.y / scale;
    vec2 snes = vec2(texCoord.x * 256.0, lineOrigin + texCoord.y * snesH);
    vec2 pixel = vec2(256.0, snesH) / max(targetSize.xy, vec2(1.0));
    // Maximize shrinks a window pixel in SNES space, so far SMK grass turns
    // into a checker. Keep at least the ~4.5× windowed footprint. This is
    // not a 1-SNES-pixel blur (that smeared F-Zero).
    vec2 kernel = max(pixel, vec2(256.0 / 1280.0, snesH / 960.0));
    int n = ss < 1 ? 1 : ss;
    if(n > 16) n = 16;
    vec4 acc = vec4(0.0);
    float hits = 0.0;
    for(int j = 0; j < n; j++) {
      for(int i = 0; i < n; i++) {
        vec2 o = (vec2(float(i), float(j)) + 0.5) / float(n) - 0.5;
        vec2 s = snes + o * kernel;
        vec2 uv;
        int repeatMode;
        int y;
        if(!projectM7(s, uv, repeatMode, y)) continue;
        vec4 c = shadeM7(sampleMap(uv, repeatMode, vec2(0.0), vec2(0.0)), s, y);
        if(c.a > 0.01) {
          acc += c;
          hits += 1.0;
        }
      }
    }
    if(hits > 0.0) {
      fragColor = vec4(acc.rgb / hits, 1.0);
      return;
    }
    fragColor = vec4(spr.rgb, 1.0);
  }
)";

// Expand Mode 7 VRAM (128×128 words: R=tilemap, G=character) + 256-colour
// palette into the 1024² colour atlas. Runs on the GPU; the CPU must not
// walk a million pixels when a sprite tile changes.
static string OpenGLMode7MapVertexShader = R"(
  #version 150

  out vec2 uv;

  void main() {
    float x = (gl_VertexID == 1 || gl_VertexID == 3) ? 1.0 : 0.0;
    float y = (gl_VertexID == 2 || gl_VertexID == 3) ? 1.0 : 0.0;
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(x, y);
  }
)";

static string OpenGLMode7MapFragmentShader = R"(
  #version 150

  uniform sampler2D mode7Vram;
  uniform sampler2D mode7Palette;

  in vec2 uv;
  out vec4 fragColor;

  vec2 fetchWord(int word) {
    return texelFetch(mode7Vram, ivec2(word & 127, word >> 7), 0).rg;
  }

  void main() {
    int px = int(gl_FragCoord.x);
    int py = int(gl_FragCoord.y);
    int tile = int(fetchWord((py >> 3) * 128 + (px >> 3)).r * 255.0 + 0.5);
    int pal = int(fetchWord((tile << 6) + ((py & 7) << 3) + (px & 7)).g * 255.0 + 0.5);
    if(pal == 0) {
      fragColor = vec4(0.0);
      return;
    }
    fragColor = texelFetch(mode7Palette, ivec2(pal, 0), 0);
  }
)";
