#pragma once

#include <swa/private/impl.h>
#include <swa/private/xkb.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_wl {
	struct swa_display base;
	struct wl_display* display;
	struct wl_registry* registry;
	struct pml_io* io_source;

	// globals
	struct wl_compositor* compositor;
	struct wl_shm* shm;
	struct wl_seat* seat;
	struct wl_data_device_manager* data_dev_manager;
	struct xdg_wm_base* xdg_wm_base;
	struct zxdg_decoration_manager_v1* decoration_manager;

	struct wl_keyboard* keyboard;
	struct wl_pointer* pointer;
	struct wl_touch* touch;
	struct wl_data_device* data_dev;

	struct pml* pml;
	bool error;
	bool ready;

	struct swa_xkb_context xkb;

	const char* appname;
	int wakeup_pipe_w, wakeup_pipe_r;
	struct pml_io* wakeup_io;
	uint64_t key_states[16]; // bitset
	uint64_t mouse_button_states; // bitset
	int mouse_x, mouse_y;
	struct swa_window_wl* focus;
	struct swa_window_wl* mouse_over;
	uint32_t mouse_enter_serial; // needed for cursor settings
	uint32_t last_serial; // needed to start a dnd/resize/move session.

	struct {
		struct pml_timer* timer;
		struct timespec set;
		struct wl_cursor_theme* theme;
		struct wl_surface* surface;
		struct wl_callback* frame_callback;
		struct wl_cursor* active;
		bool redraw;
	} cursor;

	struct {
		struct pml_timer* timer;
		uint32_t key;
		uint32_t serial;
		int32_t rate; // in repeats per second
		int32_t delay; // in ms
	} key_repeat;

	struct {
		struct swa_data_offer_wl* offer;
		uint32_t serial;
	} dnd;

	// We have to keep track of touch points to know on which
	// window the touch points are located. That information is only
	// sent from wayland when a new touch point is created.
	unsigned capacity_touch_points;
	unsigned n_touch_points;
	struct swa_wl_touch_point* touch_points;

	// dummy custom queue that can be used to e.g. force initial
	// listeners to be triggered (by moving the proxy to this queue
	// and roundtripping) without dispatching normal events.
	struct wl_event_queue* wl_queue;

	struct swa_data_offer_wl* data_offer_list;
	struct swa_data_offer_wl* selection;

	struct swa_egl_display* egl;
};

struct swa_wl_touch_point {
	struct swa_window_wl* window;
	int32_t id;
	int x, y;
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
	struct swa_wl_buffer* buffers; // list of all buffers
	int active; // index of active
};

struct swa_wl_gl_surface {
	void* surface;
	void* context;
	struct wl_egl_window* egl_window;
};

struct swa_wl_vk_surface {
	uintptr_t instance;
	uint64_t surface;
	swa_proc destroy_surface_pfn;
};

struct swa_window_wl {
	struct swa_window base;
	struct swa_display_wl* dpy;

	struct wl_surface* wl_surface;
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
	struct pml_defer* defer_redraw;

	struct {
		// if this is != NULL, this window has a native cursor that
		// can be animated, that will be used.
		struct wl_cursor* native;
		// Otherwise, will use the wl_buffer in this buffer.
		// If that is NULL, the window has no cursor.
		struct swa_wl_buffer buffer;
		// Cursor hotspot only relevant when using the buffer.
		int hx, hy;
	} cursor;

	enum swa_surface_type surface_type;
	union {
		struct swa_wl_buffer_surface buffer;
		struct swa_wl_vk_surface vk;
		struct swa_wl_gl_surface gl;
	};
};

struct swa_data_offer_wl {
	struct swa_data_offer base;
	struct swa_display_wl* dpy;
	struct wl_data_offer* offer;

	// they form a linked list
	struct swa_data_offer_wl* next;
	struct swa_data_offer_wl* prev;

	// formats and actions supported by other side
	unsigned n_formats;
	const char** formats;
	bool dnd;
	enum swa_data_action supported_actions;
	enum swa_data_action action;

	struct {
		const char* format;
		int fd;
		swa_data_handler handler;
		uint64_t n_bytes;
		char* bytes;
		struct pml_io* io;
	} data;
};

// public api
struct swa_display* swa_display_wl_create(const char* appname);

#ifdef __cplusplus
}
#endif
