/**
 * SVG Hooks based on nanovg.
 * This code is based on fcft's hook:
 * https://codeberg.org/dnkl/fcft/src/branch/master/svg-backend-nanosvg.c
 */

#include <float.h>
#include <ft2build.h>
#include <math.h>
#include <stdint.h>
#include FT_OTSVG_H

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION

#include "nanosvg.h"
#include "nanosvgrast.h"

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

typedef struct {
  NSVGimage* svg;
  float scale;
  unsigned short glyph_id_start;
  unsigned short glyph_id_end;
  float offset_x;
  float offset_y;
  FT_Error error;
} svg_rast_state;

static void slot_state_finalizer(void* obj) {
  FT_GlyphSlot slot = (FT_GlyphSlot)obj;
  svg_rast_state* state = (svg_rast_state*)slot->generic.data;

  free(state);
  slot->generic.data = NULL;
  slot->generic.finalizer = NULL;
}

static FT_Error svg_hook_init(FT_Pointer* state) {
  *state = NULL;
  return FT_Err_Ok;
}

static void svg_hook_free(FT_Pointer* state) {
  // does nothing
}

static FT_Error svg_hook_render(FT_GlyphSlot slot, FT_Pointer* _state) {
  svg_rast_state* state = (svg_rast_state*)slot->generic.data;
  if (state->error != FT_Err_Ok)
    return state->error;

  FT_Bitmap* bitmap = &slot->bitmap;
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  nsvgRasterize(rast, state->svg, state->offset_x * state->scale,
                state->offset_y * state->scale, state->scale, bitmap->buffer,
                bitmap->width, bitmap->rows, bitmap->pitch);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(state->svg);

  bitmap->pixel_mode = FT_PIXEL_MODE_BGRA;
  bitmap->num_grays = 256;
  slot->format = FT_GLYPH_FORMAT_BITMAP;

  // Nanosvg produces non-premultiplied RGBA, while FreeType expects
  // premultiplied BGRA
  for (size_t r = 0; r < bitmap->rows; r++) {
    for (size_t c = 0; c < bitmap->pitch; c += 4) {
      uint8_t* pixel = &bitmap->buffer[r * bitmap->pitch + c];
      uint8_t red = pixel[0];
      uint8_t green = pixel[1];
      uint8_t blue = pixel[2];
      uint8_t alpha = pixel[3];

      if (alpha == 0x00)
        blue = green = red = 0x00;
      else {
        blue = blue * alpha / 0xff;
        green = green * alpha / 0xff;
        red = red * alpha / 0xff;
      }

      pixel[0] = blue;
      pixel[1] = green;
      pixel[2] = red;
      pixel[3] = alpha;
    }
  }

  return FT_Err_Ok;
}

