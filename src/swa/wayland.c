#include <swa/wayland.h>
#include <dlg/dlg.h>
#include <mainloop.h>
#include <wayland-client-core.h>
#include <wayland-cursor.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;

static const struct xdg_toplevel_listener toplevel_listener;
static const struct xdg_surface_listener xdg_surface_listener;
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener;

// utility
static struct swa_display_wl* get_display_wl(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_wl*) base;
}

static struct swa_window_wl* get_window_wl(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_wl*) base;
}

static bool set_cloexec(int fd) {
	long flags = fcntl(fd, F_GETFD);
	if(flags == -1) {
		return false;
	}

	if(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
		return false;
	}

	return true;
}

static int create_pool_file(size_t size, char** name) {
	static const char template[] = "swa-buffer-XXXXXX";
	const char *path = getenv("XDG_RUNTIME_DIR");
	if (path == NULL) {
		fprintf(stderr, "XDG_RUNTIME_DIR is not set\n");
		return -1;
	}

	size_t name_size = strlen(template) + 1 + strlen(path) + 1;
	*name = malloc(name_size);
	snprintf(*name, name_size, "%s/%s", path, template);

	int fd = mkstemp(*name);
	if(fd < 0) {
		dlg_error("mkstemp: %s (%d)", strerror(errno), errno);
		return -1;
	}

	if(!set_cloexec(fd)) {
		dlg_error("set_cloexec: %s (%d)", strerror(errno), errno);
		close(fd);
		return -1;
	}

	if(ftruncate(fd, size) < 0) {
		dlg_error("ftruncate: %s (%d)", strerror(errno), errno);
		close(fd);
		return -1;
	}

	return fd;
}

static void buffer_release(void* data, struct wl_buffer* wl_buffer) {
	struct swa_wl_buffer* buffer = data;
	dlg_assert(buffer->buffer == wl_buffer);
	buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release
};

static bool buffer_init(struct swa_wl_buffer* buf, struct wl_shm *shm,
		int32_t width, int32_t height, uint32_t format) {
	uint32_t stride = width * 4;
	size_t size = stride * height;

	char *name;
	int fd = create_pool_file(size, &name);
	if(fd < 0) {
		return false;
	}

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
	buf->buffer = wl_shm_pool_create_buffer(pool, 0,
		width, height, stride, format);
	wl_shm_pool_destroy(pool);
	close(fd);
	unlink(name);
	free(name);
	fd = -1;

	buf->size = size;
	buf->data = data;
	buf->width = width;
	buf->height = height;

	wl_buffer_add_listener(buf->buffer, &buffer_listener, buf);
	return buf;
}

static void buffer_finish(struct swa_wl_buffer* buf) {
	if(buf->buffer) wl_buffer_destroy(buf->buffer);
	if(buf->data) munmap(buf->data, buf->size);
	memset(buf, 0, sizeof(*buf));
}

static void set_cursor_buffer(struct swa_display_wl* dpy,
		struct wl_buffer* buffer, int hx, int hy) {
	if(!dpy->pointer) {
		return;
	}

	dlg_assert(dpy->mouse_enter_serial);

	wl_pointer_set_cursor(dpy->pointer, dpy->mouse_enter_serial,
		dpy->cursor_surface, hx, hy);
	wl_surface_attach(dpy->cursor_surface, buffer, 0, 0);
	wl_surface_damage(dpy->cursor_surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(dpy->cursor_surface);
}

static const struct {
	enum swa_cursor_type cursor;
	const char* name;
} cursor_map[] = {
	{swa_cursor_left_pointer, "left_ptr"},
	{swa_cursor_load, "watch"},
	{swa_cursor_load_pointer, "left_ptr_watch"},
	{swa_cursor_right_pointer, "right_ptr"},
	{swa_cursor_hand, "pointer"},
	{swa_cursor_grab, "grab"},
	{swa_cursor_crosshair, "cross"},
	{swa_cursor_help, "question_arrow"},
	{swa_cursor_beam, "xterm"},
	{swa_cursor_forbidden, "crossed_circle"},
	{swa_cursor_size, "bottom_left_corner"},
	{swa_cursor_size_bottom, "bottom_side"},
	{swa_cursor_size_bottom_left, "bottom_left_corner"},
	{swa_cursor_size_bottom_right, "bottom_right_corner"},
	{swa_cursor_size_top, "top_side"},
	{swa_cursor_size_top_left, "top_left_corner"},
	{swa_cursor_size_top_right, "top_right_corner"},
	{swa_cursor_size_left, "left_side"},
	{swa_cursor_size_right, "right_side"},
};

static const char* cursor_name(enum swa_cursor_type type) {
	const unsigned count = sizeof(cursor_map) / sizeof(cursor_map[0]);
	for(unsigned i = 0u; i < count; ++i) {
		if(cursor_map[i].cursor == type) {
			return cursor_map[i].name;
		}
	}

	return NULL;
}

// window api
static void win_destroy(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);

	// unlink from list
	if(win->next) win->next->prev = win->prev;
	if(win->prev) win->prev->next = win->next;

	if(win->dpy) {
		if(win->dpy->focus == win) win->dpy->focus = NULL;
		if(win->dpy->mouse_over == win) win->dpy->focus = NULL;
		if(win->dpy->window_list == win) win->dpy->window_list = NULL;
	}

	if(win->defer_redraw) ml_defer_destroy(win->defer_redraw);
	if(win->frame_callback) wl_callback_destroy(win->frame_callback);
	if(win->decoration) zxdg_toplevel_decoration_v1_destroy(win->decoration);
	if(win->xdg_toplevel) xdg_toplevel_destroy(win->xdg_toplevel);
	if(win->xdg_surface) xdg_surface_destroy(win->xdg_surface);
	if(win->surface) wl_surface_destroy(win->surface);

	// TODO: destroy render surfaces
	free(base);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
	enum swa_window_cap caps =
		swa_window_cap_fullscreen |
		swa_window_cap_minimize |
		swa_window_cap_maximize |
		swa_window_cap_size_limits |
		swa_window_cap_cursor |
		swa_window_cap_title |
		swa_window_cap_begin_move |
		swa_window_cap_begin_resize;
	return caps;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_wl* win = get_window_wl(base);
	xdg_toplevel_set_min_size(win->xdg_toplevel, w, h);
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_wl* win = get_window_wl(base);
	xdg_toplevel_set_max_size(win->xdg_toplevel, w, h);
}

