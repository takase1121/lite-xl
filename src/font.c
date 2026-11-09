#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_OUTLINE_H
#include FT_SYSTEM_H

#include "renderer.h"
#include "font.h"
#include "renbackend.h"

// uncomment the line below for more debugging information through printf
// #define RENDERER_DEBUG

static FT_Library library = NULL;

#define check_alloc(P) _check_alloc(P, __FILE__, __LINE__)
static void* _check_alloc(void *ptr, const char *const file, size_t ln) {
  if (!ptr) {
    fprintf(stderr, "%s:%zu: memory allocation failed\n", file, ln);
    exit(EXIT_FAILURE);
  }
  return ptr;
}

// getting freetype error messages (https://freetype.org/freetype2/docs/reference/ft2-error_enumerations.html)
static const char *const get_ft_error(FT_Error err) {
#undef FTERRORS_H_
#define FT_ERROR_START_LIST switch (FT_ERROR_BASE(err)) {
#define FT_ERRORDEF(e, v, s) case v: return s;
#define FT_ERROR_END_LIST }
#include FT_ERRORS_H
  return "unknown error";
}

/************************* Fonts *************************/

// approximate number of glyphs per atlas surface
#define GLYPHS_PER_ATLAS 96
// some padding to add to atlas surface to store more glyphs
#define FONT_HEIGHT_OVERFLOW_PX 0
#define FONT_WIDTH_OVERFLOW_PX 9

// maximum unicode codepoint supported (https://stackoverflow.com/a/52203901)
#define MAX_UNICODE 0x10FFFF
// number of rows and columns in the codepoint map
#define CHARMAP_ROW 128
#define CHARMAP_COL ((unsigned int)ceil((float)MAX_UNICODE / CHARMAP_ROW))

// the maximum number of glyphs for OpenType
#define MAX_GLYPHS 65535
// number of rows and columns in the glyph map
#define GLYPHMAP_ROW 128
#define GLYPHMAP_COL ((unsigned int)ceil((float)MAX_GLYPHS / GLYPHMAP_ROW))


// the bitmap format of the glyph
typedef enum {
  EGlyphFormatGrayscale, // 8bit graysclae
  EGlyphFormatSubpixel,  // 24bit subpixel
  EGlyphFormatSize
} ERenGlyphFormat;

// extra flags to store glyph info
typedef enum {
  EGlyphNone = 0,             // glyph is not loaded
  EGlyphXAdvance = (1 << 0L), // xadvance is loaded
  EGlyphBitmap = (1 << 1L)    // bitmap is loaded
} ERenGlyphFlags;

// metrics for a loaded glyph
typedef struct {
  float xadvance;
  unsigned short atlas_idx, surface_idx;
  int bitmap_left, bitmap_top;
  unsigned int x1, y0, y1;
  unsigned short flags;
  unsigned char format;
} GlyphMetric;

// maps codepoints -> glyph IDs
typedef struct {
  unsigned int *rows[CHARMAP_ROW];
} CharMap;

// a bitmap atlas with a fixed width, each surface acting as a bump allocator
typedef struct {
  RenAtlas **surfaces;
  unsigned int width, nsurface;
} GlyphAtlas;

// maps glyph IDs -> glyph metrics
typedef struct {
  // accessed with metrics[bitmap_idx][glyph_id / nrow][glyph_id - (row * ncol)]
  GlyphMetric *metrics[SUBPIXEL_BITMAPS_CACHED][GLYPHMAP_ROW];
  // accessed by atlas[glyph_format][atlas_idx].surfaces[surface_idx]
  GlyphAtlas *atlas[EGlyphFormatSize];
  size_t natlas[EGlyphFormatSize];
  size_t bytesize;
} GlyphMap;

typedef struct RenFont {
  FT_Face face;
  CharMap charmap;
  GlyphMap glyphs;
#ifdef LITE_USE_SDL_RENDERER
  int scale;
#endif
  float size, space_advance;
  unsigned short baseline, height, tab_size;
  unsigned short underline_thickness;
  ERenFontAntialiasing antialiasing;
  ERenFontHinting hinting;
  unsigned char style;
  char path[];
} RenFont;

