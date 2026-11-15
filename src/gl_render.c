#include "renbackend.h"
#include "renwindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <SDL3/SDL_opengl.h>

/* Every GL function is loaded at runtime, so one binary can drive either GLES2 or
** desktop OpenGL 2.1 (compatibility) with nothing GL-related linked in. */
#define GL_FUNCS(X) \
  X(void, glBindTexture, (GLenum, GLuint)) \
  X(void, glBlendFunc, (GLenum, GLenum)) \
  X(void, glClear, (GLbitfield)) \
  X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
  X(void, glDeleteTextures, (GLsizei, const GLuint *)) \
  X(void, glDisable, (GLenum)) \
  X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
  X(void, glEnable, (GLenum)) \
  X(void, glFinish, (void)) \
  X(void, glGenTextures, (GLsizei, GLuint *)) \
  X(void, glGetIntegerv, (GLenum, GLint *)) \
  X(const GLubyte *, glGetString, (GLenum)) \
  X(void, glPixelStorei, (GLenum, GLint)) \
  X(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
  X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
  X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
  X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, glActiveTexture, (GLenum)) \
  X(void, glAttachShader, (GLuint, GLuint)) \
  X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) \
  X(void, glBindBuffer, (GLenum, GLuint)) \
  X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
  X(void, glCompileShader, (GLuint)) \
  X(GLuint, glCreateProgram, (void)) \
  X(GLuint, glCreateShader, (GLenum)) \
  X(void, glDeleteBuffers, (GLsizei, const GLuint *)) \
  X(void, glDeleteProgram, (GLuint)) \
  X(void, glDeleteShader, (GLuint)) \
  X(void, glEnableVertexAttribArray, (GLuint)) \
  X(void, glGenBuffers, (GLsizei, GLuint *)) \
  X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
  X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
  X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
  X(void, glLinkProgram, (GLuint)) \
  X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
  X(void, glUniform1i, (GLint, GLint)) \
  X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
  X(void, glUniform3fv, (GLint, GLsizei, const GLfloat *)) \
  X(void, glUseProgram, (GLuint)) \
  X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))

#define X(ret, name, args) static ret (APIENTRY *p_##name) args;
GL_FUNCS(X)
#undef X

static bool load_gl_functions(void) {
#define X(ret, name, args) if (!(*(void **) &p_##name = (void *) SDL_GL_GetProcAddress(#name))) return false;
  GL_FUNCS(X)
#undef X
  return true;
}

#define glBindTexture p_glBindTexture
#define glBlendFunc p_glBlendFunc
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glColorMask p_glColorMask
#define glDeleteTextures p_glDeleteTextures
#define glDisable p_glDisable
#define glDrawArrays p_glDrawArrays
#define glEnable p_glEnable
#define glFinish p_glFinish
#define glGenTextures p_glGenTextures
#define glGetIntegerv p_glGetIntegerv
#define glGetString p_glGetString
#define glPixelStorei p_glPixelStorei
#define glScissor p_glScissor
#define glTexImage2D p_glTexImage2D
#define glTexParameteri p_glTexParameteri
#define glTexSubImage2D p_glTexSubImage2D
#define glViewport p_glViewport
#define glActiveTexture p_glActiveTexture
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glCompileShader p_glCompileShader
#define glCreateProgram p_glCreateProgram
#define glCreateShader p_glCreateShader
#define glDeleteBuffers p_glDeleteBuffers
#define glDeleteProgram p_glDeleteProgram
#define glDeleteShader p_glDeleteShader
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glGenBuffers p_glGenBuffers
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glGetProgramiv p_glGetProgramiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glGetShaderiv p_glGetShaderiv
#define glGetUniformLocation p_glGetUniformLocation
#define glLinkProgram p_glLinkProgram
#define glShaderSource p_glShaderSource
#define glUniform1i p_glUniform1i
#define glUniform2f p_glUniform2f
#define glUniform3fv p_glUniform3fv
#define glUseProgram p_glUseProgram
#define glVertexAttribPointer p_glVertexAttribPointer

