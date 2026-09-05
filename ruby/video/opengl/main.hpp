auto OpenGL::setShader(const string& pathname) -> void {
  for(auto& program : programs) program.release();
  programs.reset();

  settings.reset();

  format = inputFormat;
  filter = GL_LINEAR;
  wrap = GL_CLAMP_TO_BORDER;
  absoluteWidth = 0, absoluteHeight = 0;
  relativeWidth = 0, relativeHeight = 0;

  uint historySize = 0;
  if(pathname == "None") {
    filter = GL_NEAREST;
  } else if(pathname == "Blur") {
    filter = GL_LINEAR;
  } else if(directory::exists(pathname)) {
    auto document = BML::unserialize(file::read({pathname, "manifest.bml"}));

    for(auto node : document["settings"]) {
      settings.insert({node.name(), node.text()});
    }

    for(auto node : document["input"]) {
      if(node.name() == "history") historySize = node.natural();
      if(node.name() == "format") format = glrFormat(node.text());
      if(node.name() == "filter") filter = glrFilter(node.text());
      if(node.name() == "wrap") wrap = glrWrap(node.text());
    }

    for(auto node : document["output"]) {
      string text = node.text();
      if(node.name() == "width") {
        if(text.endsWith("%")) relativeWidth = toReal(text.trimRight("%", 1L)) / 100.0;
        else absoluteWidth = text.natural();
      }
      if(node.name() == "height") {
        if(text.endsWith("%")) relativeHeight = toReal(text.trimRight("%", 1L)) / 100.0;
        else absoluteHeight = text.natural();
      }
    }

    for(auto node : document.find("program")) {
      uint n = programs.size();
      programs(n).bind(this, node, pathname);
    }
  }

  //changing shaders may change input format, which requires the input texture to be recreated
  if(texture) { glDeleteTextures(1, &texture); texture = 0; }
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, getFormat(), getType(), buffer);
  allocateHistory(historySize);
}

auto OpenGL::allocateHistory(uint size) -> void {
  for(auto& frame : history) glDeleteTextures(1, &frame.texture);
  history.reset();
  while(size--) {
    OpenGLTexture frame;
    frame.filter = filter;
    frame.wrap = wrap;
    glGenTextures(1, &frame.texture);
    glBindTexture(GL_TEXTURE_2D, frame.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, frame.width = width, frame.height = height, 0, getFormat(), getType(), buffer);
    history.append(frame);
  }
}

auto OpenGL::clear() -> void {
  for(auto& p : programs) {
    glUseProgram(p.program);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p.framebuffer);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glUseProgram(0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}

auto OpenGL::lock(uint32_t*& data, uint& pitch) -> bool {
  pitch = width * sizeof(uint32_t);
  return data = buffer;
}

auto OpenGL::setMode7Gpu(bool enable, uint ss, float lineOrigin, const uint16_t* vram, const uint32_t* palette, const uint32_t* tile0, const float* lines, const uint8_t* colorWindow, uint64_t mapHash, float luma) -> void {
  mode7Gpu = enable;
  mode7Ss = ss ? ss : 1;
  mode7LineOrigin = lineOrigin;
  mode7Vram = vram;
  mode7Palette = palette;
  mode7Lines = lines;
  mode7Tile0 = tile0;
  mode7Window = colorWindow;
  mode7Luma = luma;
  if(!enable || mapHash != mode7MapHash) {
    mode7MapHash = mapHash;
    mode7MapReady = false;
  }
}

#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

auto OpenGL::uploadMode7VramPalette() -> void {
  if(!mode7Vram || !mode7Palette) return;
  uint8_t rgba[128 * 128 * 4];
  for(uint n = 0; n < 16384; n++) {
    uint16_t w = mode7Vram[n];
    rgba[n * 4 + 0] = (uint8_t)(w & 255);
    rgba[n * 4 + 1] = (uint8_t)(w >> 8);
    rgba[n * 4 + 2] = 0;
    rgba[n * 4 + 3] = 255;
  }
  if(!mode7VramTex) {
    glGenTextures(1, &mode7VramTex);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, mode7VramTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glrParameters(GL_NEAREST, GL_CLAMP_TO_EDGE);
  } else {
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, mode7VramTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
  if(!mode7PaletteTex) {
    glGenTextures(1, &mode7PaletteTex);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, mode7PaletteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mode7Palette);
    glrParameters(GL_NEAREST, GL_CLAMP_TO_EDGE);
  } else {
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, mode7PaletteTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mode7Palette);
  }
}