#ifdef LITE_USE_SDL_RENDERER
void update_font_scale(RenWindow *window_renderer, RenFont **fonts) {
  if (window_renderer == NULL) return;
  const int surface_scale = renwin_get_surface(window_renderer).scale;
  for (int i = 0; i < FONT_FALLBACK_MAX && fonts[i]; ++i) {
    if (fonts[i]->scale != surface_scale) {
      ren_font_group_set_size(fonts, fonts[0]->size, surface_scale);
      return;
    }
  }
}
#endif

const char* utf8_to_codepoint(const char *p, const char *endp, unsigned *dst) {
  const unsigned char *up = (unsigned char*)p;
  unsigned res, n;
  switch (*p & 0xf0) {
    case 0xf0 :  res = *up & 0x07;  n = 3;  break;
    case 0xe0 :  res = *up & 0x0f;  n = 2;  break;
    case 0xd0 :
    case 0xc0 :  res = *up & 0x1f;  n = 1;  break;
    default   :  res = *up;         n = 0;  break;
  }
  while (up < (const unsigned char *)endp && n--) {
    res = (res << 6) | (*(++up) & 0x3f);
  }
  *dst = res;
  return (const char*)up + 1;
}

static int font_set_load_options(RenFont* font) {
  int load_target = font->antialiasing == FONT_ANTIALIASING_NONE ? FT_LOAD_TARGET_MONO
    : (font->hinting == FONT_HINTING_SLIGHT ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_NORMAL);
  int hinting = font->hinting == FONT_HINTING_NONE ? FT_LOAD_NO_HINTING : FT_LOAD_FORCE_AUTOHINT;
  return load_target | hinting;
}

static int font_set_render_options(RenFont* font) {
  if (font->antialiasing == FONT_ANTIALIASING_NONE)
    return FT_RENDER_MODE_MONO;
  if (font->antialiasing == FONT_ANTIALIASING_SUBPIXEL) {
    switch (font->hinting) {
      case FONT_HINTING_NONE: FT_Library_SetLcdFilter(library, FT_LCD_FILTER_NONE); break;
      case FONT_HINTING_SLIGHT: FT_Library_SetLcdFilter(library, FT_LCD_FILTER_LIGHT); break;
      case FONT_HINTING_FULL: FT_Library_SetLcdFilter(library, FT_LCD_FILTER_DEFAULT); break;
    }
    return FT_RENDER_MODE_LCD;
  } else {
    switch (font->hinting) {
      case FONT_HINTING_NONE:   return FT_RENDER_MODE_NORMAL; break;
      case FONT_HINTING_SLIGHT: return FT_RENDER_MODE_LIGHT; break;
      case FONT_HINTING_FULL:   return FT_RENDER_MODE_LIGHT; break;
    }
  }
  return 0;
}

static int font_set_style(FT_Outline* outline, int x_translation, unsigned char style) {
  FT_Outline_Translate(outline, x_translation, 0 );
  if (style & FONT_STYLE_SMOOTH)
    FT_Outline_Embolden(outline, 1 << 5);
  if (style & FONT_STYLE_BOLD)
    FT_Outline_EmboldenXY(outline, 1 << 5, 0);
  if (style & FONT_STYLE_ITALIC) {
    FT_Matrix matrix = { 1 << 16, 1 << 14, 0, 1 << 16 };
    FT_Outline_Transform(outline, &matrix);
  }
  return 0;
}

static unsigned int font_get_glyph_id(RenFont *font, unsigned int codepoint) {
  if (codepoint > MAX_UNICODE) return 0;
  size_t row = codepoint / CHARMAP_COL;
  size_t col = codepoint - (row * CHARMAP_COL);
  if (!font->charmap.rows[row]) font->charmap.rows[row] = check_alloc(SDL_calloc(sizeof(unsigned int), CHARMAP_COL));
  if (font->charmap.rows[row][col] == 0) {
    unsigned int glyph_id = FT_Get_Char_Index(font->face, codepoint);
    // use -1 as a sentinel value for "glyph not available", a bit risky, but OpenType
    // uses uint16 to store glyph IDs. In theory this cannot ever be reached
    font->charmap.rows[row][col] = glyph_id ? glyph_id : (unsigned int) -1;
  }
  return font->charmap.rows[row][col] == (unsigned int) -1 ? 0 : font->charmap.rows[row][col];
}

