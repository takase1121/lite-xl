#include "renbackend.h"
#include "renwindow.h"

#if defined(LITE_USE_GL) && !defined(LITE_USE_SDL_RENDERER)

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <SDL3/SDL_opengles2.h>

enum { LOC_POS, LOC_UV, LOC_COL };

typedef struct { float x, y, u, v; uint8_t r, g, b, a; } GLVert;

typedef struct {
  SDL_Window *window;
  SDL_GLContext ctx;
  GLuint prog, vbo, white_tex;
  GLint u_size, u_sel;
  GLVert *verts;
  size_t count, cap;
  GLuint tex;
  bool lcd;
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
  "precision mediump float;\n"
  "uniform sampler2D uTex;\n"
  "uniform vec3 uSel;\n"
  "varying vec2 vUV;\n"
  "varying vec4 vCol;\n"
  "void main() {\n"
  "  gl_FragColor = vec4(vCol.rgb, dot(texture2D(uTex, vUV).rgb, uSel) * vCol.a);\n"
  "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
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

static GLenum texture_format(int bpp) {
  return bpp == 4 ? GL_RGBA : bpp == 3 ? GL_RGB : GL_LUMINANCE;
}

/* GLES2 leaves texture contents undefined without initial data, so a NULL pixels means zeroed */
static unsigned texture_create(int w, int h, int bpp, const void *pixels) {
  unsigned tex;
  void *zeros = NULL;
  if (!pixels) {
    zeros = SDL_calloc((size_t) w * bpp, h);
    if (!zeros) return 0;
    pixels = zeros;
  }
  GLenum fmt = texture_format(bpp);
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

static unsigned atlas_texture(SDL_Surface *surface) {
  return (unsigned) SDL_GetNumberProperty(SDL_GetSurfaceProperties(surface), "gl_tex", 0);
}

/* the atlas pixels live only in the texture: new glyph rows go straight from the FreeType buffer */
static void atlas_upload(SDL_Surface *surface, int y, int rows, const uint8_t *src, int pitch, int row_bytes) {
  const int bpp = SDL_BYTESPERPIXEL(surface->format);
  unsigned tex = atlas_texture(surface);
  if (!tex) {
    tex = texture_create(surface->w, surface->h, bpp, NULL);
    SDL_SetNumberProperty(SDL_GetSurfaceProperties(surface), "gl_tex", tex);
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  /* GLES2 has no GL_UNPACK_ROW_LENGTH, so padded rows must be repacked. */
  uint8_t *packed = NULL;
  if (pitch != row_bytes) {
    packed = SDL_malloc((size_t) row_bytes * rows);
    if (!packed) return;
    for (int line = 0; line < rows; line++) {
      SDL_memcpy(packed + (size_t) line * row_bytes, src + (size_t) line * pitch, row_bytes);
    }
    src = packed;
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, row_bytes / bpp, rows, texture_format(bpp), GL_UNSIGNED_BYTE, src);
  SDL_free(packed);
}

static void atlas_destroyed(SDL_Surface *surface) {
  unsigned tex = (unsigned) SDL_GetNumberProperty(SDL_GetSurfaceProperties(surface), "gl_tex", 0);
  if (tex && SDL_GL_GetCurrentContext()) {
    glDeleteTextures(1, &tex);
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
  glBindTexture(GL_TEXTURE_2D, gl->tex);

  /* Same per-channel blend as the software path: each pass writes one channel
  ** using that channel's coverage as alpha. */
  static const float sel[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
  if (gl->lcd) {
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

static void push_quad(GLRenderer *gl, GLuint tex, bool lcd, float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1, RenColor c) {
  if (tex != gl->tex || lcd != gl->lcd) {
    flush(gl);
    gl->tex = tex;
    gl->lcd = lcd;
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

static GLRenderer *create(SDL_Window *window, GLRenderer *share) {
  if (share) {
    SDL_GL_MakeCurrent(share->window, share->ctx);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
  }
  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
  if (!ctx) return NULL;
  SDL_GL_SetSwapInterval(SDL_getenv("LITE_GL_NOVSYNC") ? 0 : 1);

  GLRenderer *gl = SDL_calloc(1, sizeof(GLRenderer));
  gl->window = window;
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
  fprintf(stderr, "gl: GLES2 renderer active: %s | %s | %s | max texture %d\n",
          (const char*) glGetString(GL_RENDERER), (const char*) glGetString(GL_VERSION),
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

static bool available(void) {
  return true;
}

static SDL_WindowFlags window_flags(void) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
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
  GLRenderer *share = NULL;
  RenWindow **list;
  size_t count = ren_get_window_list(&list);
  for (size_t i = 0; i < count && !share; i++) {
    if (list[i]->backend == &renbackend_gl) share = gl_of(list[i]);
  }
  ren->backend_data = create(ren->window, share);
  update_scale(ren);
  return ren->backend_data != NULL;
}

static void free_window(RenWindow *ren) {
  GLRenderer *gl = gl_of(ren);
  SDL_GL_MakeCurrent(gl->window, gl->ctx);
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
  SDL_GL_MakeCurrent(gl->window, gl->ctx);
  SDL_GetWindowSizeInPixels(gl->window, &gl->w, &gl->h);
  glViewport(0, 0, gl->w, gl->h);
  glUseProgram(gl->prog);
  glUniform2f(gl->u_size, (float) gl->w, (float) gl->h);
  glActiveTexture(GL_TEXTURE0);
  gl->count = 0;
  gl->tex = gl->white_tex;
  gl->lcd = false;
  gl->clip = (RenRect) { 0, 0, gl->w, gl->h };
  glScissor(0, 0, gl->w, gl->h);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}

static void present(RenWindow *ren, UNUSED RenRect *rects, UNUSED int count) {
  GLRenderer *gl = gl_of(ren);
  flush(gl);
  /* with vsync off, wait for the GPU so frame timings include its work */
  if (SDL_getenv("LITE_GL_NOVSYNC")) glFinish();
  SDL_GL_SwapWindow(gl->window);
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
  push_quad(gl, gl->white_tex, false, r.x, r.y, r.x + r.width, r.y + r.height, 0, 0, 0, 0, color);
}

static void draw_glyph(RenSurface *rs, const RenGlyph *g) {
  const float tw = g->atlas->w, th = g->atlas->h;
  push_quad(rs->data, atlas_texture(g->atlas), g->lcd, g->dx, g->dy, g->dx + g->w, g->dy + g->h,
            0, g->sy / th, g->w / tw, (g->sy + g->h) / th, g->color);
}

const RenBackend renbackend_gl = {
  .name = "gl",
  .full_redraw = true,
  .available = available,
  .window_flags = window_flags,
  .init_window = init_window,
  .free_window = free_window,
  .resize = resize,
  .get_size = get_size,
  .begin_frame = begin_frame,
  .present = present,
  .set_clip = set_clip,
  .get_surface = get_surface,
  .draw_rect = draw_rect,
  .draw_glyph = draw_glyph,
  .stores_atlas_pixels = false,
  .atlas_upload = atlas_upload,
  .atlas_destroyed = atlas_destroyed,
  .max_atlas_size = max_atlas_size,
};

#else

static bool unavailable(void) { return false; }

const RenBackend renbackend_gl = { .name = "gl", .available = unavailable };

#endif
