#pragma once

// TODO: full input support
//  - support for touch
//  - support for keyboard key repeat (see wayland)
// TODO: support for animated cursors (see wayland)
// TODO: cursor plane support for vulkan
// TODO: add extra compile time flag for gl support in drm backend?
//   something like SWA_WITH_GBM?
// TODO: multi output support
//   track on which output the cursor currently sits
//   send focus and mouse cross events
//   probably makes sense to couple focus and mouse_over state
// TODO: support other session types (e.g. logind)
// TODO: for direct session, use fork and ipc (see wlroots)?
//  so the program doesn't have to run as root.
//  Might be a bad idea to fork like this in a library though...
//  Low prio, we should rather focus on logind as best solution
// TODO: check for DRM_CAP_DUMB_BUFFER, otherwise don't return
//  buffer surface cap
// TODO: make dependency on drm stuff optional?
//  research whether they all of them are available everywhere or a mesa thing.
//  In that case we could at least offer a vkdisplay backend

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

typedef void* EGLSurface;
typedef void* EGLContext;

struct drm_display {
	struct swa_display base;
	struct pml* pml;

	int wakeup_pipe_w, wakeup_pipe_r;
	struct pml_io* wakeup_io;
	bool quit;

	struct {
		bool vtset;
		bool active;
		int sigusrfd;
		struct pml_io* sigusrio;
		int sigtermfd;
		struct pml_io* sigtermio;
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
			enum swa_keyboard_mod mods;
			uint64_t key_states[16]; // bitset
		} keyboard;

		struct {
			bool present;
			double x;
			double y;
			struct drm_window* over;
			uint64_t button_states; // bitset
		} pointer;

		struct {
			bool present;
		} touch;
	} input;

	struct swa_xcursor_theme* cursor_theme;

	struct gbm_device* gbm_device;
	struct swa_egl_display* egl;
};

struct drm_output {
	struct drm_window* window; // only set when there is a window for output

	drmModeModeInfo mode;
	uint32_t mode_id;
	bool needs_modeset;

	struct {
		uint32_t id;
		union drm_crtc_props props;
	} crtc;

	struct {
		uint32_t id;
		union drm_connector_props props;
	} connector;

	struct {
		uint32_t id;
		union drm_plane_props props;
	} primary_plane;

	struct {
		uint32_t id;
		union drm_plane_props props;
	} cursor_plane;
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

struct drm_gl_surface {
	EGLSurface surface;
	EGLContext context;
	struct gbm_surface* gbm_surface;

	// The currently shown buffer.
	// We have to track it since we unlock it (i.e. make it available for
	// rendering again) when page flipping completes.
	struct gbm_bo* front;

	// The buffer that was rendered (and queued for pageflip) last.
	struct gbm_bo* pending;
};

struct drm_buffer_cursor {
	unsigned width;
	unsigned height;
	int hx;
	int hy;
	struct drm_dumb_buffer buffer;
	bool update;
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
		struct drm_gl_surface gl;
	};

	union {
		struct drm_buffer_cursor buffer;
	} cursor;
};

struct swa_display* drm_display_create(void);

#ifdef __cplusplus
}
#endif