auto OpenGL::rebuildMode7Map() -> void {
  if(!mode7MapProgram || !mode7Vram || !mode7Palette) return;

  uploadMode7VramPalette();

  if(!mode7MapTex) {
    glGenTextures(1, &mode7MapTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mode7MapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024, 1024, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.25f);
    GLfloat aniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
    if(aniso >= 2.0f) {
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso > 8.0f ? 8.0f : aniso);
    }
    glGenFramebuffers(1, &mode7MapFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mode7MapFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mode7MapTex, 0);
  }

  GLuint saved = program;
  program = mode7MapProgram;
  glUseProgram(mode7MapProgram);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mode7MapFbo);
  glrUniform1i("mode7Vram", 5);
  glrUniform1i("mode7Palette", 6);
  glViewport(0, 0, 1024, 1024);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  program = saved;

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, mode7MapTex);
  glGenerateMipmap(GL_TEXTURE_2D);

  if(!mode7Tile0Tex) glGenTextures(1, &mode7Tile0Tex);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, mode7Tile0Tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mode7Tile0 ? mode7Tile0 : mode7Palette);
  glrParameters(GL_NEAREST, GL_REPEAT);
}

auto OpenGL::outputMode7() -> bool {
  auto gpuLog = [](const char* msg) {
    if(!getenv("BSNES_DUMP_GPU")) return;
    static FILE* fp = nullptr;
    if(!fp) fp = fopen("/tmp/bsnes-hd-gpu.log", "w");
    if(!fp) return;
    fputs(msg, fp);
    fputc('\n', fp);
    fflush(fp);
    fputs(msg, stderr);
    fputc('\n', stderr);
  };
  if(!mode7Gpu || !mode7Program || !mode7Vram || !mode7Palette || !mode7Lines) {
    static bool once = false;
    if(!once) {
      once = true;
      char buf[256];
      snprintf(buf, sizeof buf, "[bsnes-hd] GPU Mode 7 skipped gpu=%d prog=%u vram=%p pal=%p lines=%p",
        (int)mode7Gpu, (unsigned)mode7Program, (const void*)mode7Vram, (const void*)mode7Palette, (const void*)mode7Lines);
      gpuLog(buf);
    }
    return false;
  }

  uploadMode7VramPalette();
  if(!mode7VramTex || !mode7PaletteTex) return false;
  if(mode7MapProgram && !mode7MapReady) rebuildMode7Map();
  mode7MapReady = true;

  if(!mode7LineTex) {
    glGenTextures(1, &mode7LineTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mode7LineTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 6, 240, 0, GL_RGBA, GL_FLOAT, nullptr);
    glrParameters(GL_NEAREST, GL_CLAMP_TO_EDGE);
  }
  if(!mode7WindowTex) {
    glGenTextures(1, &mode7WindowTex);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, mode7WindowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 240, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glrParameters(GL_NEAREST, GL_CLAMP_TO_EDGE);
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, mode7MapTex ? mode7MapTex : mode7VramTex);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, mode7LineTex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 6, 240, GL_RGBA, GL_FLOAT, mode7Lines);

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, mode7Tile0Tex ? mode7Tile0Tex : mode7PaletteTex);

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, mode7WindowTex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_RED, GL_UNSIGNED_BYTE, mode7Window);

  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, mode7VramTex);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, mode7PaletteTex);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glrParameters(GL_NEAREST, GL_CLAMP_TO_BORDER);

  GLuint saved = program;
  program = mode7Program;
  glUseProgram(mode7Program);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glrUniform1i("source[0]", 0);
  glrUniform1i("mode7Map", 1);
  glrUniform1i("mode7Lines", 2);
  glrUniform1i("mode7Tile0", 3);
  glrUniform1i("mode7Window", 4);
  glrUniform1i("mode7Vram", 5);
  glrUniform1i("mode7Palette", 6);
  GLint locSs = glGetUniformLocation(mode7Program, "ss");
  glrUniform1i("ss", (GLint)mode7Ss);
  glrUniform1f("lineOrigin", mode7LineOrigin);
  glrUniform1f("mode7Luma", mode7Luma);
  {
    static bool once = false;
    if(!once) {
      once = true;
      uint32_t pix = buffer && width > 80 && height > 80 ? buffer[(height / 2) * width + (width / 2)] : 0;
      uint32_t pal1 = mode7Palette ? mode7Palette[1] : 0;
      uint valid = 0;
      int y0 = -1, y1 = -1;
      if(mode7Lines) {
        for(uint y = 0; y < 240; y++) {
          if(mode7Lines[y * 24 + 15] < 1.5f) continue;
          if(y0 < 0) y0 = (int)y;
          y1 = (int)y;
          valid++;
        }
      }
      char buf[512];
      snprintf(buf, sizeof buf,
        "[bsnes-hd] GPU Mode 7 ss=%u loc(ss)=%d luma=%.3f origin=%.1f pal1=0x%08x validLines=%u y=%d..%d vramTex=%u palTex=%u midPixel=0x%08x %ux%u",
        (unsigned)mode7Ss, (int)locSs, mode7Luma, mode7LineOrigin, (unsigned)pal1,
        valid, y0, y1, (unsigned)mode7VramTex, (unsigned)mode7PaletteTex,
        (unsigned)pix, (unsigned)width, (unsigned)height);
      gpuLog(buf);
      if(mode7Lines && y0 >= 0) {
        const float* p = mode7Lines + y0 * 24;
        snprintf(buf, sizeof buf,
          "[bsnes-hd] line %d A=%.1f B=%.1f C=%.1f D=%.1f math=%.1f fx=%.3f,%.3f,%.3f pack=%.0f valid=%.0f",
          y0, p[0], p[1], p[2], p[3], p[16], p[17], p[18], p[19], p[14], p[15]);
        gpuLog(buf);
        int yMid = (y0 + y1) / 2;
        while(yMid > y0 && mode7Lines[yMid * 24 + 15] < 1.5f) yMid--;
        p = mode7Lines + yMid * 24;
        snprintf(buf, sizeof buf,
          "[bsnes-hd] line %d A=%.1f B=%.1f C=%.1f D=%.1f math=%.1f fx=%.3f,%.3f,%.3f pack=%.0f valid=%.0f",
          yMid, p[0], p[1], p[2], p[3], p[16], p[17], p[18], p[19], p[14], p[15]);
        gpuLog(buf);
      }
    }
  }
  float sw = width ? width : 1, sh = height ? height : 1;
  uint targetWidth = absoluteWidth ? absoluteWidth : (outputWidth ? outputWidth : 1);
  uint targetHeight = absoluteHeight ? absoluteHeight : (outputHeight ? outputHeight : 1);
  float tw = targetWidth, th = targetHeight;
  float ow = outputWidth ? outputWidth : 1, oh = outputHeight ? outputHeight : 1;
  glrUniform4f("sourceSize", sw, sh, 1.0 / sw, 1.0 / sh);
  glrUniform4f("targetSize", tw, th, 1.0 / tw, 1.0 / th);
  glrUniform4f("outputSize", ow, oh, 1.0 / ow, 1.0 / oh);
  render(width, height, outputX, outputY, outputWidth, outputHeight);
  if(auto dump = getenv("BSNES_DUMP_GPU")) {
    static bool mathOnce = false;
    if(!mathOnce && mode7Lines) {
      mathOnce = true;
      if(auto fp = fopen("/tmp/bsnes-math.txt", "w")) {
        fprintf(fp, "ss=%u luma=%.4f origin=%.2f %ux%u\n",
          (unsigned)mode7Ss, mode7Luma, mode7LineOrigin, (unsigned)width, (unsigned)height);
        if(buffer && width && height) {
          uint xs[4] = {width / 2, width / 2, width / 2, width / 2};
          uint ys[4] = {height / 5, height / 3, height / 2, (height * 2) / 3};
          for(uint i = 0; i < 4; i++) {
            uint x = xs[i], y = ys[i];
            if(x >= width || y >= height) continue;
            fprintf(fp, "cpu %u,%u = 0x%08x\n", x, y, (unsigned)buffer[y * width + x]);
          }
        }
        for(uint y = 0; y < 240; y++) {
          const float* p = mode7Lines + y * 24;
          if(p[15] < 1.5f) continue;
          uint mathN = 0, aboveN = 0, visN = 0;
          if(mode7Window) {
            const uint8_t* w = mode7Window + y * 256;
            for(uint x = 0; x < 256; x++) {
              if(w[x] & 1) mathN++;
              if(w[x] & 2) aboveN++;
              if(w[x] & 4) visN++;
            }
          }
          fprintf(fp, "y=%u A=%.1f B=%.1f C=%.1f D=%.1f pack=%.0f valid=%.0f math=%.0f fx=%.3f,%.3f,%.3f winMath=%u winAbove=%u bg1vis=%u\n",
            y, p[0], p[1], p[2], p[3], p[14], p[15], p[16], p[17], p[18], p[19], mathN, aboveN, visN);
        }
        fclose(fp);
      }
    }
    static uint dumped = 0;
    GLint captureViewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, captureViewport);
    bool captureReady = !getenv("BSNES_DUMP_FULLSCREEN")
      || (captureViewport[2] >= 1920 && captureViewport[3] >= 1000);
    uint captureAfter = 8;
    if(auto value = getenv("BSNES_DUMP_GPU_AFTER")) {
      int requested = atoi(value);
      if(requested > 0) captureAfter = (uint)requested;
    }
    if(captureReady && ++dumped >= captureAfter) {
      int w = 0, h = 0;
      uint8_t* px = nullptr;
      int dw = getenv("BSNES_DUMP_W") ? atoi(getenv("BSNES_DUMP_W")) : 0;
      int dh = getenv("BSNES_DUMP_H") ? atoi(getenv("BSNES_DUMP_H")) : 0;
      if(dw >= 256 && dh >= 224) {
        GLint prevTex = 0;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        GLuint fbo = 0, tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, prevTex);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glrUniform4f("targetSize", (float)dw, (float)dh, 1.0f / dw, 1.0f / dh);
        glrUniform4f("outputSize", (float)dw, (float)dh, 1.0f / dw, 1.0f / dh);
        render(width, height, 0, 0, dw, dh);
        w = dw; h = dh;
        px = new uint8_t[(size_t)w * (size_t)h * 4];
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
      } else {
        GLint vp[4] = {};
        glGetIntegerv(GL_VIEWPORT, vp);
        w = vp[2]; h = vp[3];
        if(w > 0 && h > 0) {
          px = new uint8_t[(size_t)w * (size_t)h * 4];
          glReadPixels(vp[0], vp[1], w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        }
      }
      if(px && w > 0 && h > 0) {
        if(auto fp = fopen(string{dump, ".lines.bin"}, "wb")) {
          fwrite(mode7Lines, sizeof(float), 240 * 24, fp);
          fclose(fp);
        }
        if(auto fp = fopen(string{dump, ".vram.bin"}, "wb")) {
          fwrite(mode7Vram, sizeof(uint16_t), 16384, fp);
          fclose(fp);
        }
        if(auto fp = fopen(string{dump, ".palette.bin"}, "wb")) {
          fwrite(mode7Palette, sizeof(uint32_t), 256, fp);
          fclose(fp);
        }
        if(auto fp = fopen(string{dump, ".geometry.txt"}, "w")) {
          fprintf(fp, "%u %u %u %u %u %u %.9g\n", width, height,
            targetWidth, targetHeight, outputWidth, outputHeight, mode7LineOrigin);
          fclose(fp);
        }
        if(auto fp = fopen(dump, "wb")) {
          fprintf(fp, "P6\n%d %d\n255\n", w, h);
          for(int y = h - 1; y >= 0; y--) {
            for(int x = 0; x < w; x++) {
              auto* p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
              fputc(p[0], fp); fputc(p[1], fp); fputc(p[2], fp);
            }
          }
          fclose(fp);
        }
        delete[] px;
      }
      _exit(0);
    }
  }
  program = saved;
  return true;
}