#define FONT_IS_SUBPIXEL(F) ((F)->antialiasing == FONT_ANTIALIASING_SUBPIXEL)
#define FONT_BITMAP_COUNT(F) ((F)->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? SUBPIXEL_BITMAPS_CACHED : 1)
#define SLOT_BITMAP_TYPE(B) ((B).pixel_mode == FT_PIXEL_MODE_LCD ? EGlyphFormatSubpixel : EGlyphFormatGrayscale)

static inline SDL_PixelFormat glyphformat_to_pixelformat(ERenGlyphFormat format, int *depth) {
  switch (format) {
    case EGlyphFormatSubpixel:  *depth = 24; return SDL_PIXELFORMAT_RGB24;
    case EGlyphFormatGrayscale: *depth = 8;  return SDL_PIXELFORMAT_INDEX8;
    default: return SDL_PIXELFORMAT_UNKNOWN;
  }
}

static RenAtlas *font_allocate_glyph_surface(RenFont *font, FT_GlyphSlot slot, int bitmap_idx, GlyphMetric *metric) {
  // get an atlas with the correct width
  ERenGlyphFormat glyph_format = SLOT_BITMAP_TYPE(slot->bitmap);
  int atlas_idx = -1;
  for (int i = 0; i < font->glyphs.natlas[glyph_format]; i++) {
    if (font->glyphs.atlas[glyph_format][i].width >= metric->x1) {
      atlas_idx = i;
      break;
    }
  }
  if (atlas_idx < 0) {
    font->glyphs.atlas[glyph_format] = check_alloc(
      SDL_realloc(font->glyphs.atlas[glyph_format], sizeof(GlyphAtlas) * (font->glyphs.natlas[glyph_format] + 1))
    );
    font->glyphs.atlas[glyph_format][font->glyphs.natlas[glyph_format]] = (GlyphAtlas) {
      .width = metric->x1 + FONT_WIDTH_OVERFLOW_PX, .nsurface = 0,
      .surfaces = NULL,
    };
    font->glyphs.bytesize += sizeof(GlyphAtlas);
    atlas_idx = font->glyphs.natlas[glyph_format]++;
  }
  metric->atlas_idx = atlas_idx;
  GlyphAtlas *atlas = &font->glyphs.atlas[glyph_format][atlas_idx];

  // find the surface with the minimum height that can fit the glyph (limited to last 100 surfaces)
  int surface_idx = -1, max_surface_idx = (int) atlas->nsurface - 100, min_waste = INT_MAX;
  for (int i = atlas->nsurface - 1; i >= 0 && i > max_surface_idx; i--) {
    GlyphMetric *m = atlas->surfaces[i]->userdata;
    int new_min_waste = (int) atlas->surfaces[i]->h - (m ? (int) m->y1 : 0);
    if (new_min_waste >= metric->y1 && new_min_waste < min_waste) {
      surface_idx = i;
      min_waste = new_min_waste;
    }
  }
  if (surface_idx < 0) {
    // allocate a new surface array, and a surface
    int h = FONT_HEIGHT_OVERFLOW_PX + (double) font->face->size->metrics.height / 64.0f;
    if (h <= FONT_HEIGHT_OVERFLOW_PX) h += slot->bitmap.rows;
    if (h <= FONT_HEIGHT_OVERFLOW_PX) h += font->size;
    int depth = 0;
    SDL_PixelFormat format = glyphformat_to_pixelformat(glyph_format, &depth);
    atlas->surfaces = check_alloc(SDL_realloc(atlas->surfaces, sizeof(RenAtlas *) * (atlas->nsurface + 1)));
    // GL textures cannot exceed GL_MAX_TEXTURE_SIZE
    int glyphs = GLYPHS_PER_ATLAS, max_tex = ren_max_atlas_size();
    if (max_tex && glyphs * h > max_tex) glyphs = max_tex / h > 0 ? max_tex / h : 1;
    atlas->surfaces[atlas->nsurface] = check_alloc(ren_atlas_create(atlas->width, glyphs * h, format));
    surface_idx = atlas->nsurface++;
    font->glyphs.bytesize += (sizeof(RenAtlas *) + sizeof(RenAtlas) + atlas->width * glyphs * h * glyph_format);
  }
  metric->surface_idx = surface_idx;
  RenAtlas *surface = atlas->surfaces[surface_idx];
  if (surface->userdata) {
    GlyphMetric *last_metric = surface->userdata;
    metric->y0 = last_metric->y1; metric->y1 += last_metric->y1;
  }
  surface->userdata = metric;
  return surface;
}

