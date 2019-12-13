#pragma once

#include "props.h"
#include <swa/impl.h>
#include <swa/xkb.h>
#include <stdint.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <pml.h>

struct drm_vk_surface;

#ifdef __cplusplus
extern "C" {
#endif

struct pointer {
	struct pointer* next;
	struct pointer* prev;

	struct libinput_device* dev;
};

struct keyboard {
	struct keyboard* next;
	struct keyboard* prev;

	struct libinput_device* dev;
};

struct touch {
	struct touch* prev;
	struct touch* next;

	struct libinput_device* dev;
};

struct drm_display {
	struct swa_display base;
	struct pml* pml;

	struct {
		bool vtset;
		bool active;
		int sigusrfd;
		struct pml_io* sigusrio;
		int tty_fd;
		int saved_kb_mode;
	} session;

	struct {
		int fd;
		// bool has_fb_mods;
		struct pml_io* io;
		drmModeResPtr res;
		unsigned n_planes;
		drmModePlanePtr* planes;
		unsigned n_outputs;
		struct drm_output* outputs;
	} drm;

	struct udev* udev;
	struct udev_monitor* udev_monitor;
	struct pml_io* udev_io;

	struct {
		struct libinput* context;
		struct pml_io* io;

		struct {
			bool present;
			struct drm_window* focus;
			struct xkb_keymap* keymap;
			struct xkb_state* state;
		} keyboard;

		struct {
			bool present;
			uint32_t x;
			uint32_t y;
			struct drm_window* over;
		} pointer;

		struct {
			bool present;
		} touch;
	} input;
};

struct drm_output {
	struct drm_window* window; // optional

	uint32_t connector_id;
	uint32_t crtc_id;
	uint32_t primary_plane_id;

	drmModeModeInfo mode;
	uint32_t mode_id;
	bool needs_modeset;

	struct {
		union drm_crtc_props crtc;
		union drm_plane_props plane;
		union drm_connector_props connector;
	} props;
};

// Dumb buffers always have linear format mod and drm XRGB8888 format
struct drm_dumb_buffer {
	void* data;
	bool in_use;
	uint32_t stride;
	uint32_t fb_id;
	uint64_t size;
	uint32_t gem_handle;
};

struct drm_buffer_surface {
	struct drm_dumb_buffer buffers[3];
	struct drm_dumb_buffer* active;

	// a buffer we submitted for pageflip but the pageflip hasn't
	// completed yet
	struct drm_dumb_buffer* pending;
	// the currently active buffer, i.e. the last one for which the pageflip
	// has completed
	struct drm_dumb_buffer* last;
};

struct drm_window {
	struct swa_window base;
	struct drm_display* dpy;
	struct pml_defer* defer_draw;

	bool redraw;
	struct drm_output* output; // optional

	enum swa_surface_type surface_type;
	union {
		struct drm_buffer_surface buffer;
		struct drm_vk_surface* vk;
	};
};

struct swa_display* drm_display_create(const char* appname);

#ifdef __cplusplus
}
#endif
