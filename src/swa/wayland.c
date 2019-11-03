#include <swa/wayland.h>
#include <dlg/dlg.h>
#include <mainloop.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-client-protocol.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;
static const struct swa_data_offer_interface data_offer_impl;

static const struct xdg_toplevel_listener toplevel_listener;
static const struct xdg_surface_listener xdg_surface_listener;

static struct swa_display_wl* get_display_wl(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_wl*) base;
}

void display_destroy(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(dpy->dd) wl_data_device_destroy(dpy->dd);
	if(dpy->shm) wl_shm_destroy(dpy->shm);
	if(dpy->keyboard) wl_keyboard_destroy(dpy->keyboard);
	if(dpy->pointer) wl_pointer_destroy(dpy->pointer);
	if(dpy->touch) wl_touch_destroy(dpy->touch);
	if(dpy->xdg_wm_base) xdg_wm_base_destroy(dpy->xdg_wm_base);
	if(dpy->seat) wl_seat_destroy(dpy->seat);
	if(dpy->dd_manager) wl_data_device_manager_destroy(dpy->dd_manager);
	if(dpy->compositor) wl_compositor_destroy(dpy->compositor);
	if(dpy->registry) wl_registry_destroy(dpy->registry);
	if(dpy->display) wl_display_disconnect(dpy->display);
	free(dpy);
}

bool display_poll_events(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	mainloop_iterate(dpy->mainloop, false);
	return dpy->error;
}

bool display_wait_events(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	mainloop_iterate(dpy->mainloop, true);
	return dpy->error;
}

void display_wakeup(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	int err = write(dpy->wakeup_pipe_r, " ", 1);

	// if the pipe is full, the waiting thread will wake up and clear
	// it and it doesn't matter that our write call failed
	if(err < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		dlg_warn("Writing to wakeup pipe failed: %s", strerror(errno));
	}
}

enum swa_display_cap display_capabilities(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	enum swa_display_cap caps =
		swa_display_cap_gl |
		swa_display_cap_vk |
		swa_display_cap_buffer_surface |
		swa_display_cap_client_decoration;
	if(dpy->keyboard) caps |= swa_display_cap_keyboard;
	if(dpy->pointer) caps |= swa_display_cap_mouse;
	if(dpy->touch) caps |= swa_display_cap_touch;
	if(dpy->dd) caps |= swa_display_cap_dnd | swa_display_cap_clipboard;
	if(dpy->decoration_manager) caps |= swa_display_cap_server_decoration;
	return caps;
}

const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
	static const char* names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
	};
	*count = sizeof(names) / sizeof(names[0]);
	return names;
};

bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	struct swa_display_wl* dpy = get_display_wl(base);
	unsigned n_bits = 8 * sizeof(dpy->key_states);
	if(key >= n_bits) {
		dlg_warn("keycode not tracked (too high)");
		return false;
	}

	unsigned idx = key / 64;
	unsigned bit = key % 64;
	return (dpy->key_states[idx] & (1 << bit));
}

const char* display_key_name(struct swa_display*, enum swa_key);
enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display*);
struct swa_window* display_get_keyboard_focus(struct swa_display*);
bool display_mouse_button_pressed(struct swa_display*, enum swa_mouse_button);
void display_mouse_position(struct swa_display*, int* x, int* y);
struct swa_window* display_get_mouse_over(struct swa_display*);
struct swa_data_offer* display_get_clipboard(struct swa_display*);
bool display_set_clipboard(struct swa_display*, struct swa_data_source*, void*);
bool display_start_dnd(struct swa_display*, struct swa_data_source*, void*);

struct swa_window* display_create_window(struct swa_display* base,
		struct swa_window_settings settings) {
	struct swa_display_wl* dpy = get_display_wl(base);
	struct swa_window_wl* win = calloc(1, sizeof(*win));
	win->base.impl = &window_impl;
	win->dpy = dpy;
	win->surface = wl_compositor_create_surface(dpy->compositor);

	win->xdg_surface = xdg_wm_base_get_xdg_surface(dpy->xdg_wm_base, win->surface);
	xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
	win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
	xdg_toplevel_add_listener(win->xdg_toplevel, &toplevel_listener, win);

	if(settings.title) {
		xdg_toplevel_set_title(win->xdg_toplevel, settings.title);
	}
	if(settings.app_name) {
		xdg_toplevel_set_app_id(win->xdg_toplevel, settings.app_name);
	}

	// commit the role so we get a configure event and can start drawing
	wl_surface_commit(win->surface);

	// the default of the decoration protocol (and wayland in general)
	// is client side decorations. We only have to act if the user
	// explicitly requested server side decorations
	if(settings.client_decorate == swa_preference_no) {
		if(dpy->decoration_manager) {
			win->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
				dpy->decoration_manager, win->xdg_toplevel);
			zxdg_toplevel_decoration_v1_add_listener(win->decoration,
				&decoration_listener, win);
			zxdg_toplevel_decoration_v1_set_mode(xdgDecoration(),
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		} else {
			dlg_info("Can't set up server side decoration since "
				"the compositor doesn't support the decoration protocol");
		}
	}

	if(settings.state != swa_window_state_normal) {
		swa_window_set_state(&win->base, settings.state);
	}

	return &win->base;
}

static const struct swa_display_interface display_impl = {
	.destroy = display_destroy,
	.poll_events = display_poll_events,
	.wait_events = display_wait_events,
	.wakeup = display_wakeup,
	.capabilities = display_capabilities,
	.vk_extensions = display_vk_extensions,
	.key_pressed = display_key_pressed,
	.key_name = display_key_name,
	.active_keyboard_mods = display_active_keyboard_mods,
	.get_keyboard_focus = display_get_keyboard_focus,
	.mouse_button_pressed = display_mouse_button_pressed,
	.mouse_position = display_mouse_position,
	.get_mouse_over = display_get_mouse_over,
	.get_clipboard = display_get_clipboard,
	.set_clipboard = display_set_clipboard,
	.start_dnd = display_start_dnd,
	.create_window = display_create_window,
};

