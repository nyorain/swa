#pragma once

#include <swa/swa.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_surface;
struct wl_seat;
struct wl_surface;
struct wl_output;

struct swa_ext_wlr_layer {
	unsigned ext_type; // swa_ext
	struct swa_ext_struct* next;

	struct wl_output* output; // can be null
	const char* layer_namespace;
	unsigned anchors; // zwlr_layer_surface_v1_anchor
	unsigned keyboard_interactivity; // zwlr_layer_surface_v1_keyboard_interactivity
	unsigned layer; // zwlr_layer_shell_v1_layer
};

// Creates a wayland display implementation.
SWA_API struct swa_display* swa_display_wl_create(const char* appname);

// Creates a wayland display implementation for the already existent wayland
// display connection. Will never dispatch the display itself but always
// create a new, internal queue and use that for all internally created
// objects. TODO
// SWA_API struct swa_display* swa_display_wl_create_for_display(
// 	struct wl_display* dpy, const char* appname);

SWA_API bool swa_display_is_wl(struct swa_display* dpy);

SWA_API struct wl_display* swa_display_wl_get_display(struct swa_display* dpy);
SWA_API struct wl_seat* swa_display_wl_get_seat(struct swa_display* dpy);

SWA_API void swa_window_wl_lock_pointer(struct swa_window* win);
SWA_API void swa_window_wl_unlock_pointer(struct swa_window* win);

#ifdef __cplusplus
}
#endif