static bool use_es;

enum { LOC_POS, LOC_UV, LOC_COL };

typedef struct { float x, y, u, v; uint8_t r, g, b, a; } GLVert;

typedef struct {
  SDL_GLContext ctx;
  GLuint prog, vbo, white_tex;
  GLint u_size, u_sel;
  GLVert *verts;
  size_t count, cap;
  RenAtlas *atlas;
  RenRect clip;
  int w, h;
} GLRenderer;

static int max_texture_size = 0;

static const char *vs_src =
  "attribute vec2 aPos;\n"
  "attribute vec2 aUV;\n"
  "attribute vec4 aCol;\n"
  "uniform vec2 uSize;\n"
  "varying vec2 vUV;\n"
  "varying vec4 vCol;\n"
  "void main() {\n"
  "  gl_Position = vec4(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0, 0.0, 1.0);\n"
  "  vUV = aUV;\n"
  "  vCol = aCol;\n"
  "}\n";

static const char *fs_src =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D uTex;\n"
  "uniform vec3 uSel;\n"
  "varying vec2 vUV;\n"
  "varying vec4 vCol;\n"
  "void main() {\n"
  "  gl_FragColor = vec4(vCol.rgb, dot(texture2D(uTex, vUV).rgb, uSel) * vCol.a);\n"
  "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
  const char *sources[] = { use_es ? "" : "#version 120\n", src };
  GLuint s = glCreateShader(type);
  glShaderSource(s, 2, sources, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "gl shader error: %s\n", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static GLuint link_program(void) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
  if (!vs || !fs) return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glBindAttribLocation(p, LOC_POS, "aPos");
  glBindAttribLocation(p, LOC_UV, "aUV");
  glBindAttribLocation(p, LOC_COL, "aCol");
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(p, sizeof(log), NULL, log);
    fprintf(stderr, "gl link error: %s\n", log);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

#define PENDING_BYTES (32 * 1024)
#define ATLAS_TEXTURE_FORMAT(bpp) ((bpp) == 4 ? GL_RGBA : (bpp) == 3 ? GL_RGB : GL_LUMINANCE)
#define ATLAS_STRIDE(atlas) ((size_t) (atlas)->w * SDL_BYTESPERPIXEL((atlas)->format))

/* GLES2 leaves texture contents undefined without initial data, so a NULL pixels means zeroed */
static unsigned texture_create(int w, int h, int bpp, const void *pixels) {
  unsigned tex;
  void *zeros = NULL;
  if (!pixels) {
    zeros = SDL_calloc((size_t) w * bpp, h);
    if (!zeros) return 0;
    pixels = zeros;
  }
  GLenum fmt = ATLAS_TEXTURE_FORMAT(bpp);
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
  SDL_free(zeros);
  return tex;
}


typedef struct {
  GLuint tex;
  uint8_t *buf;
  int pending_y, pending_h;
} GLAtlas;

static int pending_cap(const RenAtlas *atlas) {
  return SDL_min(atlas->h, SDL_max(1, (int) (PENDING_BYTES / ATLAS_STRIDE(atlas))));
}

static void upload_pending(RenAtlas *atlas) {
  GLAtlas *a = atlas->surface;
  if (!a->pending_h) return;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, a->tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, a->pending_y, atlas->w, a->pending_h,
                  ATLAS_TEXTURE_FORMAT(SDL_BYTESPERPIXEL(atlas->format)), GL_UNSIGNED_BYTE, a->buf);
  SDL_memset(a->buf, 0, a->pending_h * ATLAS_STRIDE(atlas));
  a->pending_h = 0;
}

static void atlas_destroy(RenAtlas *atlas) {
  GLAtlas *a = atlas->surface;
  if (a->tex && SDL_GL_GetCurrentContext()) {
    glDeleteTextures(1, &a->tex);
  }
  SDL_free(a->buf);
  SDL_free(a);
}