auto OpenGL::output() -> void {
  clear();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, getFormat(), getType(), buffer);

  if(outputMode7()) return;

  struct Source {
    GLuint texture;
    uint width, height;
    GLuint filter, wrap;
  };
  vector<Source> sources;
  sources.prepend({texture, width, height, filter, wrap});

  for(auto& p : programs) {
    uint targetWidth = p.absoluteWidth ? p.absoluteWidth : outputWidth;
    uint targetHeight = p.absoluteHeight ? p.absoluteHeight : outputHeight;
    if(p.relativeWidth) targetWidth = sources[0].width * p.relativeWidth;
    if(p.relativeHeight) targetHeight = sources[0].height * p.relativeHeight;

    p.size(targetWidth, targetHeight);
    glUseProgram(p.program);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p.framebuffer);

    glrUniform1i("phase", p.phase);
    glrUniform1i("historyLength", history.size());
    glrUniform1i("sourceLength", sources.size());
    glrUniform1i("pixmapLength", p.pixmaps.size());
    glrUniform4f("targetSize", targetWidth, targetHeight, 1.0 / targetWidth, 1.0 / targetHeight);
    glrUniform4f("outputSize", outputWidth, outputHeight, 1.0 / outputWidth, 1.0 / outputHeight);

    uint aid = 0;
    for(auto& frame : history) {
      glrUniform1i({"history[", aid, "]"}, aid);
      glrUniform4f({"historySize[", aid, "]"}, frame.width, frame.height, 1.0 / frame.width, 1.0 / frame.height);
      glActiveTexture(GL_TEXTURE0 + (aid++));
      glBindTexture(GL_TEXTURE_2D, frame.texture);
      glrParameters(frame.filter, frame.wrap);
    }

    uint bid = 0;
    for(auto& source : sources) {
      glrUniform1i({"source[", bid, "]"}, aid + bid);
      glrUniform4f({"sourceSize[", bid, "]"}, source.width, source.height, 1.0 / source.width, 1.0 / source.height);
      glActiveTexture(GL_TEXTURE0 + aid + (bid++));
      glBindTexture(GL_TEXTURE_2D, source.texture);
      glrParameters(source.filter, source.wrap);
    }

    uint cid = 0;
    for(auto& pixmap : p.pixmaps) {
      glrUniform1i({"pixmap[", cid, "]"}, aid + bid + cid);
      glrUniform4f({"pixmapSize[", bid, "]"}, pixmap.width, pixmap.height, 1.0 / pixmap.width, 1.0 / pixmap.height);
      glActiveTexture(GL_TEXTURE0 + aid + bid + (cid++));
      glBindTexture(GL_TEXTURE_2D, pixmap.texture);
      glrParameters(pixmap.filter, pixmap.wrap);
    }

    glActiveTexture(GL_TEXTURE0);
    glrParameters(sources[0].filter, sources[0].wrap);
    p.render(sources[0].width, sources[0].height, 0, 0, targetWidth, targetHeight);
    glBindTexture(GL_TEXTURE_2D, p.texture);

    p.phase = (p.phase + 1) % p.modulo;
    sources.prepend({p.texture, p.width, p.height, p.filter, p.wrap});
  }

  uint targetWidth = absoluteWidth ? absoluteWidth : outputWidth;
  uint targetHeight = absoluteHeight ? absoluteHeight : outputHeight;
  if(relativeWidth) targetWidth = sources[0].width * relativeWidth;
  if(relativeHeight) targetHeight = sources[0].height * relativeHeight;

  glUseProgram(program);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

  glrUniform1i("source[0]", 0);
  glrUniform4f("targetSize", targetWidth, targetHeight, 1.0 / targetWidth, 1.0 / targetHeight);
  glrUniform4f("outputSize", outputWidth, outputHeight, 1.0 / outputWidth, 1.0 / outputHeight);

  glrParameters(sources[0].filter, sources[0].wrap);
  render(sources[0].width, sources[0].height, outputX, outputY, outputWidth, outputHeight);

  if(history.size() > 0) {
    OpenGLTexture frame = history.takeRight();

    glBindTexture(GL_TEXTURE_2D, frame.texture);
    if(width == frame.width && height == frame.height) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, getFormat(), getType(), buffer);
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, format, frame.width = width, frame.height = height, 0, getFormat(), getType(), buffer);
    }

    history.prepend(frame);
  }
}

