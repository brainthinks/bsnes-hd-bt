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
  uniform int ss;
  uniform float lineOrigin;
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

  vec4 sampleM7(vec2 snes) {
    int y = int(clamp(snes.y, 0.0, 239.0));
    vec4 aa = texelFetch(mode7Lines, ivec2(0, y), 0);
    vec4 ab = texelFetch(mode7Lines, ivec2(1, y), 0);
    vec4 lp = texelFetch(mode7Lines, ivec2(2, y), 0);
    vec4 of = texelFetch(mode7Lines, ivec2(3, y), 0);
    // of.w is 0 for scanlines the CPU did not render as Mode 7.
    if(of.w < 1.5) return vec4(0.0);
    float ya = lp.x;
    float yb = lp.y;
    float yf = snes.y - 0.5;
    if(of.w > 2.5) yf = 255.0 - yf;
    float a = rec(ilerp(ya, rec(aa.x), yb, rec(ab.x), yf));
    float b = rec(ilerp(ya, rec(aa.y), yb, rec(ab.y), yf));
    float c = rec(ilerp(ya, rec(aa.z), yb, rec(ab.z), yf));
    float d = rec(ilerp(ya, rec(aa.w), yb, rec(ab.w), yf));
    float xf = snes.x - 0.5;
    if(of.z > 0.5) xf = 255.0 - xf;
    float hcenter = lp.z;
    float vcenter = lp.w;
    float ht = of.x;
    float vty = of.y + yf;
    float originX = a * ht + b * vty + hcenter * 256.0;
    float originY = c * ht + d * vty + vcenter * 256.0;
    vec2 uv = vec2(originX + a * xf, originY + c * xf) / 256.0;
    ivec2 ip = ivec2(uv);
    ip &= 1023;
    return texelFetch(mode7Map, ip, 0);
  }

  void main() {
    vec4 spr = texture(source[0], texCoord);
    float scale = max(sourceSize.x / 256.0, 1.0);
    float snesH = sourceSize.y / scale;
    vec2 snes = vec2(texCoord.x * 256.0, lineOrigin + texCoord.y * snesH);
    vec2 pixel = vec2(256.0, snesH) / max(targetSize.xy, vec2(1.0));
    vec4 acc = vec4(0.0);
    int n = ss < 1 ? 1 : ss;
    for(int j = 0; j < 16; j++) {
      if(j >= n) break;
      for(int i = 0; i < 16; i++) {
        if(i >= n) break;
        vec2 o = (vec2(float(i), float(j)) + 0.5) / float(n) - 0.5;
        acc += sampleM7(snes + o * pixel);
      }
    }
    acc /= float(n * n);
    vec4 dest = vec4(spr.rgb, 1.0);
    dest = mix(dest, acc, acc.a);
    fragColor = mix(dest, spr, spr.a);
  }
)";