static GlyphMetric *font_load_glyph_metric(RenFont *font, unsigned int glyph_id, unsigned int bitmap_idx) {
  unsigned int load_option = font_set_load_options(font);
  int row = glyph_id / GLYPHMAP_COL, col = glyph_id - (row * GLYPHMAP_COL);
  int bitmaps = FONT_BITMAP_COUNT(font);

  // we set all 3 subpixel bitmaps at once, so if either of them are missing we should load it with freetype
  if (!font->glyphs.metrics[0][row] || !(font->glyphs.metrics[0][row][col].flags & EGlyphXAdvance)) {
    // load the font without hinting to fix an issue with monospaced fonts,
    // because freetype doesn't report the correct LSB and RSB delta. Transformation & subpixel positioning don't affect
    // the xadvance, so we can save some time by not doing this step multiple times
    if (FT_Load_Glyph(font->face, glyph_id, (load_option | FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_HINTING) & ~FT_LOAD_FORCE_AUTOHINT) != 0)
      return NULL;
    for (int i = 0; i < bitmaps; i++) {
      // save the metrics for all subpixel indexes
      if (!font->glyphs.metrics[i][row]) {
        font->glyphs.metrics[i][row] = check_alloc(SDL_calloc(sizeof(GlyphMetric), GLYPHMAP_COL));
        font->glyphs.bytesize += sizeof(GlyphMetric) * GLYPHMAP_COL;
      }
      GlyphMetric *metric = &font->glyphs.metrics[i][row][col];
      metric->flags |= EGlyphXAdvance;
      metric->xadvance = font->face->glyph->advance.x / 64.0f;
    }
  }
  return &font->glyphs.metrics[bitmap_idx][row][col];
}

static RenAtlas *font_load_glyph_bitmap(RenFont *font, unsigned int glyph_id, unsigned int bitmap_idx) {
  GlyphMetric *metric = font_load_glyph_metric(font, glyph_id, bitmap_idx);
  if (!metric) return NULL;
  if (metric->flags & EGlyphBitmap) return font->glyphs.atlas[metric->format][metric->atlas_idx].surfaces[metric->surface_idx];

  // render the glyph for a bitmap_idx
  unsigned int load_option = font_set_load_options(font), render_option = font_set_render_options(font);
  FT_GlyphSlot slot = font->face->glyph;
  if (FT_Load_Glyph(font->face, glyph_id, load_option | FT_LOAD_BITMAP_METRICS_ONLY) != 0
      || font_set_style(&slot->outline, bitmap_idx * (64 / SUBPIXEL_BITMAPS_CACHED), font->style) != 0
      || FT_Render_Glyph(slot, render_option) != 0)
    return NULL;

  // if this bitmap is empty, or has a format we don't support, just store the xadvance
  if (!slot->bitmap.width || !slot->bitmap.rows || !slot->bitmap.buffer ||
      (slot->bitmap.pixel_mode != FT_PIXEL_MODE_MONO
        && slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY
        && slot->bitmap.pixel_mode != FT_PIXEL_MODE_LCD))
    return NULL;

  unsigned int glyph_width = slot->bitmap.width / FONT_BITMAP_COUNT(font);
  // FT_PIXEL_MODE_MONO uses 1 bit per pixel packed bitmap
  if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) glyph_width *= 8;

  metric->x1 = glyph_width;
  metric->y1 = slot->bitmap.rows;
  metric->bitmap_left = slot->bitmap_left;
  metric->bitmap_top = slot->bitmap_top;
  metric->flags |= EGlyphBitmap;
  metric->format = SLOT_BITMAP_TYPE(slot->bitmap);

  // find the best surface to copy the glyph over, and copy it
  RenAtlas *surface = font_allocate_glyph_surface(font, slot, bitmap_idx, metric);
  const uint8_t *src = slot->bitmap.buffer;
  int pitch = slot->bitmap.pitch;
  uint8_t *expanded = NULL;
  if (font->antialiasing == FONT_ANTIALIASING_NONE) {
    // expand the 1 bit per pixel bitmap to one byte per pixel
    expanded = check_alloc(SDL_malloc((size_t) slot->bitmap.width * slot->bitmap.rows));
    for (unsigned int line = 0; line < slot->bitmap.rows; ++line) {
      for (unsigned int column = 0; column < slot->bitmap.width; ++column) {
        int source_pixel = slot->bitmap.buffer[line * slot->bitmap.pitch + (column / 8)];
        expanded[line * slot->bitmap.width + column] = ((source_pixel >> (7 - (column % 8))) & 0x1) * 0xFF;
      }
    }
    src = expanded;
    pitch = slot->bitmap.width;
  }
  ren_atlas_upload(surface, metric->y0, slot->bitmap.rows, src, pitch, slot->bitmap.width);
  SDL_free(expanded);
  return surface;
}

