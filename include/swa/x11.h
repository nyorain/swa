#pragma once

#include <swa/swa.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xcb_connection_t xcb_connection_t;

// Creates an x11 display implementation.
SWA_API struct swa_display* swa_display_x11_create(const char* appname);
SWA_API xcb_connection_t* swa_display_x11_connection(struct swa_display* dpy);

SWA_API bool swa_display_is_x11(struct swa_display* dpy);
SWA_API uint32_t swa_window_x11_cursor(struct swa_window* win);

#ifdef __cplusplus
}
#endif