static FT_Error svg_hook_preset_slot(FT_GlyphSlot slot,
                                     FT_Bool cache,
                                     FT_Pointer* _state) {
  svg_rast_state* state = NULL;
  svg_rast_state dummy_state = {0};
  FT_SVG_Document document = (FT_SVG_Document)slot->other;
  FT_Size_Metrics metrics = document->metrics;

  if (cache) {
    if (!slot->generic.data) {
      slot->generic.data = calloc(1, sizeof(svg_rast_state));
      slot->generic.finalizer = &slot_state_finalizer;
    }
    state = (svg_rast_state*)slot->generic.data;
    state->error = FT_Err_Ok;
  } else {
    state = &dummy_state;
  }

  // The nanosvg rasterizer does not support rasterizing specific element IDs
  if (document->start_glyph_id != document->end_glyph_id) {
    state->error = FT_Err_Unimplemented_Feature;
    return state->error;
  }

  state->glyph_id_start = document->start_glyph_id;
  state->glyph_id_end = document->end_glyph_id;

  // create a string from the svg document for nanosvg
  char* svg_string = malloc(document->svg_document_length + 1);
  if (!svg_string) {
    state->error = FT_Err_Out_Of_Memory;
    return state->error;
  }
  memcpy(svg_string, document->svg_document, document->svg_document_length);
  svg_string[document->svg_document_length] = '\0';

  // parse the svg document
  state->svg = nsvgParse(svg_string, "px", 0.0);
  free(svg_string);
  if (!state->svg) {
    state->error = FT_Err_Invalid_SVG_Document;
    return state->error;
  }

  /*
   * Not sure if bug in nanosvg, but for images with negative
   * bounds, the image size (svg->width, svg->height) is
   * wrong. Workaround by figuring out the bounds ourselves, and
   * calculating the size from that.
   */
  float min_x = FLT_MAX;
  float min_y = FLT_MAX;
  float max_x = FLT_MIN;
  float max_y = FLT_MIN;

  for (const struct NSVGshape* shape = state->svg->shapes; shape != NULL;
       shape = shape->next) {
    min_x = min(min_x, shape->bounds[0]);
    min_y = min(min_y, shape->bounds[1]);
    max_x = max(max_x, shape->bounds[2]);
    max_y = max(max_y, shape->bounds[3]);
  }

  state->offset_x = -min_x;
  state->offset_y = -min_y;

  float svg_width = max_x - min_x;
  float svg_height = max_y - min_y;

  if (!svg_width || !svg_height) {
    svg_width = document->units_per_EM;
    svg_height = document->units_per_EM;
  }

  float x_scale = (float)metrics.x_ppem / floorf(svg_width);
  float y_scale = (float)metrics.y_ppem / floorf(svg_height);
  state->scale = x_scale < y_scale ? x_scale : y_scale;

  float width = floorf(svg_width) * state->scale;
  float height = floorf(svg_height) * state->scale;

  /*
   * We need to take into account any transformations applied.  The end
   * user who applied the transformation doesn't know the internal details
   * of the SVG document.  Thus, we expect that the end user should just
   * write the transformation as if the glyph is a traditional one.  We
   * then do some maths on this to get the equivalent transformation in
   * SVG coordinates.
   */
  // float xx = (float)document->transform.xx / (1 << 16);
  // float xy = -(float)document->transform.xy / (1 << 16);
  // float yx = -(float)document->transform.yx / (1 << 16);
  // float yy = (float)document->transform.yy / (1 << 16);

  // float x0 = (float)document->delta.x / 64 * svg_width / metrics.x_ppem;
  // float y0 = -(float)document->delta.y / 64 * svg_height / metrics.y_ppem;

  /*
   * User transformations
   *
   * Normally, we don’t set any in fcft. There’s one exception -
   * when FontConfig has added an FC_MATRIX pattern. This is
   * typically done when simulating italic fonts.
   *
   * Preferably, we’d like to error out here, and simply skip the
   * glyph. However, it seems FreeType ignores errors thrown from
   * this hook. This leads to a crash in the render hook, since
   * we’ve free:d the NSVG image.
   *
   * Therefore, we log a warning, and then *ignore* the
   * transform. For the normal use case, where the transform is
   * intended to simulate italics, it’s probably *better* to ignore
   * it, since most SVG glyphs are emojis, which doesn’t really look
   * good when slanted.
   */
  // if (xx != 1. || yy != 1. || xy != 0. || yx != 0. || x0 != 0. || y0 != 0.)
  // {}

  float ascender = slot->face->size->metrics.ascender / 64.;
  slot->bitmap.rows = ceilf(height);
  slot->bitmap.width = ceilf(width);
  slot->bitmap_left =
      min_x * state->scale + (metrics.x_ppem - (int)slot->bitmap.width) / 2;
  slot->bitmap_top = min_y != 0. ? -min_y * state->scale : ascender;
  slot->bitmap.width = ceilf(width);
  slot->bitmap.pitch = slot->bitmap.width * 4;
  slot->bitmap.pixel_mode = FT_PIXEL_MODE_BGRA;

  // Everything below is from rsvg reference hooks

  // Compute all the bearings and set them correctly. The outline is
  // scaled already, we just need to use the bounding box.
  float horiBearingX = 0.;
  float horiBearingY = -slot->bitmap_top;

  // XXX parentheses correct?
  float vertBearingX = slot->metrics.horiBearingX / 64.0f -
                       slot->metrics.horiAdvance / 64.0f / 2;
  float vertBearingY =
      (slot->metrics.vertAdvance / 64.0f - slot->metrics.height / 64.0f) / 2;

  // Do conversion in two steps to avoid 'bad function cast' warning.
  slot->metrics.width = roundf(width * 64);
  slot->metrics.height = roundf(height * 64);

  slot->metrics.horiBearingX = horiBearingX * 64; /* XXX rounding? */
  slot->metrics.horiBearingY = horiBearingY * 64;
  slot->metrics.vertBearingX = vertBearingX * 64;
  slot->metrics.vertBearingY = vertBearingY * 64;

  if (slot->metrics.vertAdvance == 0)
    slot->metrics.vertAdvance = height * 1.2f * 64;

  if (!cache)
    nsvgDelete(state->svg);

  return FT_Err_Ok;
}

SVG_RendererHooks nanosvg_hooks = {.init_svg = &svg_hook_init,
                                   .free_svg = &svg_hook_free,
                                   .render_svg = &svg_hook_render,
                                   .preset_slot = &svg_hook_preset_slot};