static void win_show(struct swa_window* base, bool show) {
	dlg_warn("window doesn't have visibility capability");
}

static void win_set_size(struct swa_window* base, unsigned w, unsigned h) {
	// TODO: we might be able to implement it by just chaing width and height
	// and redrawing, resulting in a larger buffer. Not sure.
	dlg_warn("window doesn't have resize capability");
}
static void win_set_position(struct swa_window* base, int x, int y) {
	dlg_warn("window doesn't have position capability");
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_wl* win = get_window_wl(base);

	enum swa_cursor_type type = cursor.type;
	if(type == swa_cursor_default) {
		type = swa_cursor_left_pointer;
	}

	if(cursor.type == swa_cursor_none) {
		buffer_finish(&win->cursor_buffer);
		win->cursor_hx = win->cursor_hy = 0;
	} else if(cursor.type == swa_cursor_image) {
		win->cursor_hx = cursor.hx;
		win->cursor_hy = cursor.hy;
		if(!win->cursor_buffer.data ||
				win->cursor_buffer.width != cursor.image.width ||
				win->cursor_buffer.height != cursor.image.height) {
			buffer_finish(&win->cursor_buffer);
			buffer_init(&win->cursor_buffer, win->dpy->shm,
				cursor.image.width, cursor.image.height,
				WL_SHM_FORMAT_ARGB8888);
		}

		// TODO: format conversion only little endian atm
		struct swa_image dst = {
			.width = win->cursor_buffer.width,
			.height = win->cursor_buffer.height,
			.stride = 4 * win->cursor_buffer.width,
			.format = swa_image_format_bgra32,
			.data = win->cursor_buffer.data,
		};
		swa_convert_image(&cursor.image, &dst);
	} else {
		const char* cname = cursor_name(cursor.type);
		if(!cname) {
			dlg_warn("failed to convert cursor type %d to xcursor", cursor.type);
			cname = cursor_name(swa_cursor_left_pointer);
		}

		struct wl_cursor* wl_cursor = wl_cursor_theme_get_cursor(
			win->dpy->cursor_theme, cname);
		if(!wl_cursor) {
			dlg_warn("failed to retrieve cursor %s", cname);
			return;
		}

		buffer_finish(&win->cursor_buffer);

		// TODO: handle mulitple/animated images
		struct wl_cursor_image* img = wl_cursor->images[0];
		win->cursor_buffer.buffer = wl_cursor_image_get_buffer(img);
		win->cursor_buffer.width = img->width;
		win->cursor_buffer.height = img->height;
		win->cursor_hx = img->hotspot_y;
		win->cursor_hy = img->hotspot_x;
	}

	// update cursor if mouse is currently over window
	if(win->dpy->mouse_over == win) {
		set_cursor_buffer(win->dpy, win->cursor_buffer.buffer,
			win->cursor_hx, win->cursor_hy);
	}
}