static RenFont *font_group_get_glyph(RenFont **fonts, unsigned int codepoint, int subpixel_idx, RenAtlas **surface, GlyphMetric **metric) {
  if (subpixel_idx < 0) subpixel_idx += SUBPIXEL_BITMAPS_CACHED;
  RenFont *font = NULL;
  unsigned int glyph_id = 0;
  for (int i = 0; i < FONT_FALLBACK_MAX && fonts[i]; i++) {
    font = fonts[i]; glyph_id = font_get_glyph_id(fonts[i], codepoint);
    // use the first font that has representation for the glyph ID, but for whitespaces always use the first font
    if (glyph_id || is_whitespace(codepoint)) break;
  }
  // load the glyph if it is not loaded
  subpixel_idx = FONT_IS_SUBPIXEL(font) ? subpixel_idx : 0;
  GlyphMetric *m = font_load_glyph_metric(font, glyph_id, subpixel_idx);
  // try the box drawing character (0x25A1) if the requested codepoint is not a whitespace, and we cannot load the .notdef glyph
  if ((!m || !m->flags) && codepoint != 0x25A1 && !is_whitespace(codepoint))
    return font_group_get_glyph(fonts, 0x25A1, subpixel_idx, surface, metric);
  if (metric && m) *metric = m;
  if (surface && m) *surface = font_load_glyph_bitmap(font, glyph_id, subpixel_idx);
  return font;
}

static void font_clear_glyph_cache(RenFont* font) {
  for (int glyph_format_idx = 0; glyph_format_idx < EGlyphFormatSize; glyph_format_idx++) {
    for (int atlas_idx = 0; atlas_idx < font->glyphs.natlas[glyph_format_idx]; atlas_idx++) {
      GlyphAtlas *atlas = &font->glyphs.atlas[glyph_format_idx][atlas_idx];
      for (int surface_idx = 0; surface_idx < atlas->nsurface; surface_idx++) {
        ren_atlas_destroy(atlas->surfaces[surface_idx]);
      }
      SDL_free(atlas->surfaces);
    }
    SDL_free(font->glyphs.atlas[glyph_format_idx]);
    font->glyphs.atlas[glyph_format_idx] = NULL;
    font->glyphs.natlas[glyph_format_idx] = 0;
  }
  // clear glyph metric
  for (int subpixel_idx = 0; subpixel_idx < FONT_BITMAP_COUNT(font); subpixel_idx++) {
    for (int glyphmap_row = 0; glyphmap_row < GLYPHMAP_ROW; glyphmap_row++) {
      SDL_free(font->glyphs.metrics[subpixel_idx][glyphmap_row]);
      font->glyphs.metrics[subpixel_idx][glyphmap_row] = NULL;
    }
  }
  font->glyphs.bytesize = 0;
}

// based on https://github.com/libsdl-org/SDL_ttf/blob/2a094959055fba09f7deed6e1ffeb986188982ae/SDL_ttf.c#L1735
static unsigned long font_file_read(FT_Stream stream, unsigned long offset, unsigned char *buffer, unsigned long count) {
  uint64_t amount;
  SDL_IOStream *file = (SDL_IOStream *) stream->descriptor.pointer;
  SDL_SeekIO(file, (int) offset, SDL_IO_SEEK_SET);
  if (count == 0)
    return 0;
  amount = SDL_ReadIO(file, buffer, sizeof(char) * count);
  if (amount <= 0)
    return 0;
  return (unsigned long) amount;
}

static void font_file_close(FT_Stream stream) {
  if (stream && stream->descriptor.pointer)
    SDL_CloseIO((SDL_IOStream *) stream->descriptor.pointer);
  SDL_free(stream);
}

