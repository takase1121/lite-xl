#include "renwindow.h"
#include "renbackend.h"

void renwin_init_command_buf(RenWindow *ren) {
  ren->command_buf = NULL;
  ren->command_buf_idx = 0;
  ren->command_buf_size = 0;
}

void renwin_show_window(RenWindow *ren) {
  SDL_ShowWindow(ren->window);
}

RenSurface renwin_get_surface(RenWindow *ren) {
  return ren->backend->get_surface(ren);
}