static void refresh_cb(struct ml_defer* defer) {
	struct swa_window_wl* win = ml_defer_get_data(defer);
	win->redraw = false;
	if(win->base.listener && win->base.listener->draw) {
		win->base.listener->draw(&win->base);
	}
	ml_defer_enable(defer, false);
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->configured || win->frame_callback) {
		win->redraw = true;
	}

	if(!win->defer_redraw) {
		win->defer_redraw = ml_defer_new(win->dpy->mainloop, refresh_cb);
		ml_defer_set_data(win->defer_redraw, win);
	}
	ml_defer_enable(win->defer_redraw, true);
}

static void win_surface_frame(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(win->frame_callback) {
		return;
	}

	win->frame_callback = wl_surface_frame(win->surface);
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	struct swa_window_wl* win = get_window_wl(base);
	switch(state) {
		case swa_window_state_normal:
			xdg_toplevel_unset_fullscreen(win->xdg_toplevel);
			xdg_toplevel_unset_maximized(win->xdg_toplevel);
			break;
		case swa_window_state_fullscreen:
			xdg_toplevel_set_fullscreen(win->xdg_toplevel, NULL);
			break;
		case swa_window_state_maximized:
			xdg_toplevel_set_maximized(win->xdg_toplevel);
			break;
		case swa_window_state_minimized:
			xdg_toplevel_set_minimized(win->xdg_toplevel);
			break;
		default:
			break;
	}
}

static void win_begin_move(struct swa_window* base, void* trigger) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->dpy->seat) {
		dlg_warn("Can't begin moving window without seat");
		return;
	}

	if(!trigger) {
		dlg_warn("Can't begin moving window without valid trigger event");
		return;
	}

	uint32_t serial = (uint32_t) trigger;
	xdg_toplevel_move(win->xdg_toplevel, win->dpy->seat, serial);
}
static void win_begin_resize(struct swa_window* base, enum swa_edge edges,
		void* trigger) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->dpy->seat) {
		dlg_warn("Can't begin resizing window without seat");
		return;
	}

	if(!trigger) {
		dlg_warn("Can't begin resizing window without valid trigger event");
		return;
	}

	uint32_t serial = (uint32_t) trigger;

	// the enumerations map directly onto each other
	enum xdg_toplevel_resize_edge wl_edges = (enum xdg_toplevel_resize_edge) edges;
	xdg_toplevel_resize(win->xdg_toplevel, win->dpy->seat, wl_edges, serial);
}
static void win_set_title(struct swa_window* base, const char* title) {
	struct swa_window_wl* win = get_window_wl(base);
	xdg_toplevel_set_title(win->xdg_toplevel, title);
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
	dlg_warn("window doesn't have icon capability");
}
static bool win_is_client_decorated(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->decoration) {
		return true;
	}

	// we set this special value when creating the window with creation
	// it means that the server hasn't replied yet, which is weird.
	if(win->decoration_mode == 0xFFFFFFFFu) {
		dlg_warn("compositor hasn't informed window about decoration mode yet");
		return true;
	}

	return win->decoration_mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
}
static bool win_get_vk_surface(struct swa_window* base, void* vkSurfaceKHR) {
	struct swa_window_wl* win = get_window_wl(base);
}

static bool win_gl_make_current(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
}
static bool win_gl_swap_buffers(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
}
static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
	struct swa_window_wl* win = get_window_wl(base);
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	struct swa_window_wl* win = get_window_wl(base);
}
static void win_apply_buffer(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
}

static const struct swa_window_interface window_impl = {
	.destroy = win_destroy,
	.get_capabilities = win_get_capabilities,
	.set_min_size = win_set_min_size,
	.set_max_size = win_set_max_size,
	.show = win_show,
	.set_size = win_set_size,
	.set_position = win_set_position,
	.refresh = win_refresh,
	.surface_frame = win_surface_frame,
	.set_state = win_set_state,
	.begin_move = win_begin_move,
	.begin_resize = win_begin_resize,
	.set_title = win_set_title,
	.set_icon = win_set_icon,
	.is_client_decorated = win_is_client_decorated,
	.get_vk_surface = win_get_vk_surface,
	.gl_make_current = win_gl_make_current,
	.gl_set_swap_interval = win_gl_set_swap_interval,
	.get_buffer = win_get_buffer,
	.apply_buffer = win_apply_buffer
};

// display api
static void xdg_wm_base_ping(void *data, struct xdg_wm_base* wm_base,
		uint32_t serial) {
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = xdg_wm_base_ping
};

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
}

bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	struct swa_display_wl* dpy = get_display_wl(base);
	const unsigned n_bits = 8 * sizeof(dpy->key_states);
	if(key >= n_bits) {
		dlg_warn("keycode not tracked (too high)");
		return false;
	}

	unsigned idx = key / 64;
	unsigned bit = key % 64;
	return (dpy->key_states[idx] & (1 << bit));
}

const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return NULL;
}

enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return swa_keyboard_mod_none;
}

struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return dpy->focus;
}

bool display_mouse_button_pressed(struct swa_display* base, enum swa_mouse_button button) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(button >= 64) {
		dlg_warn("mouse button code not tracked (too high)");
		return false;
	}

	return (dpy->mouse_button_states & (1 << button));
}
void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->mouse_over) {
		return;
	}

	*x = dpy->mouse_x;
	*y = dpy->mouse_y;
}
struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return dpy->mouse_over;
}

struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return NULL;
}

bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source, void* trigger) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return false;
}
bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source, void* trigger) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return false;
}

struct swa_window* display_create_window(struct swa_display* base,
		struct swa_window_settings settings) {
	struct swa_display_wl* dpy = get_display_wl(base);
	struct swa_window_wl* win = calloc(1, sizeof(*win));
	win->base.impl = &window_impl;
	win->dpy = dpy;
	win->surface = wl_compositor_create_surface(dpy->compositor);
	wl_surface_set_user_data(win->surface, win);
	win->width = settings.width;
	win->height = settings.height;

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

	// when the decoration protocol is not present, client side decorations
	// should be assumed on wayland
	win->decoration_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	if(dpy->decoration_manager) {
		win->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
			dpy->decoration_manager, win->xdg_toplevel);
		zxdg_toplevel_decoration_v1_add_listener(win->decoration,
			&decoration_listener, win);
		win->decoration_mode = 0xFFFFFFFFu;
		if(settings.client_decorate == swa_preference_yes) {
			zxdg_toplevel_decoration_v1_set_mode(win->decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		} else if(settings.client_decorate == swa_preference_no) {
			zxdg_toplevel_decoration_v1_set_mode(win->decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
	} else if(settings.client_decorate == swa_preference_no) {
		dlg_info("Can't set up server side decoration since "
			"the compositor doesn't support the decoration protocol");
	}

	if(settings.state != swa_window_state_normal) {
		swa_window_set_state(&win->base, settings.state);
	}

	// commit the role so we get a configure event and can start drawing
	wl_surface_commit(win->surface);
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

static void decoration_configure(void *data,
		struct zxdg_toplevel_decoration_v1* deco, uint32_t mode) {
	struct swa_window_wl* win = data;
	win->decoration_mode = mode;
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	.configure = decoration_configure
};

static void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		  int32_t width, int32_t height, struct wl_array *states) {
	struct swa_window_wl* win = data;
	dlg_assert(width >= 0 && height >= 0);

	// width or height being 0 means we should decide them ourselves
	// (mainly for the first configure event).
	bool resized = false;
	if(width) {
		resized = (int32_t)win->width != width;
		win->width = width;
	} else if(win->width == SWA_DEFAULT_SIZE) {
		resized = true;
		win->width = SWA_FALLBACK_WIDTH;
	}

	if(height) {
		resized = (int32_t)win->height != height;
		win->height = height;
	} else if(win->height == SWA_DEFAULT_SIZE) {
		resized = true;
		win->height = SWA_FALLBACK_HEIGHT;
	}

	if(resized && win->base.listener && win->base.listener->resize) {
		win->base.listener->resize(&win->base, win->width, win->height);
	}

	// refresh the window if the size changed or if it was never
	// drawn since this is the first configure event
	bool draw = win->base.listener && win->base.listener->draw;
	if(draw && (resized || !win->configured)) {
		if(win->frame_callback) {
			dlg_assert(win->configured);
			win->redraw = true;
		} else {
			win->base.listener->draw(&win->base);
		}
	}

	win->configured = true;

	// there is no way to know whether the window is minimized.
	// If a compositor sends fullscreen and maximized in the state list,
	// we consider the window is fullscreen state.
	uint32_t* wl_states = (uint32_t*)(states->data);
	enum swa_window_state state = swa_window_state_normal;
	for(unsigned i = 0u; i < states->size / sizeof(uint32_t); ++i) {
		bool finished = false;
		switch(wl_states[i]) {
			case XDG_TOPLEVEL_STATE_FULLSCREEN:
				state = swa_window_state_fullscreen;
				finished = true;
				break;
			case XDG_TOPLEVEL_STATE_MAXIMIZED:
				state = swa_window_state_maximized;
				break;
		}

		if(finished) {
			break;
		}
	}

	if(state != win->state) {
		win->state = state;
		if(win->base.listener && win->base.listener->state) {
			win->base.listener->state(&win->base, state);
		}
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	struct swa_window_wl* win = data;
	if(win->base.listener && win->base.listener->close) {
		win->base.listener->close(&win->base);
	}
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
		  uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);

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
	} else if(strcmp(interface, xdg_wm_base_interface.name) == 0) {
		dpy->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(dpy->xdg_wm_base, &wm_base_listener, dpy);
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
