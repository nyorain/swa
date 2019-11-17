#pragma once

#include <swa/swa.h>
#include <swa/impl.h>
#include <swa/xkb.h>
#include <xcb/xcb_ewmh.h>

#ifdef __cplusplus
extern "C" {
#endif

// xlib/xcb forward declarations
typedef struct _XDisplay Display;

struct swa_display_x11 {
    struct swa_display base;
    bool error;

	Display* display;
	xcb_connection_t* conn;
	xcb_ewmh_connection_t ewmh;
	xcb_screen_t* screen;
	enum swa_window_cap ewmh_caps;
	xcb_generic_event_t* next_event;
	struct swa_xkb_context xkb;

	struct swa_window_x11* window_list;

	struct {
		int xpresent;
		int xinput;
		bool shm;
	} ext;

	struct {
		xcb_atom_t clipboard;
		xcb_atom_t targets;
		xcb_atom_t text;
		xcb_atom_t utf8_string;
		xcb_atom_t file_name;
		xcb_atom_t wm_delete_window;
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
	xcb_gc_t gc;
	bool active;

 	// when using shm
	unsigned int shmid;
	uint32_t shmseg;
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
	unsigned depth;

	unsigned width;
	unsigned height;

	enum swa_surface_type surface_type;
	union {
		struct swa_x11_buffer_surface buffer;
	};
};

struct swa_display* swa_display_x11_create(void);

#ifdef __cplusplus
}
#endif