static int font_set_face_metrics(RenFont *font, FT_Face face) {
  FT_Error err;
  float pixel_size = font->size;
  #ifdef LITE_USE_SDL_RENDERER
  pixel_size *= font->scale;
  #endif
  if ((err = FT_Set_Pixel_Sizes(face, 0, (int) pixel_size)) != 0)
    return err;

  font->face = face;
  if(FT_IS_SCALABLE(face)) {
    font->height = (short)((face->height / (float)face->units_per_EM) * font->size);
    font->baseline = (short)((face->ascender / (float)face->units_per_EM) * font->size);
    font->underline_thickness = (unsigned short)((face->underline_thickness / (float)face->units_per_EM) * font->size);
  } else {
    font->height = (short) font->face->size->metrics.height / 64.0f;
    font->baseline = (short) font->face->size->metrics.ascender / 64.0f;
  }
  if(!font->underline_thickness)
    font->underline_thickness = ceil((double) font->height / 14.0);

  if ((err = FT_Load_Char(face, ' ', (font_set_load_options(font) | FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_HINTING) & ~FT_LOAD_FORCE_AUTOHINT)) != 0)
    return err;
  font->space_advance = face->glyph->advance.x / 64.0f;
  return 0;
}

RenFont* ren_font_load(const char* path, float size, ERenFontAntialiasing antialiasing, ERenFontHinting hinting, unsigned char style) {
  FT_Error err = FT_Err_Ok;
  SDL_IOStream *file = NULL; RenFont *font = NULL;
  FT_Face face = NULL; FT_Stream stream = NULL;

  file = SDL_IOFromFile(path, "rb");
  if (!file) return NULL; // error set by SDL_IOFromFile
  
  int len = strlen(path);
  font = check_alloc(SDL_calloc(1, sizeof(RenFont) + len + 1));
  strcpy(font->path, path);
  font->size = size;
  font->antialiasing = antialiasing;
  font->hinting = hinting;
  font->style = style;
  font->tab_size = 2;
#ifdef LITE_USE_SDL_RENDERER
  font->scale = 1;
#endif

  stream = check_alloc(SDL_calloc(1, sizeof(FT_StreamRec)));
  if (!stream) goto stream_failure;
  stream->read = &font_file_read;
  stream->close = &font_file_close;
  stream->descriptor.pointer = file;
  stream->pos = 0;
  stream->size = (unsigned long) SDL_GetIOSize(file);

  if ((err = FT_Open_Face(library, &(FT_Open_Args) { .flags = FT_OPEN_STREAM, .stream = stream }, 0, &face)) != 0)
    goto failure;
  if ((err = font_set_face_metrics(font, face)) != 0)
    goto failure;
  return font;

stream_failure:
  if (file) SDL_CloseIO(file);
failure:
  if (err != FT_Err_Ok) SDL_SetError("%s", get_ft_error(err));
  if (face) FT_Done_Face(face);
  if (font) SDL_free(font);
  return NULL;
}

RenFont* ren_font_copy(RenFont* font, float size, ERenFontAntialiasing antialiasing, ERenFontHinting hinting, int style) {
  antialiasing = antialiasing == -1 ? font->antialiasing : antialiasing;
  hinting = hinting == -1 ? font->hinting : hinting;
  style = style == -1 ? font->style : style;

  return ren_font_load(font->path, size, antialiasing, hinting, style); // SDL_SetError() will be called appropriately
}

const char* ren_font_get_path(RenFont *font) {
  return font->path;
}

void ren_font_free(RenFont* font) {
  font_clear_glyph_cache(font);
  // free codepoint cache as well
  for (int i = 0; i < CHARMAP_ROW; i++) {
    SDL_free(font->charmap.rows[i]);
  }
  FT_Done_Face(font->face);
  SDL_free(font);
}

void ren_font_group_set_tab_size(RenFont **fonts, int n) {
  for (int j = 0; j < FONT_FALLBACK_MAX && fonts[j]; ++j) {
    fonts[j]->tab_size = n;
  }
}

int ren_font_group_get_tab_size(RenFont **fonts) {
  return fonts[0]->tab_size;
}

float ren_font_group_get_size(RenFont **fonts) {
  return fonts[0]->size;
}

void ren_font_group_set_size(RenFont **fonts, float size, int surface_scale) {
  for (int i = 0; i < FONT_FALLBACK_MAX && fonts[i]; ++i) {
    font_clear_glyph_cache(fonts[i]);
    fonts[i]->size = size;
    fonts[i]->tab_size = 2;
    #ifdef LITE_USE_SDL_RENDERER
    fonts[i]->scale = surface_scale;
    #endif
    font_set_face_metrics(fonts[i], fonts[i]->face);
  }
}

