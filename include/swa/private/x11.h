#pragma once

#include <swa/swa.h>
#include <swa/private/impl.h>
#include <swa/private/xkb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/present.h>

#ifdef __cplusplus
extern "C" {
#endif

// xlib/xcb forward declarations
typedef struct _XDisplay Display;
typedef struct xcb_cursor_context_t xcb_cursor_context_t;

struct swa_display_x11 {
	struct swa_display base;
	bool error;

	Display* display;
	xcb_connection_t* conn;
	xcb_ewmh_connection_t ewmh;
	xcb_screen_t* screen;
	enum swa_window_cap ewmh_caps;
	xcb_generic_event_t* next_event;

	xcb_window_t dummy_window;
	struct swa_window_x11* window_list;
	struct swa_window_x11* focus;

	unsigned n_cursors;
	struct swa_x11_cursor* cursors;
	struct swa_egl_display* egl;

	struct {
		unsigned x,y;
		struct swa_window_x11* over;
		uint64_t button_states; // bitset
		xcb_button_index_t button; // See win_begin_{move,resize}
	} mouse;

	struct {
		struct swa_xkb_context xkb;
		struct swa_window_x11* focus;
		uint64_t key_states[16]; // bitset
		bool repeated;
		int32_t device_id;
	} keyboard;

	// event op-codes
	struct {
		uint8_t xpresent;
		uint8_t xinput;
		uint8_t xkb;
		bool shm;
	} ext;

	struct {
		xcb_atom_t clipboard;
		xcb_atom_t targets;
		xcb_atom_t text;
		xcb_atom_t utf8_string;
		xcb_atom_t file_name;
		xcb_atom_t wm_delete_window;
		xcb_atom_t wm_change_state;
		xcb_atom_t motif_wm_hints;

		struct {
			xcb_atom_t text;
			xcb_atom_t utf8;
			xcb_atom_t uri_list;
			xcb_atom_t binary;
		} mime;

		struct {
			xcb_atom_t enter;
			xcb_atom_t position;
			xcb_atom_t status;
			xcb_atom_t type_list;
			xcb_atom_t action_copy;
			xcb_atom_t action_move;
			xcb_atom_t action_ask;
			xcb_atom_t action_link;
			xcb_atom_t drop;
			xcb_atom_t leave;
			xcb_atom_t finished;
			xcb_atom_t selection;
			xcb_atom_t proxy;
			xcb_atom_t aware;
		} xdnd;
	} atoms;
};

struct swa_x11_buffer_surface {
	void* bytes;
	uint64_t n_bytes;

	enum swa_image_format format;
	unsigned bytes_per_pixel;
	unsigned scanline_align; // in bytes
	xcb_gc_t gc;
	bool active;

 	// when using shm
	unsigned int shmid;
	uint32_t shmseg;
};

struct swa_x11_vk_surface {
	uintptr_t instance;
	uint64_t surface;
};

struct swa_x11_gl_surface {
	void* surface; // EGLSurface
	void* context; // EGLContext
};

struct swa_window_x11 {
	struct swa_window base;
	struct swa_display_x11* dpy;

	// linked list
	struct swa_window_x11* next;
	struct swa_window_x11* prev;

	xcb_window_t window;
	xcb_colormap_t colormap;
	xcb_visualtype_t* visualtype;
	xcb_cursor_t cursor;
	unsigned depth;
	bool client_decorated;
	bool init_size_pending;

	// only when using present extension:
	struct {
		// whether we asked the server to notify us on vsync
		bool pending;
		// whether a redraw was requested an postponed because a
		// present notify event was still pending
		bool redraw;
		xcb_present_event_t context;
		// the msc (counter) we want to get notified for redrawing
		uint64_t target_msc;
		uint32_t serial;
	} present;

	bool send_draw;
	bool send_resize;

	unsigned width;
	unsigned height;

	enum swa_surface_type surface_type;
	union {
		struct swa_x11_buffer_surface buffer;
		struct swa_x11_vk_surface vk;
		struct swa_x11_gl_surface gl;
	};
};

struct swa_display* swa_display_x11_create(const char* appname);

#ifdef __cplusplus
}
#endif