auto OpenGL::initialize(const string& shader) -> bool {
  if(!OpenGLBind()) return false;

  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_POLYGON_SMOOTH);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_DITHER);

  program = glCreateProgram();
  vertex = glrCreateShader(program, GL_VERTEX_SHADER, OpenGLOutputVertexShader);
//geometry = glrCreateShader(program, GL_GEOMETRY_SHADER, OpenGLGeometryShader);
  fragment = glrCreateShader(program, GL_FRAGMENT_SHADER, OpenGLFragmentShader);
  OpenGLSurface::allocate();
  glrLinkProgram(program);

  mode7Program = glCreateProgram();
  mode7Vertex = glrCreateShader(mode7Program, GL_VERTEX_SHADER, OpenGLOutputVertexShader);
  mode7Fragment = glrCreateShader(mode7Program, GL_FRAGMENT_SHADER, OpenGLMode7FragmentShader);
  if(mode7Vertex && mode7Fragment) {
    glBindFragDataLocation(mode7Program, 0, "fragColor");
    glrLinkProgram(mode7Program);
  } else { mode7Program = 0; }

  mode7MapProgram = glCreateProgram();
  mode7MapVertex = glrCreateShader(mode7MapProgram, GL_VERTEX_SHADER, OpenGLMode7MapVertexShader);
  mode7MapFragment = glrCreateShader(mode7MapProgram, GL_FRAGMENT_SHADER, OpenGLMode7MapFragmentShader);
  if(mode7MapVertex && mode7MapFragment) {
    glBindFragDataLocation(mode7MapProgram, 0, "fragColor");
    glrLinkProgram(mode7MapProgram);
  } else { mode7MapProgram = 0; }

  setShader(shader);
  return initialized = true;
}

