#pragma once

#include <swa/swa.h>
#include <swa/impl.h>

#ifdef __cplusplus
extern "C" {
#endif

// xlib/xcb forward declarations
typedef struct _XDisplay Display;
typedef struct xcb_connection_t xcb_connection_t;
typedef struct xcb_visualtype_t xcb_visualtype_t;
typedef struct xcb_screen_t xcb_screen_t;
typedef uint32_t xcb_window_t;
typedef uint32_t xcb_atom_t;
typedef uint32_t xcb_timestamp_t;
typedef uint32_t xcb_cursor_t;
typedef uint32_t xcb_pixmap_t;

struct swa_display_x11 {
    struct swa_display base;
    bool error;

	Display* display;
	xcb_connection_t* connection;
};

struct swa_window_x11 {
    struct swa_window base;
    struct swa_display_x11* dpy;

	xcb_window_t window;
	xcb_visualtype_t* visualtype;
};

struct swa_display* swa_display_x11_create(void);

#ifdef __cplusplus
}
#endif