static bool atlas_create(RenAtlas *atlas) {
  GLAtlas *a = SDL_calloc(1, sizeof *a);
  if (!a) return false;
  atlas->surface = a;
  a->buf = SDL_calloc(pending_cap(atlas), ATLAS_STRIDE(atlas));
  a->tex = texture_create(atlas->w, atlas->h, SDL_BYTESPERPIXEL(atlas->format), NULL);
  if (!a->buf || !a->tex) {
    atlas_destroy(atlas);
    return false;
  }
  return true;
}

static void atlas_upload(RenAtlas *atlas, int y, int rows, const uint8_t *src, int pitch, int row_bytes) {
  GLAtlas *a = atlas->surface;
  for (int line = 0; line < rows; line++) {
    if (a->pending_h == pending_cap(atlas)) upload_pending(atlas);
    /* glyph rows arrive in ascending y, so the pending rows are contiguous */
    if (!a->pending_h) a->pending_y = y + line;
    SDL_memcpy(a->buf + a->pending_h * ATLAS_STRIDE(atlas), src + (size_t) line * pitch, row_bytes);
    a->pending_h++;
  }
}

static int max_atlas_size(void) {
  return max_texture_size;
}

static void flush(GLRenderer *gl) {
  if (!gl->count) return;
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glBufferData(GL_ARRAY_BUFFER, gl->count * sizeof(GLVert), gl->verts, GL_STREAM_DRAW);
  glEnableVertexAttribArray(LOC_POS);
  glEnableVertexAttribArray(LOC_UV);
  glEnableVertexAttribArray(LOC_COL);
  glVertexAttribPointer(LOC_POS, 2, GL_FLOAT, GL_FALSE, sizeof(GLVert), (void*) offsetof(GLVert, x));
  glVertexAttribPointer(LOC_UV, 2, GL_FLOAT, GL_FALSE, sizeof(GLVert), (void*) offsetof(GLVert, u));
  glVertexAttribPointer(LOC_COL, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLVert), (void*) offsetof(GLVert, r));
  glScissor(gl->clip.x, gl->h - (gl->clip.y + gl->clip.height), gl->clip.width, gl->clip.height);
  GLuint tex = gl->white_tex;
  if (gl->atlas) {
    upload_pending(gl->atlas);
    GLAtlas *a = gl->atlas->surface;
    tex = a->tex;
  }
  glBindTexture(GL_TEXTURE_2D, tex);

  /* Same per-channel blend as the software path: each pass writes one channel
  ** using that channel's coverage as alpha. */
  static const float sel[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
  if (gl->atlas && gl->atlas->format == SDL_PIXELFORMAT_RGB24) {
    for (int ch = 0; ch < 3; ch++) {
      glColorMask(ch == 0, ch == 1, ch == 2, GL_FALSE);
      glUniform3fv(gl->u_sel, 1, sel[ch]);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei) gl->count);
    }
  } else {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glUniform3fv(gl->u_sel, 1, sel[0]);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei) gl->count);
  }
  gl->count = 0;
}

static void push_quad(GLRenderer *gl, RenAtlas *atlas, float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1, RenColor c) {
  if (atlas != gl->atlas) {
    flush(gl);
    gl->atlas = atlas;
  }
  if (gl->count + 6 > gl->cap) {
    gl->cap = gl->cap ? gl->cap * 2 : 6 * 1024;
    gl->verts = SDL_realloc(gl->verts, gl->cap * sizeof(GLVert));
    if (!gl->verts) { fprintf(stderr, "gl: out of memory\n"); exit(EXIT_FAILURE); }
  }
  GLVert q[6] = {
    { x0, y0, u0, v0, c.r, c.g, c.b, c.a }, { x1, y0, u1, v0, c.r, c.g, c.b, c.a }, { x0, y1, u0, v1, c.r, c.g, c.b, c.a },
    { x1, y0, u1, v0, c.r, c.g, c.b, c.a }, { x1, y1, u1, v1, c.r, c.g, c.b, c.a }, { x0, y1, u0, v1, c.r, c.g, c.b, c.a },
  };
  SDL_memcpy(&gl->verts[gl->count], q, sizeof(q));
  gl->count += 6;
}

