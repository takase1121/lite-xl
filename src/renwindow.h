#include <SDL3/SDL.h>
#include "renderer.h"

struct RenWindow {
  SDL_Window *window;
  uint8_t *command_buf;
  size_t command_buf_idx;
  size_t command_buf_size;
  float scale_x;
  float scale_y;
  const struct RenBackend *backend;
  void *backend_data;
#ifdef LITE_USE_SDL_RENDERER
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  RenSurface rensurface;
#endif
};
typedef struct RenWindow RenWindow;

void renwin_init_command_buf(RenWindow *ren);
void renwin_show_window(RenWindow *ren);
RenSurface renwin_get_surface(RenWindow *ren);

