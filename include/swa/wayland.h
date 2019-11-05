#pragma once

#include <swa/impl.h>
#include <swa/egl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_wl {
	struct swa_display base;
	struct wl_display* display;
	struct wl_registry* registry;

	// globals
	struct wl_compositor* compositor;
	struct wl_shm* shm;
	struct wl_seat* seat;
	struct wl_keyboard* keyboard;
	struct wl_pointer* pointer;
	struct wl_touch* touch;
	struct wl_data_device_manager* dd_manager;
	struct wl_data_device* dd;
	struct xdg_wm_base* xdg_wm_base;
	struct zxdg_decoration_manager_v1* decoration_manager;

	struct wl_cursor_theme* cursor_theme;

	struct mainloop* mainloop;
	struct ml_io* display_io;
	struct ml_io* wakeup_io;
	struct ml_timer* keyboard_timer;
	bool error;

	int wakeup_pipe_w, wakeup_pipe_r;
	uint64_t key_states[64]; // bitset
	uint64_t mouse_button_states; // bitset
	int mouse_x, mouse_y;
	struct swa_window_wl* focus;
	struct swa_window_wl* mouse_over;
	uint32_t mouse_enter_serial;

	struct swa_window_wl* window_list;
	struct wl_surface* cursor_surface;
};

struct swa_wl_buffer {
	struct wl_buffer* buffer;
	uint32_t width, height;
	uint64_t size;
	bool busy;
	void* data;
};

struct swa_wl_buffer_surface {
	unsigned n_bufs;
	struct swa_wl_buffer* buffers;
};

struct swa_wl_gl_surface {
	EGLSurface egl_surface;
	EGLContext context;
	struct wl_egl_window* egl_window;
};

struct swa_wl_vk_surface {
	uint64_t instance;
	uint64_t surface;
};

struct swa_window_wl {
	struct swa_window base;
	struct swa_display_wl* dpy;

	// linked list of all windows in the display
	struct swa_window_wl* next;
	struct swa_window_wl* prev;

	struct wl_surface* surface;
	struct xdg_surface* xdg_surface;
	struct xdg_toplevel* xdg_toplevel;
	struct zxdg_toplevel_decoration_v1* decoration;
	struct wl_callback* frame_callback;
	// whether the window received at least one toplevel configure event
	// if this is true, the width and height are just the values this
	// window was constructed with
	bool configured;
	// whether this window was refreshed but couldn't handle it immediately.
	// when the next frame callback or the first configure event appear,
	// will send out a draw event
	bool redraw;
	unsigned width;
	unsigned height;
	uint32_t decoration_mode;
	enum swa_window_state state;
	struct ml_defer* defer_redraw;

	struct swa_wl_buffer cursor_buffer;
	int cursor_hx, cursor_hy;

	union {
		struct swa_wl_buffer_surface buffer;
		struct swa_wl_vk_surface vk;
		struct swa_wl_gl_surface gl;
	};
};

struct swa_data_listener_wl {
	swa_data_handler listener;
	const char* format;
	uint64_t n_bytes;
	const char* bytes;
};

struct swa_data_offer_wl {
	struct swa_data_offer base;
	struct swa_display_wl* dpy;
	struct wl_data_offer* offer;

	// formats and actions supported by other side
	unsigned n_formats;
	const char** formats;
	enum swa_data_action actions;

	unsigned n_listener;
	struct swa_data_listener_wl* listener;
};

struct swa_display* swa_display_wl_create(void);

#ifdef __cplusplus
}
#endif