int ren_font_group_get_height(RenFont **fonts) {
  return fonts[0]->height;
}

// some fonts provide xadvance for whitespaces (e.g. Unifont), which we need to ignore
float font_get_xadvance(RenFont *font, unsigned int codepoint, float xadvance, double curr_x, RenTab tab) {
  if (!is_whitespace(codepoint) && xadvance) {
    return xadvance;
  }
  if (codepoint != '\t') {
    return font->space_advance;
  }
  float tab_size = font->space_advance * font->tab_size;
  if (isnan(tab.offset)) {
    return tab_size;
  }
  double offset = fmodl(curr_x + tab.offset, tab_size);
  float adv = tab_size - offset;
  // If there is not enough space until the next stop, skip it
  if (adv < font->space_advance) {
    adv += tab_size;
  }
  return adv;
}

double ren_font_group_get_width(RenFont **fonts, const char *text, size_t len, RenTab tab, int *x_offset) {
  double width = 0;
  const char* end = text + len;

  bool set_x_offset = x_offset == NULL;
  while (text < end) {
    unsigned int codepoint;
    text = utf8_to_codepoint(text, end, &codepoint);
    GlyphMetric *metric = NULL;
    font_group_get_glyph(fonts, codepoint, 0, NULL, &metric);
    width += font_get_xadvance(fonts[0], codepoint, metric ? metric->xadvance : 0, width, tab);
    if (!set_x_offset && metric) {
      set_x_offset = true;
      *x_offset = metric->bitmap_left; // TODO: should this be scaled by the surface scale?
    }
  }
  if (!set_x_offset)
    *x_offset = 0;
#ifdef LITE_USE_SDL_RENDERER
  return width / fonts[0]->scale;
#else
  return width;
#endif
}

#ifdef RENDERER_DEBUG
// this function can be used to debug font atlases, it is not public
void ren_font_dump(RenFont *font) {
  char filename[1024];
  for (int glyph_format_idx = 0; glyph_format_idx < EGlyphFormatSize; glyph_format_idx++) {
    for (int atlas_idx = 0; atlas_idx < font->glyphs.natlas[glyph_format_idx]; atlas_idx++) {
      GlyphAtlas *atlas = &font->glyphs.atlas[glyph_format_idx][atlas_idx];
      for (int surface_idx = 0; surface_idx < atlas->nsurface; surface_idx++) {
        snprintf(filename, 1024, "%s-%d-%d-%d.bmp", font->face->family_name, glyph_format_idx, atlas_idx, surface_idx);
        if (atlas->surfaces[surface_idx]->backend == &renbackend_soft) SDL_SaveBMP(atlas->surfaces[surface_idx]->surface, filename);
      }
    }
  }
  fprintf(stderr, "%s: %zu bytes\n", font->face->family_name, font->glyphs.bytesize);
}
#endif

bool font_group_lookup(RenFont **fonts, unsigned int codepoint, int subpixel_idx, FontGlyph *out) {
  RenAtlas *atlas = NULL; GlyphMetric *metric = NULL;
  RenFont *font = font_group_get_glyph(fonts, codepoint, subpixel_idx, &atlas, &metric);
  if (!metric)
    return false;
  *out = (FontGlyph) {
    .font = font, .atlas = atlas,
    .sy = metric->y0, .w = metric->x1, .h = metric->y1 - metric->y0,
    .left = metric->bitmap_left, .top = metric->bitmap_top,
    .lcd = metric->format == EGlyphFormatSubpixel,
    .xadvance = metric->xadvance,
    .space_advance = font->space_advance,
    .font_height = font->height, .underline_thickness = font->underline_thickness,
  };
  return true;
}

int font_group_get_baseline(RenFont **fonts) {
  return fonts[0]->baseline;
}

unsigned char font_group_get_style(RenFont **fonts) {
  return fonts[0]->style;
}

int font_init(void) {
  FT_Error err;
  if ((err = FT_Init_FreeType(&library)) != 0)
    return SDL_SetError("%s", get_ft_error(err));
  return 0;
}

void font_free(void) {
  FT_Done_FreeType(library);
}