static GLRenderer *create(SDL_Window *window, bool share) {
  if (share) SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
  if (!ctx) return NULL;
  if (!load_gl_functions()) {
    SDL_GL_DestroyContext(ctx);
    SDL_SetError("could not load OpenGL functions");
    return NULL;
  }
  SDL_GL_SetSwapInterval(SDL_getenv("LITE_GL_NOVSYNC") ? 0 : 1);

  GLRenderer *gl = SDL_calloc(1, sizeof(GLRenderer));
  gl->ctx = ctx;
  gl->prog = link_program();
  if (!gl->prog) {
    SDL_GL_DestroyContext(ctx);
    SDL_free(gl);
    SDL_SetError("GL shader setup failed");
    return NULL;
  }
  gl->u_size = glGetUniformLocation(gl->prog, "uSize");
  gl->u_sel = glGetUniformLocation(gl->prog, "uSel");
  glUseProgram(gl->prog);
  glUniform1i(glGetUniformLocation(gl->prog, "uTex"), 0);

  glGenBuffers(1, &gl->vbo);
  const uint8_t white[4] = { 255, 255, 255, 255 };
  gl->white_tex = texture_create(1, 1, 4, white);

  if (!max_texture_size) {
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  }
  fprintf(stderr, "gl: %s renderer active: %s | %s | %s | max texture %d\n",
          use_es ? "GLES2" : "OpenGL 2.1", (const char*) glGetString(GL_RENDERER), (const char*) glGetString(GL_VERSION),
          SDL_GetCurrentVideoDriver(), max_texture_size);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  return gl;
}

static GLRenderer *gl_of(RenWindow *ren) {
  return ren->backend_data;
}

static void set_context_attributes(bool es) {
  if (es) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  } else {
    /* compatibility 2.1 is also what macOS hands out when core is not requested */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  }
}