static void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		  int32_t width, int32_t height, struct wl_array *states) {
}

static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
		  uint32_t serial) {

}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure
};


static char* last_wl_log = NULL;
static void log_handler(const char* format, va_list vlist) {
	va_list vlistcopy;
	va_copy(vlistcopy, vlist);

	unsigned size = vsnprintf(NULL, 0, format, vlist);
	va_end(vlist);

	last_wl_log = realloc(last_wl_log, size + 1);
	vsnprintf(last_wl_log, size + 1, format, vlistcopy);
	last_wl_log[size - 1] = '\0'; // replace newline
	va_end(vlistcopy);

	dlg_info("wayland log: %s", last_wl_log);
}

static void touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial,
		 uint32_t time, struct wl_surface *surface, int32_t id,
		 wl_fixed_t x, wl_fixed_t y) {
}

static void touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial,
		uint32_t time, int32_t id) {
}

static void touch_motion(void *data, struct wl_touch *wl_touch, uint32_t time,
		   int32_t id, wl_fixed_t x, wl_fixed_t y) {
}

static void touch_frame(void *data, struct wl_touch *wl_touch) {
}

static void touch_cancel(void *data, struct wl_touch *wl_touch) {
}

static void touch_shape(void *data, struct wl_touch *wl_touch, int32_t id,
		  wl_fixed_t major, wl_fixed_t minor) {
}

static void touch_orientation(void *data, struct wl_touch *wl_touch,
		int32_t id, wl_fixed_t orientation) {
}

static const struct wl_touch_listener touch_listener = {
	.down = touch_down,
	.up = touch_up,
	.motion = touch_motion,
	.frame = touch_frame,
	.cancel = touch_cancel,
	.shape = touch_shape,
	.orientation = touch_orientation,
};


static void pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
		wl_fixed_t sy) {
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
}

static void pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
}

static void pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
}

static void pointer_frame(void *data, struct wl_pointer *wl_pointer) {
}

static void pointer_axis_source(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis_source) {
}

static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis) {
}

static void pointer_axis_discrete(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void keyboard_keymap_cb(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	printf("keymap\n");
}

static void keyboard_enter_cb(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	printf("enter\n");
}

static void keyboard_leave_cb(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
}

static void keyboard_key_cb(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
}

static void keyboard_modifiers_cb(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
}

static void keyboard_repeat_info_cb(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap_cb,
	.enter = keyboard_enter_cb,
	.leave = keyboard_leave_cb,
	.key = keyboard_key_cb,
	.modifiers = keyboard_modifiers_cb,
	.repeat_info = keyboard_repeat_info_cb,
};

static void seat_caps_cb(void* data, struct wl_seat* seat, uint32_t caps) {
	struct swa_display_wl* dpy = data;
	if((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !dpy->keyboard) {
		dpy->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(dpy->keyboard, &keyboard_listener, dpy);
	} else if(!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && dpy->keyboard) {
		dlg_info("lost wl_keyboard");
		wl_keyboard_destroy(dpy->keyboard);
		dpy->keyboard = NULL;
	}

	if((caps & WL_SEAT_CAPABILITY_POINTER) && !dpy->pointer) {
		dpy->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(dpy->pointer, &pointer_listener, dpy);
	} else if(!(caps & WL_SEAT_CAPABILITY_POINTER) && dpy->pointer) {
		dlg_info("lost wl_pointer");
		wl_pointer_destroy(dpy->pointer);
		dpy->pointer = NULL;
	}

	if((caps & WL_SEAT_CAPABILITY_TOUCH) && !dpy->touch) {
		dpy->touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(dpy->touch, &touch_listener, dpy);
	} else if(!(caps & WL_SEAT_CAPABILITY_TOUCH) && dpy->touch) {
		dlg_info("lost wl_touch");
		wl_touch_destroy(dpy->touch);
		dpy->touch = NULL;
	}
}

static void seat_name_cb(void* data, struct wl_seat* seat, const char* name) {
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps_cb,
	.name = seat_name_cb,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	(void) version;
	struct swa_display_wl* dpy = data;
	if(strcmp(interface, wl_compositor_interface.name) == 0) {
		dpy->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	} else if(strcmp(interface, wl_shm_interface.name) == 0) {
		dpy->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if(strcmp(interface, wl_seat_interface.name) == 0) {
		dpy->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
		wl_seat_add_listener(dpy->seat, &seat_listener, dpy);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// TODO: handle
	// not really sure what to do when something like compositor or shm
	// is gone though. I guess this is mainly for somewhat dynamic
	// globals like output? seat might probably go though, handle that!
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

struct swa_display* swa_display_wl_create(void) {
	struct wl_display* wld = wl_display_connect(NULL);
	if(!wld) {
		return NULL;
	}

	wl_log_set_handler_client(log_handler);

	struct swa_display_wl* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;

	dpy->registry = wl_display_get_registry(wld);
	wl_registry_add_listener(dpy->registry, &registry_listener, dpy);
	wl_display_roundtrip(dpy->display);

	const char* missing = NULL;
	if(!dpy->compositor) missing = "wl_compositor";

	if(missing) {
		printf("Missing required Wayland interface '%s'\n", missing);
		display_destroy(&dpy->base);
		return NULL;
	}

	return &dpy->base;
}
