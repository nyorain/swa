#pragma once

#include <swa/swa.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_surface;

// Creates a wayland display implementation.
SWA_API struct swa_display* swa_display_wl_create(const char* appname);

// Creates a wayland display implementation for the already existent wayland
// display connection. Will never dispatch the display itself but always
// create a new, internal queue and use that for all internally created
// objects.
// SWA_API struct swa_display* swa_display_wl_create_for_display(
// 	struct wl_display* dpy, const char* appname);

SWA_API bool swa_display_is_wl(struct swa_display* dpy);

#ifdef __cplusplus
}
#endif
