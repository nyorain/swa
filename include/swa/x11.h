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

// During a callback done from dispatching, this will return the currently
// processed event (might always still be null though). The returned pointer
// points to a xcb_generic_event_t that is only guaranteed to be valid until
// the current callback returns.
// Must only be called on an x11 display.
SWA_API const void* swa_display_x11_current_event(struct swa_display* dpy);

#ifdef __cplusplus
}
#endif