auto OpenGL::terminate() -> void {
  if(!initialized) return;
  setShader("");  //release shader resources (eg frame[] history)
  OpenGLSurface::release();
  if(mode7Fragment) { if(mode7Program) glDetachShader(mode7Program, mode7Fragment); glDeleteShader(mode7Fragment); mode7Fragment = 0; }
  if(mode7Vertex) { if(mode7Program) glDetachShader(mode7Program, mode7Vertex); glDeleteShader(mode7Vertex); mode7Vertex = 0; }
  if(mode7Program) { glDeleteProgram(mode7Program); mode7Program = 0; }
  if(mode7MapFragment) { if(mode7MapProgram) glDetachShader(mode7MapProgram, mode7MapFragment); glDeleteShader(mode7MapFragment); mode7MapFragment = 0; }
  if(mode7MapVertex) { if(mode7MapProgram) glDetachShader(mode7MapProgram, mode7MapVertex); glDeleteShader(mode7MapVertex); mode7MapVertex = 0; }
  if(mode7MapProgram) { glDeleteProgram(mode7MapProgram); mode7MapProgram = 0; }
  if(mode7MapFbo) { glDeleteFramebuffers(1, &mode7MapFbo); mode7MapFbo = 0; }
  if(mode7MapTex) { glDeleteTextures(1, &mode7MapTex); mode7MapTex = 0; }
  if(mode7VramTex) { glDeleteTextures(1, &mode7VramTex); mode7VramTex = 0; }
  if(mode7PaletteTex) { glDeleteTextures(1, &mode7PaletteTex); mode7PaletteTex = 0; }
  if(mode7LineTex) { glDeleteTextures(1, &mode7LineTex); mode7LineTex = 0; }
  if(mode7Tile0Tex) { glDeleteTextures(1, &mode7Tile0Tex); mode7Tile0Tex = 0; }
  if(mode7WindowTex) { glDeleteTextures(1, &mode7WindowTex); mode7WindowTex = 0; }
  mode7MapReady = false;
  mode7MapHash = 0;
  if(buffer) { delete[] buffer; buffer = nullptr; }
  initialized = false;
}