static bool probe(bool es) {
  set_context_attributes(es);
  bool ok = false;
  SDL_Window *window = SDL_CreateWindow("", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (window) {
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (ctx) SDL_GL_DestroyContext(ctx);
    ok = ctx != NULL;
    SDL_DestroyWindow(window);
  }
  /* drop the events of the throwaway window, nobody owns its id */
  SDL_PumpEvents();
  SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
  return ok;
}

/* tries both profiles on a throwaway window; the window itself cannot be retried
** because some platforms fix the pixel format when the window is created */
static bool available(SDL_PropertiesID props) {
  static int state;
  if (!state) {
    bool order[2];
    int count = 0;
    const char *list = SDL_GetStringProperty(props, RENBACKEND_PROP_GL_BACKEND, NULL);
    while (list && *list && count < 2) {
      const char *end = SDL_strchr(list, ',');
      size_t len = end ? (size_t) (end - list) : SDL_strlen(list);
      if (len == 4 && !SDL_strncmp(list, "gles", 4)) order[count++] = true;
      else if (len == 2 && !SDL_strncmp(list, "gl", 2)) order[count++] = false;
      list += len + (end != NULL);
    }
    if (!count) {
#if defined(_WIN32) || defined(__APPLE__)
      order[0] = false; order[1] = true;
#else
      order[0] = true; order[1] = false;
#endif
      count = 2;
    }
    state = -1;
    for (int i = 0; i < count && state < 0; i++) {
      if (probe(order[i])) {
        use_es = order[i];
        state = 1;
      }
    }
  }
  return state > 0;
}

static SDL_WindowFlags window_flags(void) {
  set_context_attributes(use_es);
  return SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
}

/* drawing is in native pixels, like the software path, so input needs the pixel/point ratio */
static void update_scale(RenWindow *ren) {
  int pw, ph, w, h;
  SDL_GetWindowSizeInPixels(ren->window, &pw, &ph);
  SDL_GetWindowSize(ren->window, &w, &h);
  ren->scale_x = (float) pw / w;
  ren->scale_y = (float) ph / h;
}

static bool init_window(RenWindow *ren) {
  RenWindow *share = NULL;
  RenWindow **list;
  size_t count = ren_get_window_list(&list);
  for (size_t i = 0; i < count && !share; i++) {
    if (list[i]->backend == &renbackend_gl) share = list[i];
  }
  if (share) SDL_GL_MakeCurrent(share->window, gl_of(share)->ctx);
  ren->backend_data = create(ren->window, share != NULL);
  update_scale(ren);
  return ren->backend_data != NULL;
}

static void free_window(RenWindow *ren) {
  GLRenderer *gl = gl_of(ren);
  SDL_GL_MakeCurrent(ren->window, gl->ctx);
  glDeleteTextures(1, &gl->white_tex);
  glDeleteBuffers(1, &gl->vbo);
  glDeleteProgram(gl->prog);
  SDL_GL_DestroyContext(gl->ctx);
  SDL_free(gl->verts);
  SDL_free(gl);
  ren->backend_data = NULL;
}

static void resize(RenWindow *ren) {
  update_scale(ren);
}

static void get_size(RenWindow *ren, int *w, int *h) {
  SDL_GetWindowSizeInPixels(ren->window, w, h);
}

static void begin_frame(RenWindow *ren) {
  GLRenderer *gl = gl_of(ren);
  SDL_GL_MakeCurrent(ren->window, gl->ctx);
  SDL_GetWindowSizeInPixels(ren->window, &gl->w, &gl->h);
  glViewport(0, 0, gl->w, gl->h);
  glUseProgram(gl->prog);
  glUniform2f(gl->u_size, (float) gl->w, (float) gl->h);
  glActiveTexture(GL_TEXTURE0);
  gl->count = 0;
  gl->atlas = NULL;
  gl->clip = (RenRect) { 0, 0, gl->w, gl->h };
  glScissor(0, 0, gl->w, gl->h);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}

static void end_frame(RenWindow *ren, UNUSED RenRect *rects, UNUSED int count) {
  GLRenderer *gl = gl_of(ren);
  flush(gl);
  /* with vsync off, wait for the GPU so frame timings include its work */
  if (SDL_getenv("LITE_GL_NOVSYNC")) glFinish();
  SDL_GL_SwapWindow(ren->window);
}

static void set_clip(RenWindow *ren, RenRect rect) {
  GLRenderer *gl = gl_of(ren);
  flush(gl);
  gl->clip = rect;
}

static RenSurface get_surface(RenWindow *ren) {
  return (RenSurface){ .surface = NULL, .scale = 1, .backend = ren->backend, .data = ren->backend_data };
}

static void draw_rect(RenSurface *rs, RenRect r, RenColor color) {
  GLRenderer *gl = rs->data;
  push_quad(gl, NULL, r.x, r.y, r.x + r.width, r.y + r.height, 0, 0, 0, 0, color);
}

static void draw_glyph(RenSurface *rs, const FontGlyph *g, int x, int y, RenColor color) {
  const float tw = g->atlas->w, th = g->atlas->h;
  push_quad(rs->data, g->atlas, x, y, x + g->w, y + g->h,
            0, g->sy / th, g->w / tw, (g->sy + g->h) / th, color);
}

const RenBackend renbackend_gl = {
  .name = "gl",
  .flags = BACKEND_FLAG_FULL_REDRAW,
  .available = available,
  .window_flags = window_flags,
  .init_window = init_window,
  .free_window = free_window,
  .resize = resize,
  .get_size = get_size,
  .begin_frame = begin_frame,
  .end_frame = end_frame,
  .set_clip = set_clip,
  .get_surface = get_surface,
  .draw_rect = draw_rect,
  .draw_glyph = draw_glyph,
  .atlas_create = atlas_create,
  .atlas_destroy = atlas_destroy,
  .atlas_upload = atlas_upload,
  .max_atlas_size = max_atlas_size,
};
