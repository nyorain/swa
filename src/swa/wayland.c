#define _POSIX_C_SOURCE 200809L

#include <swa/config.h>
#include <swa/private/wayland.h>
#include <dlg/dlg.h>
#include <pml.h>
#include <wayland-client-core.h>
#include <wayland-cursor.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <poll.h>
#include <linux/input-event-codes.h>

#ifdef SWA_WITH_VK
  #include <vulkan/vulkan.h>
  #include <vulkan/vulkan_wayland.h>
#endif

#ifdef SWA_WITH_GL
  #include <swa/private/egl.h>
  #include <wayland-egl-core.h>
  #include <EGL/egl.h>
#endif

#define SWA_DECORATION_MODE_PENDING 0xFFFFFFFFu

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;
static const struct swa_data_offer_interface data_offer_impl;

static const struct xdg_toplevel_listener toplevel_listener;
static const struct xdg_surface_listener xdg_surface_listener;
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener;
static const struct wl_callback_listener cursor_frame_listener;

static char* last_wl_log = NULL;

// from xcursor.c
const char* const* swa_get_xcursor_names(enum swa_cursor_type type);

// utility
static struct swa_display_wl* get_display_wl(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_wl*) base;
}

static struct swa_window_wl* get_window_wl(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_wl*) base;
}

static struct swa_data_offer_wl* get_data_offer_wl(struct swa_data_offer* base) {
	dlg_assert(base->impl == &data_offer_impl);
	return (struct swa_data_offer_wl*) base;
}

// Checks and returns if the display has an (critical) error.
// The first time this error is noticed, a description will be printed.
static bool check_error(struct swa_display_wl* dpy) {
	if(dpy->error) {
		return true;
	}

	int err = wl_display_get_error(dpy->display);
	if(!err) {
		return false;
	}

	dpy->error = true;
	if(err == EPROTO) {
		struct error {
			int code;
			const char* msg;
		};

#define ERROR(name) {name, #name}
#define MAX_ERRORS 6
		static const struct {
			const struct wl_interface* interface;
			struct error errors[MAX_ERRORS];
		} errors[] = {
			// core protocol
			{&wl_display_interface, {
				ERROR(WL_DISPLAY_ERROR_INVALID_OBJECT),
				ERROR(WL_DISPLAY_ERROR_INVALID_METHOD),
				ERROR(WL_DISPLAY_ERROR_NO_MEMORY),
			}},
			{&wl_shm_interface, {
				ERROR(WL_SHM_ERROR_INVALID_FORMAT),
				ERROR(WL_SHM_ERROR_INVALID_STRIDE),
				ERROR(WL_SHM_ERROR_INVALID_FD),
			}},
			{&wl_data_offer_interface, {
				ERROR(WL_DATA_OFFER_ERROR_INVALID_FINISH),
				ERROR(WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK),
				ERROR(WL_DATA_OFFER_ERROR_INVALID_ACTION),
				ERROR(WL_DATA_OFFER_ERROR_INVALID_OFFER),
			}},
			{&wl_data_source_interface, {
				ERROR(WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK),
				ERROR(WL_DATA_SOURCE_ERROR_INVALID_SOURCE),
			}},
			{&wl_data_device_interface, {
				ERROR(WL_DATA_DEVICE_ERROR_ROLE),
			}},
			{&wl_surface_interface, {
				ERROR(WL_SURFACE_ERROR_INVALID_SCALE),
				ERROR(WL_SURFACE_ERROR_INVALID_TRANSFORM),
			}},
			{&xdg_wm_base_interface, {
				ERROR(XDG_WM_BASE_ERROR_ROLE),
				ERROR(XDG_WM_BASE_ERROR_DEFUNCT_SURFACES),
				ERROR(XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP),
				ERROR(XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT),
				ERROR(XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE),
				ERROR(XDG_WM_BASE_ERROR_INVALID_POSITIONER),
			}},
			{&xdg_surface_interface, {
				ERROR(XDG_SURFACE_ERROR_NOT_CONSTRUCTED),
				ERROR(XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED),
				ERROR(XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER),
			}},
			{&zxdg_toplevel_decoration_v1_interface, {
				ERROR(ZXDG_TOPLEVEL_DECORATION_V1_ERROR_UNCONFIGURED_BUFFER),
				ERROR(ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED),
				ERROR(ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ORPHANED),
			}},
		};
#undef ERROR

		const struct wl_interface* interface;
		uint32_t id;
		int code = wl_display_get_protocol_error(dpy->display, &interface, &id);

		const char* error_name = "<unknown>";
		const char* interface_name = "<null interface>";
		if(interface) {
			unsigned ec = sizeof(errors) / sizeof(errors[0]);
			for(unsigned i = 0u; i < ec; ++i) {
				if(errors[i].interface == interface) {
					for(unsigned e = 0u; e < MAX_ERRORS; ++e) {
						struct error error = errors[i].errors[e];
						if(error.code == code) {
							error_name = error.msg;
							break;
						} else if(error.code == 0) {
							break;
						}
					}
					break;
				}
			}
			interface_name = interface->name;
		}

		dlg_error("Wayland display has critical protocol error\n\t"
			"Interface: '%s'\n\t"
			"Error: '%s'", interface_name, error_name);
	} else {
		const char* ename = strerror(err);
		if(!ename) {
			ename = "<unknown>";
		}

		dlg_error("Wayland display has critical error: '%s' (%d)",
			ename, err);
	}

	return true;
}

static bool add_fd_flags(int fd, int add_flags) {
	long flags = fcntl(fd, F_GETFD);
	if(flags == -1) {
		dlg_error("fcntl (get): %s (%d)", strerror(errno), errno);
		return false;
	}

	if(fcntl(fd, F_SETFD, flags | add_flags) == -1) {
		dlg_error("fcntl (set): %s (%d)", strerror(errno), errno);
		return false;
	}

	return true;
}

static bool swa_pipe(int fds[static 2]) {
	// NOTE: on linux we could use pipe2 here, not as racy
	int err = pipe(fds);
	if(err < 0) {
		dlg_error("pipe: %s (%d)", strerror(errno), errno);
		return false;
	}

	int flags = O_NONBLOCK | FD_CLOEXEC;
	if(!add_fd_flags(fds[0], flags) || !add_fd_flags(fds[0], flags)) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}

	return true;
}

static int create_pool_file(size_t size, char** name) {
	static const char template[] = "swa-buffer-XXXXXX";
	const char *path = getenv("XDG_RUNTIME_DIR");
	if(path == NULL) {
		dlg_error("XDG_RUNTIME_DIR is not set");
		return -1;
	}

	size_t name_size = strlen(template) + 1 + strlen(path) + 1;
	*name = malloc(name_size);
	snprintf(*name, name_size, "%s/%s", path, template);

	int fd = mkstemp(*name);
	if(fd < 0) {
		dlg_error("mkstemp: %s (%d)", strerror(errno), errno);
		free(name);
		return -1;
	}

	if(!add_fd_flags(fd, FD_CLOEXEC)) {
		dlg_error("set_cloexec: %s (%d)", strerror(errno), errno);
		close(fd);
		free(name);
		return -1;
	}

	if(ftruncate(fd, size) < 0) {
		dlg_error("ftruncate: %s (%d)", strerror(errno), errno);
		close(fd);
		free(name);
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

static bool buffer_init(struct swa_wl_buffer* buf, struct wl_shm* shm,
		int32_t width, int32_t height, uint32_t format) {
	uint32_t stride = width * 4;
	size_t size = stride * height;

	char* name;
	int fd = create_pool_file(size, &name);
	if(fd < 0) {
		return false;
	}

	void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(data == MAP_FAILED) {
		close(fd);
		unlink(name);
		free(name);
		return false;
	}

	struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
	if(!pool) {
		munmap(data, size);
		unlink(name);
		close(fd);
		free(name);
		return false;
	}

	buf->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
	wl_shm_pool_destroy(pool);
	close(fd);
	unlink(name);
	free(name);
	if(!buf->buffer) {
		munmap(data, size);
		return false;
	}

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

static void cursor_render(struct swa_display_wl* dpy) {
	dlg_assert(dpy->cursor.timer);
	dlg_assert(dpy->cursor.active);

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint32_t dms = 1000 * (ts.tv_sec - dpy->cursor.set.tv_sec) +
		(ts.tv_nsec - dpy->cursor.set.tv_nsec) / (1000 * 1000);

	uint32_t duration;
	int idx = wl_cursor_frame_and_duration(dpy->cursor.active, dms, &duration);

	struct wl_cursor_image* img = dpy->cursor.active->images[idx];
	struct wl_buffer* buffer = wl_cursor_image_get_buffer(img);

	dpy->cursor.frame_callback = wl_surface_frame(dpy->cursor.surface);
	wl_callback_add_listener(dpy->cursor.frame_callback,
		&cursor_frame_listener, dpy);

	wl_surface_attach(dpy->cursor.surface, buffer, 0, 0);
	wl_surface_damage(dpy->cursor.surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(dpy->cursor.surface);

	if(duration > 0) {
		ts.tv_nsec += duration * 1000 * 1000;
		pml_timer_set_time(dpy->cursor.timer, ts);
	}
}

static void cursor_frame(void* data, struct wl_callback* cb, uint32_t id) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->cursor.frame_callback == cb);
	wl_callback_destroy(cb);
	dpy->cursor.frame_callback = NULL;
	if(dpy->cursor.redraw) {
		dpy->cursor.redraw = false;
		cursor_render(dpy);
	}
}

static const struct wl_callback_listener cursor_frame_listener = {
	.done = cursor_frame
};

static void cursor_time_cb(struct pml_timer* timer) {
	struct swa_display_wl* dpy = pml_timer_get_data(timer);
	if(dpy->cursor.frame_callback) {
		dpy->cursor.redraw = true;
		return;
	}

	cursor_render(dpy);
}

static void set_cursor(struct swa_display_wl* dpy, struct swa_window_wl* win) {
	if(!dpy->cursor.surface) {
		return;
	}

	if(dpy->cursor.frame_callback) {
		wl_callback_destroy(dpy->cursor.frame_callback);
		dpy->cursor.frame_callback = NULL;
	}

	dlg_assert(dpy->mouse_enter_serial);
	dpy->cursor.active = NULL;
	dpy->cursor.redraw = false;

	struct wl_buffer* buffer = NULL;
	int hx = 0;
	int hy = 0;
	if(win->cursor.native) {
		struct wl_cursor_image* start = win->cursor.native->images[0];
		buffer = wl_cursor_image_get_buffer(start);
		hy = start->hotspot_y;
		hx = start->hotspot_x;

		dpy->cursor.active = win->cursor.native;
		dpy->cursor.frame_callback = wl_surface_frame(dpy->cursor.surface);
		wl_callback_add_listener(dpy->cursor.frame_callback,
			&cursor_frame_listener, dpy);

		if(win->cursor.native->image_count > 1) {
			clock_gettime(CLOCK_MONOTONIC, &dpy->cursor.set);
			struct timespec next = dpy->cursor.set;
			next.tv_nsec += start->delay * 1000 * 1000;
			pml_timer_set_time(dpy->cursor.timer, next);
		}
	} else {
		buffer = win->cursor.buffer.buffer;
		hx = win->cursor.hx;
		hy = win->cursor.hy;
	}

	wl_pointer_set_cursor(dpy->pointer, dpy->mouse_enter_serial,
		dpy->cursor.surface, hx, hy);
	wl_surface_attach(dpy->cursor.surface, buffer, 0, 0);
	wl_surface_damage(dpy->cursor.surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(dpy->cursor.surface);
}


// window api
static void win_destroy(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);

	// unlink everywhere
	if(win->dpy) {
		if(win->dpy->focus == win) win->dpy->focus = NULL;
		if(win->dpy->mouse_over == win) win->dpy->mouse_over = NULL;

		// remove tracked touch points for this window
		unsigned out = 0u;
		for(unsigned i = 0u; i < win->dpy->n_touch_points; ++i) {
			if(win->dpy->touch_points[i].window == win) {
				continue;
			}

			if(i != out) {
				win->dpy->touch_points[out] = win->dpy->touch_points[i];
			}
			++out;
		}

		win->dpy->n_touch_points = out;
	}

	// destroy surface buffer
	if(win->surface_type == swa_surface_buffer) {
		for(unsigned i = 0u; i < win->buffer.n_bufs; ++i) {
			buffer_finish(&win->buffer.buffers[i]);
		}
		free(win->buffer.buffers);
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		if(win->vk.surface) {
			dlg_assert(win->vk.instance);

			VkInstance instance = (VkInstance) win->vk.instance;
			VkSurfaceKHR surface = (VkSurfaceKHR) win->vk.surface;
			PFN_vkDestroySurfaceKHR fn = (PFN_vkDestroySurfaceKHR)
				vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
			if(fn) {
				fn(instance, surface, NULL);
			} else {
				dlg_error("Failed to load 'vkDestroySurfaceKHR' function");
			}
		}
#else
		dlg_error("swa was compiled without vk support; invalid surface");
#endif
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		// if they are still current, egl will defer destruction
		if(win->gl.context) {
			eglDestroyContext(win->dpy->egl->display, win->gl.context);
		}
		if(win->gl.surface) {
			eglDestroySurface(win->dpy->egl->display, win->gl.surface);
		}
		if(win->gl.egl_window) {
			wl_egl_window_destroy(win->gl.egl_window);
		}
#else
		dlg_error("swa was compiled without gl support; invalid surface");
#endif
	}

	if(win->defer_redraw) pml_defer_destroy(win->defer_redraw);
	if(win->frame_callback) wl_callback_destroy(win->frame_callback);
	if(win->decoration) zxdg_toplevel_decoration_v1_destroy(win->decoration);
	if(win->xdg_toplevel) xdg_toplevel_destroy(win->xdg_toplevel);
	if(win->xdg_surface) xdg_surface_destroy(win->xdg_surface);
	if(win->wl_surface) wl_surface_destroy(win->wl_surface);

	free(base);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	enum swa_window_cap caps =
		swa_window_cap_fullscreen |
		swa_window_cap_minimize |
		swa_window_cap_maximize |
		swa_window_cap_size_limits |
		swa_window_cap_title;
	if(win->dpy->seat) {
		caps |=
			swa_window_cap_begin_move |
			swa_window_cap_begin_resize;
	}
	if(win->dpy->cursor.theme) {
		caps |= swa_window_cap_cursor;
	}
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
	// we might be able to implement it by just chaing width and height
	// and redrawing, resulting in a larger buffer. Not sure.
	dlg_warn("window doesn't have resize capability");
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->dpy->cursor.surface) {
		dlg_warn("Display doesn't have cursor surface");
		return;
	}

	if(cursor.type != swa_cursor_none && !win->dpy->shm) {
		dlg_warn("Can't set custom cursor since compositor doesn't have wl_shm");
		return;
	}

	enum swa_cursor_type type = cursor.type;

	// There is no concept of a "default" cursor type on wayland.
	// Note that unsetting the cursor (i.e. not actively setting any
	// cursor on pointer enter) is *not* what we want since then the
	// previous cursor will just be shown (which may be anything, depending
	// on the window we come from).
	if(type == swa_cursor_default) {
		type = swa_cursor_left_pointer;
	}

	if(type == swa_cursor_none) {
		buffer_finish(&win->cursor.buffer);
		win->cursor.hx = win->cursor.hy = 0;
		win->cursor.native = NULL;
	} else if(type == swa_cursor_image) {
		static const enum wl_shm_format wl_fmt = WL_SHM_FORMAT_ARGB8888;
		static const enum swa_image_format swa_fmt = swa_image_format_bgra32;

		win->cursor.hx = cursor.hx;
		win->cursor.hy = cursor.hy;
		if(!win->cursor.buffer.data ||
				win->cursor.buffer.width != cursor.image.width ||
				win->cursor.buffer.height != cursor.image.height) {
			buffer_finish(&win->cursor.buffer);
			if(!buffer_init(&win->cursor.buffer, win->dpy->shm,
					cursor.image.width, cursor.image.height, wl_fmt)) {
				return;
			}
		}

		struct swa_image dst = {
			.width = win->cursor.buffer.width,
			.height = win->cursor.buffer.height,
			.stride = 4 * win->cursor.buffer.width,
			.format = swa_fmt,
			.data = win->cursor.buffer.data,
		};
		swa_convert_image(&cursor.image, &dst);
	} else {
		const char* const* names = swa_get_xcursor_names(type);
		if(!names) {
			dlg_warn("failed to convert cursor type %d to xcursor", type);
			return;
		}

		struct wl_cursor* cursor = NULL;
		for(; *names; ++names) {
			cursor = wl_cursor_theme_get_cursor(win->dpy->cursor.theme, *names);
			if(cursor) {
				break;
			} else {
				dlg_debug("failed to retrieve cursor %s", *names);
			}
		}

		if(!cursor) {
			dlg_warn("Failed to get any cursor for cursor type %d", type);
			return;
		}

		win->cursor.native = cursor;
		buffer_finish(&win->cursor.buffer);
	}

	// update cursor if mouse is currently over window
	if(win->dpy->mouse_over == win) {
		set_cursor(win->dpy, win);
	}
}

static void refresh_cb(struct pml_defer* defer) {
	struct swa_window_wl* win = pml_defer_get_data(defer);
	win->redraw = false;

	// this must be done *before* calling the draw handler to allow
	// it to potentially enable it again (e.g. when calling
	// refresh without previous frame callback)
	pml_defer_enable(defer, false);
	if(win->base.listener->draw) {
		win->base.listener->draw(&win->base);
	}
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->configured || win->frame_callback) {
		win->redraw = true;
		return;
	}

	if(!win->defer_redraw) {
		win->defer_redraw = pml_defer_new(win->dpy->pml, refresh_cb);
		pml_defer_set_data(win->defer_redraw, win);
	}
	pml_defer_enable(win->defer_redraw, true);
}

static void win_frame_done(void* data, struct wl_callback* cb, uint32_t id) {
	struct swa_window_wl* win = data;
	dlg_assert(win->frame_callback == cb);
	wl_callback_destroy(win->frame_callback);
	win->frame_callback = NULL;

	if(win->redraw) {
		win->redraw = false;
		if(win->base.listener->draw) {
			win->base.listener->draw(&win->base);
		}
	}
}

static const struct wl_callback_listener win_frame_listener = {
	.done = win_frame_done,
};

static void win_surface_frame(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(win->frame_callback) {
		wl_callback_destroy(win->frame_callback);
		win->frame_callback = NULL;
	}

	win->frame_callback = wl_surface_frame(win->wl_surface);
	wl_callback_add_listener(win->frame_callback, &win_frame_listener, win);
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	struct swa_window_wl* win = get_window_wl(base);
	switch(state) {
		case swa_window_state_normal:
			win->state = state;
			xdg_toplevel_unset_fullscreen(win->xdg_toplevel);
			xdg_toplevel_unset_maximized(win->xdg_toplevel);
			break;
		case swa_window_state_fullscreen:
			win->state = state;
			xdg_toplevel_set_fullscreen(win->xdg_toplevel, NULL);
			break;
		case swa_window_state_maximized:
			win->state = state;
			xdg_toplevel_set_maximized(win->xdg_toplevel);
			break;
		case swa_window_state_minimized:
			win->state = state;
			xdg_toplevel_set_minimized(win->xdg_toplevel);
			break;
		default:
			dlg_warn("Invalid window state %d", state);
			break;
	}
}

static void win_begin_move(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->dpy->seat) {
		dlg_warn("Can't begin moving window without seat");
		return;
	}

	xdg_toplevel_move(win->xdg_toplevel, win->dpy->seat, win->dpy->last_serial);
}
static void win_begin_resize(struct swa_window* base, enum swa_edge edges) {
	struct swa_window_wl* win = get_window_wl(base);
	if(!win->dpy->seat) {
		dlg_warn("Can't begin resizing window without seat");
		return;
	}

	// the enumerations map directly onto each other
	enum xdg_toplevel_resize_edge wl_edges = (enum xdg_toplevel_resize_edge) edges;
	xdg_toplevel_resize(win->xdg_toplevel, win->dpy->seat,
		win->dpy->last_serial, wl_edges);
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
	if(win->decoration_mode == SWA_DECORATION_MODE_PENDING) {
		dlg_warn("compositor hasn't informed window about decoration mode yet");
		return true;
	}

	return win->decoration_mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
}

static uint64_t win_get_vk_surface(struct swa_window* base) {
#ifdef SWA_WITH_VK
	struct swa_window_wl* win = get_window_wl(base);
	if(win->surface_type != swa_surface_vk) {
		dlg_warn("can't get vulkan surface from non-vulkan window");
		return 0;
	}

	return win->vk.surface;
#else
	dlg_warn("swa was compiled without vulkan suport");
	return 0;
#endif
}

static bool win_gl_make_current(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_wl* win = get_window_wl(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);
	return eglMakeCurrent(win->dpy->egl->display, win->gl.surface,
		win->gl.surface, win->gl.context);
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_swap_buffers(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_wl* win = get_window_wl(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);

	// eglSwapBuffers must commit to the surface in one way or another
	win_surface_frame(&win->base);
	return eglSwapBuffers(win->dpy->egl->display, win->gl.surface);
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
#ifdef SWA_WITH_GL
	// struct swa_window_wl* win = get_window_wl(base);
	dlg_error("Unimplemented");
	return false;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	// We always just use this format since it's compatible with cairo
	// and guaranteed to be supported by all compositors
	static const enum wl_shm_format format = WL_SHM_FORMAT_ARGB8888;

	struct swa_window_wl* win = get_window_wl(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return false;
	}

	if(win->buffer.active != -1) {
		dlg_error("There is already an active buffer");
		return false;
	}

	// search for free buffer
	// prefer buffers with matching dimensions
	struct swa_wl_buffer* found = NULL;
	bool recreate = true;
	unsigned active;
	for(unsigned i = 0u; i < win->buffer.n_bufs; ++i) {
		struct swa_wl_buffer* buf = &win->buffer.buffers[i];
		if(buf->busy) {
			continue;
		}

		active = i;
		found = buf;
		if(buf->width == win->width && buf->height == win->height) {
			recreate = false;
			break;
		}
	}

	if(!found) { // create a new buffer
		active = win->buffer.n_bufs;
		++win->buffer.n_bufs;
		unsigned size = win->buffer.n_bufs * sizeof(*win->buffer.buffers);
		win->buffer.buffers = realloc(win->buffer.buffers, size);
		found = &win->buffer.buffers[active];
		if(!buffer_init(found, win->dpy->shm, win->width, win->height, format)) {
			return false;
		}
	} else if(recreate) {
		buffer_finish(found);
		if(!buffer_init(found, win->dpy->shm, win->width, win->height, format)) {
			return false;
		}
	}

	img->width = win->width;
	img->height = win->height;
	img->stride = win->width * 4;
	img->format = swa_image_format_bgra32;
	img->data = found->data;

	win->buffer.active = active;
	return true;
}

static void win_apply_buffer(struct swa_window* base) {
	struct swa_window_wl* win = get_window_wl(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return;
	}

	if(win->buffer.active < 0) {
		dlg_error("No active buffer");
		return;
	}

	struct swa_wl_buffer* buf = &win->buffer.buffers[win->buffer.active];
	wl_surface_attach(win->wl_surface, buf->buffer, 0, 0);
	wl_surface_damage(win->wl_surface, 0, 0, INT32_MAX, INT32_MAX);
	win_surface_frame(&win->base);
	wl_surface_commit(win->wl_surface);

	win->buffer.active = -1;
}

static const struct swa_window_interface window_impl = {
	.destroy = win_destroy,
	.get_capabilities = win_get_capabilities,
	.set_min_size = win_set_min_size,
	.set_max_size = win_set_max_size,
	.show = win_show,
	.set_size = win_set_size,
	.refresh = win_refresh,
	.surface_frame = win_surface_frame,
	.set_state = win_set_state,
	.set_cursor = win_set_cursor,
	.begin_move = win_begin_move,
	.begin_resize = win_begin_resize,
	.set_title = win_set_title,
	.set_icon = win_set_icon,
	.is_client_decorated = win_is_client_decorated,
	.get_vk_surface = win_get_vk_surface,
	.gl_make_current = win_gl_make_current,
	.gl_swap_buffers = win_gl_swap_buffers,
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

static void data_offer_offer(void* data, struct wl_data_offer* wl_data_offer,
		const char* mime_type) {
	struct swa_data_offer_wl* offer = data;
	dlg_assert(offer->offer == wl_data_offer);

	++offer->n_formats;
	unsigned size = offer->n_formats * sizeof(*offer->formats);
	offer->formats = realloc(offer->formats, size);
	offer->formats[offer->n_formats - 1] = strdup(mime_type);
}

static void data_offer_source_actions(void* data,
		struct wl_data_offer* wl_data_offer, uint32_t source_actions) {
	struct swa_data_offer_wl* offer = data;
	dlg_assert(offer->offer == wl_data_offer);

	if(source_actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) {
		offer->supported_actions |= swa_data_action_copy;
	}
	if(source_actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) {
		offer->supported_actions |= swa_data_action_move;
	}
}

static void data_offer_action(void* data, struct wl_data_offer* wl_data_offer,
		uint32_t dnd_action) {
	struct swa_data_offer_wl* offer = data;
	dlg_assert(offer->offer == wl_data_offer);

	switch(dnd_action) {
		case WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY:
			offer->action = swa_data_action_copy;
			break;
		case WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE:
			offer->action = swa_data_action_move;
			break;
		default:
			offer->action = swa_data_action_none;
			break;
	}
}

static const struct wl_data_offer_listener data_offer_listener = {
	.offer = data_offer_offer,
	.source_actions = data_offer_source_actions,
	.action = data_offer_action
};

static void data_offer_destroy(struct swa_data_offer* base) {
	struct swa_data_offer_wl* offer = get_data_offer_wl(base);

	// unlink
	if(offer->next) offer->next->prev = offer->prev;
	if(offer->prev) offer->prev->next = offer->next;
	if(offer->dpy && offer->dpy->data_offer_list == offer) {
		offer->dpy->data_offer_list = offer->next;
	}

	// TODO: finish dnd source if succesful

	if(offer->data.handler) {
		dlg_assert(offer->data.format);
		struct swa_exchange_data data = {0};
		offer->data.handler(&offer->base, offer->data.format, data);
	}
	if(offer->data.format) free((void*)offer->data.format);
	if(offer->data.bytes) free(offer->data.bytes);
	if(offer->data.fd) close(offer->data.fd);
	if(offer->data.io) pml_io_destroy(offer->data.io);

	if(offer->offer) wl_data_offer_destroy(offer->offer);
	for(unsigned i = 0u; i < offer->n_formats; ++i) {
		free((void*)offer->formats[i]);
	}
	free(offer->formats);
	free(offer);
}

static bool data_offer_formats(struct swa_data_offer* base, swa_formats_handler cb) {
	struct swa_data_offer_wl* offer = get_data_offer_wl(base);
	cb(base, offer->formats, offer->n_formats);
	return true;
}

static void data_pipe_cb(struct pml_io* io, unsigned revents) {
	(void) revents;

	struct swa_data_offer_wl* offer = pml_io_get_data(io);
	dlg_assert(offer->data.fd == pml_io_get_fd(io));

	// TODO: could use ioctl FIONREAD on most unixes for efficient
	//   buffer sizing
	// TODO: i guess we should be prepared for this offer to be destroyed
	//   by the callback. kinda hard to handle though
	unsigned readCount = 2048u;
	while(true) {
		unsigned size = offer->data.n_bytes;
		offer->data.bytes = realloc(offer->data.bytes, size + readCount);
		int ret = read(offer->data.fd, offer->data.bytes + size, readCount);
		size += readCount;

		if(ret == 0) {
			// other side closed, reading is finished
			// transfer ownership of the data to the application
			struct swa_exchange_data data = {
				.data = offer->data.bytes,
				.size = offer->data.n_bytes,
			};
			offer->data.handler(&offer->base, offer->data.format, data);

			// unset handler data
			pml_io_destroy(offer->data.io);
			close(offer->data.fd);
			free((void*) offer->data.format);
			memset(&offer->data, 0, sizeof(offer->data));
		} else if(ret < 0) {
			// EINTR: interrupted by signal, just try again
			// EAGAIN: no data available at the moment, continue polling,
			// i.e. break below. Other errors here are unexpected,
			// we cancel the data transfer
			if(errno == EINTR) {
				continue;
			} else if(errno != EAGAIN) {
				dlg_warn("read(pipe): %s (%d)", strerror(errno), errno);

				struct swa_exchange_data data = {0};
				offer->data.handler(&offer->base, offer->data.format, data);

				// unset handler data
				pml_io_destroy(offer->data.io);
				close(offer->data.fd);
				free(offer->data.bytes);
				free((void*) offer->data.format);
				memset(&offer->data, 0, sizeof(offer->data));
			}
		} else if((unsigned) ret < readCount) {
			// we finished reading avilable data
			// try again to see whether we get eof or EAGAIN now
			offer->data.n_bytes = size + ret;
			continue;
		} else if((unsigned) ret == readCount) {
			// more data might be avilable, continue
			// let read size grow exponentially for performance
			readCount *= 2;
			continue;
		} else {
			dlg_error("unreachable: ret = %d; readCount = %d",
				ret, readCount);
		}

		break;
	}
}

static bool data_offer_data(struct swa_data_offer* base, const char* format,
		swa_data_handler cb) {
	struct swa_data_offer_wl* offer = get_data_offer_wl(base);
	dlg_assert(!offer->data.handler);

	int fds[2];
	if(!swa_pipe(fds)) {
		return false;
	}

	wl_data_offer_receive(offer->offer, format, fds[1]);
	close(fds[1]);

	offer->data.n_bytes = 0;
	offer->data.fd = fds[0];
	offer->data.handler = cb;
	offer->data.format = strdup(format);
	offer->data.io = pml_io_new(offer->dpy->pml, fds[0],
		POLLIN, data_pipe_cb);
	pml_io_set_data(offer->data.io, offer);

	return true;
}
static void data_offer_set_preferred(struct swa_data_offer* base,
		const char* format, enum swa_data_action action) {
	struct swa_data_offer_wl* offer = get_data_offer_wl(base);
	dlg_assert(offer->dnd);
	wl_data_offer_accept(offer->offer, offer->dpy->dnd.serial, format);

	enum wl_data_device_manager_dnd_action wl_action =
		WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
	if(format) {
		switch(action) {
			case swa_data_action_copy:
				wl_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
				break;
			case swa_data_action_move:
				wl_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
				break;
			default:
				break;
		}
	}

	unsigned v = wl_data_offer_get_version(offer->offer);
	if(v > WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION) {
		wl_data_offer_set_actions(offer->offer, wl_action, wl_action);
	}
}
static enum swa_data_action data_offer_get_action(struct swa_data_offer* base) {
	struct swa_data_offer_wl* offer = get_data_offer_wl(base);
	return offer->action;
}
static enum swa_data_action data_offer_supported_actions(struct swa_data_offer* base) {
	struct swa_data_offer_wl* offer = get_data_offer_wl(base);
	return offer->supported_actions;
}

static const struct swa_data_offer_interface data_offer_impl = {
	.destroy = data_offer_destroy,
	.formats = data_offer_formats,
	.data = data_offer_data,
	.set_preferred = data_offer_set_preferred,
	.action = data_offer_get_action,
	.supported_actions = data_offer_supported_actions
};

static void data_dev_data_offer(void* data, struct wl_data_device* wl_data_dev,
		struct wl_data_offer* wl_offer) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->data_dev == wl_data_dev);

	struct swa_data_offer_wl* offer = calloc(1, sizeof(*offer));
	offer->base.impl = &data_offer_impl;
	offer->dpy = dpy;
	offer->offer = wl_offer;
	if(dpy->data_offer_list) {
		dpy->data_offer_list->prev = offer;
	}
	offer->next = dpy->data_offer_list;
	dpy->data_offer_list = offer;
	wl_data_offer_add_listener(wl_offer, &data_offer_listener, offer);
}

static void data_dev_enter(void* data, struct wl_data_device* wl_data_dev,
		uint32_t serial, struct wl_surface* surface,
		wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* offer) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->data_dev == wl_data_dev);

	struct swa_window_wl* win = wl_surface_get_user_data(surface);
	if(!win) {
		dlg_warn("dnd entered invalid surface");
		return;
	}
}

static void data_dev_leave(void* data, struct wl_data_device* wl_data_dev) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->data_dev == wl_data_dev);
}

static void data_dev_motion(void* data, struct wl_data_device* wl_data_dev,
		uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->data_dev == wl_data_dev);
}

static void data_dev_drop(void* data, struct wl_data_device* wl_data_dev) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->data_dev == wl_data_dev);
}

static void data_dev_selection(void* data, struct wl_data_device* wl_data_dev,
		struct wl_data_offer* wl_offer) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->data_dev == wl_data_dev);

	if(dpy->selection) {
		data_offer_destroy(&dpy->selection->base);
		dpy->selection = NULL;
	}

	if(wl_offer) {
		struct swa_data_offer_wl* offer = wl_data_offer_get_user_data(wl_offer);
		dlg_assert(offer);
		dpy->selection = offer;
	}
}

static const struct wl_data_device_listener data_dev_listener = {
	.data_offer = data_dev_data_offer,
	.enter = data_dev_enter,
	.leave = data_dev_leave,
	.motion = data_dev_motion,
	.drop = data_dev_drop,
	.selection = data_dev_selection,
};

static void display_destroy(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	dlg_assert(!dpy->focus);
	dlg_assert(!dpy->mouse_over);

	swa_xkb_finish(&dpy->xkb);
	for(struct swa_data_offer_wl* d = dpy->data_offer_list; d;) {
		struct swa_data_offer_wl* next = d->next;
		data_offer_destroy(&d->base);
		d = next;
	}

#ifdef SWA_WITH_GL
	if(dpy->egl) swa_egl_display_destroy(dpy->egl);
#endif

	if(dpy->wakeup_pipe_r) close(dpy->wakeup_pipe_r);
	if(dpy->wakeup_pipe_w) close(dpy->wakeup_pipe_w);
	if(dpy->io_source) pml_io_destroy(dpy->io_source);
	if(dpy->touch_points) free(dpy->touch_points);
	if(dpy->wl_queue) wl_event_queue_destroy(dpy->wl_queue);
	if(dpy->key_repeat.timer) pml_timer_destroy(dpy->key_repeat.timer);
	if(dpy->cursor.timer) pml_timer_destroy(dpy->cursor.timer);
	if(dpy->cursor.frame_callback) wl_callback_destroy(dpy->cursor.frame_callback);
	if(dpy->cursor.theme) wl_cursor_theme_destroy(dpy->cursor.theme);
	if(dpy->cursor.surface) wl_surface_destroy(dpy->cursor.surface);
	if(dpy->data_dev) wl_data_device_destroy(dpy->data_dev);
	if(dpy->shm) wl_shm_destroy(dpy->shm);
	if(dpy->keyboard) wl_keyboard_destroy(dpy->keyboard);
	if(dpy->pointer) wl_pointer_destroy(dpy->pointer);
	if(dpy->touch) wl_touch_destroy(dpy->touch);
	if(dpy->xdg_wm_base) xdg_wm_base_destroy(dpy->xdg_wm_base);
	if(dpy->decoration_manager) zxdg_decoration_manager_v1_destroy(dpy->decoration_manager);
	if(dpy->seat) wl_seat_destroy(dpy->seat);
	if(dpy->data_dev_manager) wl_data_device_manager_destroy(dpy->data_dev_manager);
	if(dpy->compositor) wl_compositor_destroy(dpy->compositor);
	if(dpy->registry) wl_registry_destroy(dpy->registry);
	if(dpy->display) wl_display_disconnect(dpy->display);
	if(dpy->pml) pml_destroy(dpy->pml);

	free((char*) dpy->appname);
	free(dpy);
}

static bool print_error(struct swa_display_wl* dpy, const char* fn) {
	// check for critical errors
	if(check_error(dpy)) {
		return false;
	}

	// otherwise output non-critical error
	dlg_error("%s: %s (%d)", fn, strerror(errno), errno);
	return true;
}

static bool display_dispatch(struct swa_display* base, bool block) {
	struct swa_display_wl* dpy = get_display_wl(base);

	// dispatch all buffered events. Those won't be detected by POLL
	// so without this we might block or return even though there are
	// events
	int res;
	while((res = wl_display_dispatch_pending(dpy->display)) > 0);
	if(res < 0) {
		return print_error(dpy, "wl_display_dispatch_pending");
	}

	if(wl_display_flush(dpy->display) == -1) {
		return print_error(dpy, "wl_display_flush");
	}

	pml_iterate(dpy->pml, block);
	return !dpy->error;
}

static void display_wakeup(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	int err = write(dpy->wakeup_pipe_w, " ", 1);

	// if the pipe is full, the waiting thread will wake up and clear
	// it and it doesn't matter that our write call failed
	if(err < 0 && errno != EAGAIN) {
		dlg_warn("Writing to wakeup pipe failed: %s", strerror(errno));
	}
}

static enum swa_display_cap display_capabilities(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	enum swa_display_cap caps =
#ifdef SWA_WITH_GL
		swa_display_cap_gl |
#endif
#ifdef SWA_WITH_VK
		swa_display_cap_vk |
#endif
		swa_display_cap_client_decoration;
	if(dpy->shm) caps |= swa_display_cap_buffer_surface;
	if(dpy->keyboard) caps |= swa_display_cap_keyboard;
	if(dpy->pointer) caps |= swa_display_cap_mouse;
	if(dpy->touch) caps |= swa_display_cap_touch;
	// TODO: implement dnd
	if(dpy->data_dev) caps |= /*swa_display_cap_dnd |*/ swa_display_cap_clipboard;
	// NOTE: we don't know this for sure. But it's at least worth
	// a shot in this case. And the final result should always be determined
	// using win_is_client_decorated anyways
	if(dpy->decoration_manager) caps |= swa_display_cap_server_decoration;
	return caps;
}

static const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
#ifdef SWA_WITH_VK
	static const char* names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
	};
	*count = sizeof(names) / sizeof(names[0]);
	return names;
#else
	dlg_warn("swa was compiled without vulkan suport");
	*count = 0;
	return NULL;
#endif
}

static bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->keyboard) {
		dlg_warn("display has no keyboard");
		return false;
	}

	const unsigned n_bits = 8 * sizeof(dpy->key_states);
	if(key >= n_bits) {
		dlg_warn("keycode not tracked (too high)");
		return false;
	}

	unsigned idx = key / 64;
	unsigned bit = key % 64;
	return (dpy->key_states[idx] & (((uint64_t) 1) << bit));
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->keyboard) {
		dlg_warn("display has no keyboard");
		return NULL;
	}

	return swa_xkb_key_name(&dpy->xkb, key);
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->keyboard) {
		dlg_warn("display has no keyboard");
		return swa_keyboard_mod_none;
	}

	return swa_xkb_modifiers(&dpy->xkb);
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->keyboard) {
		dlg_warn("display has no keyboard");
		return NULL;
	}

	return &dpy->focus->base;
}

static bool display_mouse_button_pressed(struct swa_display* base,
		enum swa_mouse_button button) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->pointer) {
		dlg_warn("display has no mouse");
		return NULL;
	}

	if(button >= 64) {
		dlg_warn("mouse button code not tracked (too high)");
		return false;
	}

	return (dpy->mouse_button_states & (1 << button));
}
static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->pointer) {
		dlg_warn("display has no mouse");
		return;
	}

	if(!dpy->mouse_over) {
		return;
	}

	*x = dpy->mouse_x;
	*y = dpy->mouse_y;
}
static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	if(!dpy->pointer) {
		dlg_warn("display has no mouse");
		return NULL;
	}

	return dpy->mouse_over ? &dpy->mouse_over->base : NULL;
}

static struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	struct swa_display_wl* dpy = get_display_wl(base);
	return &dpy->selection->base;
}

// TODO
static bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_wl* dpy = get_display_wl(base);
	return false;
}
static bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_wl* dpy = get_display_wl(base);
	return false;
}

static swa_gl_proc display_get_gl_proc_addr(struct swa_display* base,
		const char* name) {
#ifdef SWA_WITH_GL
	return (swa_gl_proc) eglGetProcAddress(name);
#else
	dlg_error("swa was built without gl support");
	return NULL;
#endif
}

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_wl* dpy = get_display_wl(base);
	struct swa_window_wl* win = calloc(1, sizeof(*win));
	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;
	win->width = settings->width;
	win->height = settings->height;

	win->wl_surface = wl_compositor_create_surface(dpy->compositor);
	wl_surface_set_user_data(win->wl_surface, win);

	win->xdg_surface = xdg_wm_base_get_xdg_surface(dpy->xdg_wm_base, win->wl_surface);
	xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
	win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
	xdg_toplevel_add_listener(win->xdg_toplevel, &toplevel_listener, win);

	if(settings->title) {
		xdg_toplevel_set_title(win->xdg_toplevel, settings->title);
	}
	if(dpy->appname) {
		xdg_toplevel_set_app_id(win->xdg_toplevel, dpy->appname);
	}

	if(settings->state != swa_window_state_normal &&
			settings->state != swa_window_state_none) {
		win_set_state(&win->base, settings->state);
	}

	// note how we always set the cursor, even if this is cursor_default
	// this is needed since when the pointer enters this surface we
	// *always* want to set the cursor, otherwise the previously
	// used cursor will just be used (which can be different every time)
	// which is not expected behavior.
	win_set_cursor(&win->base, settings->cursor);

	// commit the role so we get a configure event and can start drawing
	// also important for the decoration mode negotiation
	wl_surface_commit(win->wl_surface);

	// when the decoration protocol is not present, client side decorations
	// should be assumed on wayland
	win->decoration_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	if(dpy->decoration_manager) {
		win->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
			dpy->decoration_manager, win->xdg_toplevel);
		zxdg_toplevel_decoration_v1_add_listener(win->decoration,
			&decoration_listener, win);

		// set this to a special constant signaling that the actual
		// value is not known yet. We wait for the compositors response
		// below
		win->decoration_mode = SWA_DECORATION_MODE_PENDING;
		if(settings->client_decorate == swa_preference_yes) {
			zxdg_toplevel_decoration_v1_set_mode(win->decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		} else if(settings->client_decorate == swa_preference_no) {
			zxdg_toplevel_decoration_v1_set_mode(win->decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}

		// the compositor will notify us which decoration mode should
		// be used for the surface. But since after window creation,
		// the application might immediately (or at least
		// before dispatching events) check decoration mode, we have
		// to wait for the compositor respone here.
		// We use a custom queue for the roundtrip to not dispatch any
		// other events but reset the queue to the default queue afterwards.
		wl_proxy_set_queue((struct wl_proxy*) win->decoration,
			win->dpy->wl_queue);
		wl_display_roundtrip_queue(win->dpy->display, win->dpy->wl_queue);
		wl_proxy_set_queue((struct wl_proxy*) win->decoration, NULL);
		dlg_assert(win->decoration_mode != SWA_DECORATION_MODE_PENDING);
	} else if(settings->client_decorate == swa_preference_no) {
		dlg_warn("Can't set up server side decoration since "
			"the compositor doesn't support the decoration protocol");
	}

	// initializing the surface after having commited the role seems fitting
	win->surface_type = settings->surface;
	if(win->surface_type == swa_surface_buffer) {
		win->buffer.active = -1;
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		win->vk.instance = settings->surface_settings.vk.instance;
		if(!win->vk.instance) {
			dlg_error("No vulkan instance passed for vulkan window");
			goto err;
		}

		VkWaylandSurfaceCreateInfoKHR info = {0};
		info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
		info.display = win->dpy->display;
		info.surface = win->wl_surface;

		VkInstance instance = (VkInstance) win->vk.instance;
		VkSurfaceKHR surface;

		PFN_vkCreateWaylandSurfaceKHR fn = (PFN_vkCreateWaylandSurfaceKHR)
			vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR");
		if(!fn) {
			dlg_error("Failed to load 'vkCreateWaylandSurfaceKHR' function");
			goto err;
		}

		VkResult res = fn(instance, &info, NULL, &surface);
		if(res != VK_SUCCESS) {
			dlg_error("Failed to create vulkan surface: %d", res);
			goto err;
		}

		win->vk.surface = (uint64_t)surface;
#else
		dlg_error("swa was compiled without vulkan support");
		goto err;
#endif
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		if(!dpy->egl) {
			dpy->egl = swa_egl_display_create(EGL_PLATFORM_WAYLAND_EXT,
				dpy->display);
			if(!dpy->egl) {
				goto err;
			}
		}

		// TODO: maybe wait with creation until we received the first
		// configure event for the toplevel window?
		// if make current is called before that we could just fail
		// (or output a warning and create it with the fallback size).
		// But i guess if buffers are swapped before the first configure
		// event, it's an error anyways since that will attach an buffer
		// and commit.
		unsigned width = win->width == SWA_DEFAULT_SIZE ?
			SWA_FALLBACK_WIDTH : win->width;
		unsigned height = win->height == SWA_DEFAULT_SIZE ?
			SWA_FALLBACK_HEIGHT : win->height;
		win->gl.egl_window = wl_egl_window_create(win->wl_surface, width, height);

		const struct swa_gl_surface_settings* gls = &settings->surface_settings.gl;
		bool alpha = settings->transparent;
		EGLConfig config;
		EGLContext* ctx = &win->gl.context;
		if(!swa_egl_init_context(dpy->egl, gls, alpha, &config, ctx)) {
			goto err;
		}

		if(!(win->gl.surface = swa_egl_create_surface(dpy->egl,
				win->gl.egl_window, config, gls->srgb))) {
			goto err;
		}
#else
		dlg_error("swa was compiled without GL support");
		goto err;
#endif
	}

	return &win->base;

err:
	win_destroy(&win->base);
	return NULL;
}

static const struct swa_display_interface display_impl = {
	.destroy = display_destroy,
	.dispatch = display_dispatch,
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
	.get_gl_proc_addr = display_get_gl_proc_addr,
	.create_window = display_create_window,
};

static void decoration_configure(void *data,
		struct zxdg_toplevel_decoration_v1* deco, uint32_t mode) {
	struct swa_window_wl* win = data;
	if(win->decoration_mode != SWA_DECORATION_MODE_PENDING) {
		// The decoration protocol states that clients must respect these
		// events *at any time*. But swa doesn't (and shouldn't) have
		// an extra event when decoration mode changes. This could
		// therefore be problematic (when applications cache this state).
		// Maybe at least send a draw event or something?
		dlg_warn("Compositor changed decoration mode after initialization");
	}
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
	if(width == 0) {
		width = (win->width == SWA_DEFAULT_SIZE) ?
			SWA_FALLBACK_WIDTH : win->width;
	}
	if(height == 0) {
		height = (win->height == SWA_DEFAULT_SIZE) ?
			SWA_FALLBACK_HEIGHT : win->height;
	}

	bool resized = (win->width != (uint32_t)width) ||
		(win->height != (uint32_t)height);
	win->width = width;
	win->height = height;

	if(resized) {
		if(win->surface_type == swa_surface_gl) {
			dlg_assert(win->gl.egl_window);
			wl_egl_window_resize(win->gl.egl_window, width, height, 0, 0);
		}

		if(win->base.listener->resize) {
			win->base.listener->resize(&win->base, win->width, win->height);
		}
	}

	// refresh the window if the size changed or if it was never
	// drawn since this is the first configure event
	bool refresh = resized || !win->configured;
	win->configured = true;
	if(refresh) {
		win_refresh(&win->base);
	}

	// parse the provided states list.
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
		if(win->base.listener->state) {
			win->base.listener->state(&win->base, state);
		}
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	struct swa_window_wl* win = data;
	if(win->base.listener->close) {
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

static void touch_down(void* data, struct wl_touch* wl_touch, uint32_t serial,
		 uint32_t time, struct wl_surface* surface, int32_t id,
		 wl_fixed_t sx, wl_fixed_t sy) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->touch == wl_touch);

	struct swa_window_wl* win = wl_surface_get_user_data(surface);
	if(!win) {
		dlg_warn("Touch began on invalid window");
		return;
	}

	// add a new touch point
	// reallocate only if we have to
	unsigned i = dpy->n_touch_points;
	++dpy->n_touch_points;
	if(dpy->n_touch_points > dpy->capacity_touch_points) {
		unsigned size = dpy->n_touch_points * sizeof(*dpy->touch_points);
		dpy->touch_points = realloc(dpy->touch_points, size);
		dpy->capacity_touch_points = dpy->n_touch_points;
	}

	dpy->last_serial = serial;
	dpy->touch_points[i].id = id;
	dpy->touch_points[i].window = win;
	dpy->touch_points[i].x = wl_fixed_to_int(sx);
	dpy->touch_points[i].y = wl_fixed_to_int(sy);

	const struct swa_window_listener* listener =
		win->base.listener;
	if(listener && listener->touch_begin) {
		struct swa_touch_event ev = {
			.id = id,
			.x = dpy->touch_points[i].x,
			.y = dpy->touch_points[i].y,
		};
		listener->touch_begin(&win->base, &ev);
	}
}

static void touch_up(void* data, struct wl_touch* wl_touch, uint32_t serial,
		uint32_t time, int32_t id) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->touch == wl_touch);
	unsigned i = 0u;
	for(; i < dpy->n_touch_points; ++i) {
		if(dpy->touch_points[i].id == id) {
			break;
		}
	}

	if(i == dpy->n_touch_points) {
		dlg_warn("compositor sent invalid touch id %d", id);
		return;
	}

	dpy->last_serial = serial;
	dlg_assert(dpy->touch_points[i].window);
	const struct swa_window_listener* listener =
		dpy->touch_points[i].window->base.listener;
	if(listener && listener->touch_end) {
		listener->touch_end(&dpy->touch_points[i].window->base, id);
	}

	// erase the touch point
	--dpy->n_touch_points;
	memmove(dpy->touch_points + i, dpy->touch_points + i + 1,
		(dpy->n_touch_points - i) * sizeof(*dpy->touch_points));
}

static void touch_motion(void* data, struct wl_touch* wl_touch, uint32_t time,
			int32_t id, wl_fixed_t sx, wl_fixed_t sy) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->touch == wl_touch);
	unsigned i = 0u;
	for(; i < dpy->n_touch_points; ++i) {
		if(dpy->touch_points[i].id == id) {
			break;
		}
	}

	if(i == dpy->n_touch_points) {
		dlg_warn("compositor sent invalid touch id %d", id);
		return;
	}

	dlg_assert(dpy->touch_points[i].window);
	const struct swa_window_listener* listener =
		dpy->touch_points[i].window->base.listener;
	int x = wl_fixed_to_int(sx);
	int y = wl_fixed_to_int(sy);
	if(listener && listener->touch_update) {
		struct swa_touch_event ev = {
			.id = id,
			.x = x,
			.y = y,
		};
		listener->touch_update(&dpy->touch_points[i].window->base, &ev);
	}

	dpy->touch_points[i].x = x;
	dpy->touch_points[i].y = y;
}

static void touch_frame(void* data, struct wl_touch* wl_touch) {
	// no-op. We could forward this information in a more elaborate
	// touch input model
}

static void touch_cancel(void *data, struct wl_touch *wl_touch) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->touch == wl_touch);

	// This is somewhat tricky since we have multiple touch points,
	// possibly on multiple windows. We want to send the touch_cancel event
	// to each window listener with active touch points but not more
	// than once. We could use something elaborate like a hash set
	// here to track which listeners were already notified but usually
	// there is just a small number of touchpoints, we therefore accept O(n^2)
	// here.
	for(unsigned i = 0u; i < dpy->n_touch_points; ++i) {
		dlg_assert(dpy->touch_points[i].window);

		// check if window was alreday notified
		bool notified = false;
		for(unsigned j = 0u; j < i; ++j) {
			if(dpy->touch_points[j].window == dpy->touch_points[i].window) {
				notified = true;
				break;
			}
		}

		const struct swa_window_listener* listener =
			dpy->touch_points[i].window->base.listener;
		if(!notified && listener && listener->touch_cancel) {
			listener->touch_cancel(&dpy->touch_points[i].window->base);
		}
	}
	dpy->n_touch_points = 0;
}

static void touch_shape(void* data, struct wl_touch* wl_touch, int32_t id,
		  wl_fixed_t major, wl_fixed_t minor) {
	// no-op. We could forward this information in a more elaborate
	// touch input model
}

static void touch_orientation(void* data, struct wl_touch* wl_touch,
		int32_t id, wl_fixed_t orientation) {
	// no-op. We could forward this information in a more elaborate
	// touch input model
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


static void pointer_enter(void* data, struct wl_pointer* wl_pointer,
		uint32_t serial, struct wl_surface* surface, wl_fixed_t sx,
		wl_fixed_t sy) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->pointer == wl_pointer);
	dlg_assert(!dpy->mouse_over);

	struct swa_window_wl* win = wl_surface_get_user_data(surface);
	if(!win) {
		dlg_warn("Mouse entered invalid window");
		return;
	}

	dpy->mouse_over = win;
	dpy->mouse_enter_serial = serial;

	// set the windows associated cursor
	set_cursor(dpy, win);

	dpy->mouse_x = wl_fixed_to_int(sx);
	dpy->mouse_y = wl_fixed_to_int(sy);
	dpy->last_serial = serial;
	if(win->base.listener->mouse_cross) {
		struct swa_mouse_cross_event ev = {
			.x = dpy->mouse_x,
			.y = dpy->mouse_y,
			.entered = true,
		};
		win->base.listener->mouse_cross(&win->base, &ev);
	}
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->pointer == wl_pointer);
	struct swa_window_wl* win = wl_surface_get_user_data(surface);
	if(!win) {
		dlg_warn("Invalid window lost keyboard focus");
		return;
	}

	dlg_assert(dpy->mouse_over == win);
	dpy->mouse_over = NULL;

	dpy->last_serial = serial;
	if(win->base.listener->mouse_cross) {
		struct swa_mouse_cross_event ev = {
			.x = dpy->mouse_x,
			.y = dpy->mouse_y,
			.entered = false,
		};
		win->base.listener->mouse_cross(&win->base, &ev);
	}

	// unset cursor state
	dpy->mouse_enter_serial = 0;
	dpy->mouse_button_states = 0;
	dpy->cursor.active = NULL;
	dpy->cursor.redraw = false;
	pml_timer_disable(dpy->cursor.timer);
	if(dpy->cursor.frame_callback) {
		wl_callback_destroy(dpy->cursor.frame_callback);
		dpy->cursor.frame_callback = NULL;
	}
}

static void pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->pointer == wl_pointer);
	if(!dpy->mouse_over) {
		return;
	}

	int x = wl_fixed_to_int(sx);
	int y = wl_fixed_to_int(sy);
	const struct swa_window_listener* listener = dpy->mouse_over->base.listener;
	if(listener && listener->mouse_move) {
		struct swa_mouse_move_event ev = {
			.x = x,
			.y = y,
			.dx = x - dpy->mouse_x,
			.dy = y - dpy->mouse_y,
		};
		listener->mouse_move(&dpy->mouse_over->base, &ev);
	}
	dpy->mouse_x = x;
	dpy->mouse_y = y;
}

static enum swa_mouse_button linux_to_button(uint32_t buttoncode) {
	switch(buttoncode) {
		case BTN_LEFT: return swa_mouse_button_left;
		case BTN_RIGHT: return swa_mouse_button_right;
		case BTN_MIDDLE: return swa_mouse_button_middle;
		case BTN_SIDE: return swa_mouse_button_custom1;
		case BTN_EXTRA: return swa_mouse_button_custom2;
		case BTN_FORWARD: return swa_mouse_button_custom3;
		case BTN_BACK: return swa_mouse_button_custom4;
		case BTN_TASK: return swa_mouse_button_custom5;
		default: return swa_mouse_button_none;
	}
}

static void pointer_button(void* data, struct wl_pointer* wl_pointer,
		uint32_t serial, uint32_t time, uint32_t wl_button, uint32_t state) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->pointer == wl_pointer);
	if(!dpy->mouse_over) {
		return;
	}

	enum swa_mouse_button button = linux_to_button(wl_button);
	if(button == swa_mouse_button_none) {
		dlg_info("Unknown wayland mouse button: %d", button);
		return;
	}

	if(state) {
		dpy->mouse_button_states |= (uint64_t)(1 << button);
	} else {
		dpy->mouse_button_states &= ~(uint64_t)(1 << button);
	}

	dpy->last_serial = serial;
	const struct swa_window_listener* listener = dpy->mouse_over->base.listener;
	if(listener && listener->mouse_button) {
		struct swa_mouse_button_event ev = {
			.button = button,
			.pressed = state,
			.x = dpy->mouse_x,
			.y = dpy->mouse_y,
		};
		listener->mouse_button(&dpy->mouse_over->base, &ev);
	}
}

static void pointer_axis(void* data, struct wl_pointer* wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->pointer == wl_pointer);
	if(!dpy->mouse_over) {
		return;
	}

	float nvalue = wl_fixed_to_double(value) / 10.f;
	float dx = 0.f;
	float dy = 0.f;
	if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
		dx = nvalue;
	} else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		dy = nvalue;
	} else {
		dlg_info("Unsupported pointer axis %d", axis);
		return;
	}

	const struct swa_window_listener* listener = dpy->mouse_over->base.listener;
	if(listener && listener->mouse_wheel) {
		listener->mouse_wheel(&dpy->mouse_over->base, dx, dy);
	}
}

// Those events could be handled if more complex axis events are supported.
// Handling the frame event will only become useful when the 3 callbacks
// below are handled
static void pointer_frame(void* data, struct wl_pointer* wl_pointer) {
}

static void pointer_axis_source(void* data,
		struct wl_pointer* wl_pointer, uint32_t axis_source) {
}

static void pointer_axis_stop(void* data, struct wl_pointer* wl_pointer,
		uint32_t time, uint32_t axis) {
}

static void pointer_axis_discrete(void* data, struct wl_pointer* wl_pointer,
		uint32_t axis, int32_t discrete) {
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

static void keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->keyboard == wl_keyboard);
	if(format == WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP) {
		goto err_fd;
	}

	if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		dlg_warn("invalid keymap format");
		goto err_fd;
	}

	void* buf = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if(buf == MAP_FAILED) {
		dlg_warn("mmap failed: %s (%d)", strerror(errno), errno);
		goto err_fd;
	}

	struct xkb_keymap* keymap = xkb_keymap_new_from_buffer(dpy->xkb.context,
		buf, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if(!keymap) {
		dlg_warn("failed to compile the xkb keymap from compositor");
		goto err_mmap;
	}

	struct xkb_state* state = xkb_state_new(keymap);
	if(!state) {
		dlg_warn("failed to create xkb state from compositor keymap");
		xkb_keymap_unref(keymap);
		goto err_mmap;
	}

	if(dpy->xkb.state) xkb_state_unref(dpy->xkb.state);
	if(dpy->xkb.keymap) xkb_keymap_unref(dpy->xkb.keymap);
	dpy->xkb.keymap = keymap;
	dpy->xkb.state = state;

err_mmap:
	munmap(buf, size);
err_fd:
	close(fd);
}

static void keyboard_enter(void* data, struct wl_keyboard* wl_keyboard,
		uint32_t serial, struct wl_surface* surface, struct wl_array* keys) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->keyboard == wl_keyboard);
	struct swa_window_wl* win = wl_surface_get_user_data(surface);
	if(!win) {
		dlg_warn("Invalid window got keyboard focus");
		return;
	}

	dlg_assert(dpy->focus == NULL);
	dpy->focus = win;

	if(win->base.listener->focus) {
		win->base.listener->focus(&win->base, true);
	}

	// NOTE: we ignore keys (i.e. the keys pressed when entering) here
	// since that is the common behavior across all backends.
	(void) keys;
}

static void keyboard_leave(void* data, struct wl_keyboard* wl_keyboard,
		uint32_t serial, struct wl_surface* surface) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->keyboard == wl_keyboard);
	struct swa_window_wl* win = wl_surface_get_user_data(surface);
	if(!win) {
		dlg_warn("Invalid window lost keyboard focus");
		return;
	}

	dlg_assert(dpy->focus == win);
	dpy->focus = NULL;
	if(win->base.listener->focus) {
		win->base.listener->focus(&win->base, false);
	}

	// stop the repeat timer if it exists
	if(dpy->key_repeat.timer) {
		dpy->key_repeat.key = dpy->key_repeat.serial = 0;
		pml_timer_disable(dpy->key_repeat.timer);
	}

	memset(dpy->key_states, 0, sizeof(dpy->key_states));
}

static void keyboard_key(void* data, struct wl_keyboard* wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t pressed) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->keyboard == wl_keyboard);
	if(!dpy->focus) {
		return;
	}

	// store the key state in the local state
	unsigned idx = key / 64;
	unsigned bit = key % 64;
	if(pressed) {
		dpy->key_states[idx] |= ((uint64_t) 1) << bit;
	} else {
		dpy->key_states[idx] &= ~(((uint64_t) 1) << bit);
	}

	char* utf8 = NULL;
	if(pressed) {
		bool canceled;
		swa_xkb_key(&dpy->xkb, key + 8, &utf8, &canceled);
		// TODO: ring the bell when canceled?
	}

	dpy->last_serial = serial;
	if(dpy->focus->base.listener->key) {
		struct swa_key_event ev = {
			.keycode = key,
			.pressed = pressed,
			.utf8 = utf8,
			.repeated = false,
			.modifiers = swa_xkb_modifiers(&dpy->xkb),
		};
		dpy->focus->base.listener->key(&dpy->focus->base, &ev);
	}

	free(utf8);

	bool repeats = xkb_keymap_key_repeats(dpy->xkb.keymap, key + 8);
	if(pressed && dpy->key_repeat.timer && repeats) {
		dpy->key_repeat.key = key;
		dpy->key_repeat.serial = serial;

		struct timespec next;
		clock_gettime(CLOCK_MONOTONIC, &next);
		next.tv_nsec += 1000 * 1000 * dpy->key_repeat.delay;
		pml_timer_set_time(dpy->key_repeat.timer, next);
	} else if(!pressed && key == dpy->key_repeat.key) {
		dpy->key_repeat.key = dpy->key_repeat.serial = 0;
		pml_timer_disable(dpy->key_repeat.timer);
	}
}

static void keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->keyboard == wl_keyboard);
	swa_xkb_update_state(&dpy->xkb,
		(int[3]){mods_depressed, mods_latched, mods_locked},
		(int[3]){group, group, group});
}

static void key_repeat_cb(struct pml_timer* timer) {
	struct swa_display_wl* dpy = pml_timer_get_data(timer);
	dlg_assert(dpy->keyboard);
	dlg_assert(dpy->focus);

	char* utf8;
	bool canceled;

	// set the serial of the original key press here again
	dpy->last_serial = dpy->key_repeat.serial;
	swa_xkb_key(&dpy->xkb, dpy->key_repeat.key + 8, &utf8, &canceled);
	if(dpy->focus->base.listener->key) {
		struct swa_key_event ev = {
			.keycode = dpy->key_repeat.key,
			.pressed = true,
			.utf8 = utf8,
			.repeated = true,
			.modifiers = swa_xkb_modifiers(&dpy->xkb),
		};
		dpy->focus->base.listener->key(&dpy->focus->base, &ev);
		free(utf8);
	}

	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	if(dpy->key_repeat.rate == 1) {
		next.tv_sec += 1;
	} else {
		next.tv_nsec += (1000 * 1000 * 1000) / dpy->key_repeat.rate;
	}
	pml_timer_set_time(timer, next);
}

static void keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard,
		int32_t rate, int32_t delay) {
	struct swa_display_wl* dpy = data;
	dlg_assert(dpy->keyboard == wl_keyboard);
	dpy->key_repeat.rate = rate;
	dpy->key_repeat.delay = delay;
	if(rate > 0 && !dpy->key_repeat.timer) {
		dpy->key_repeat.timer = pml_timer_new(dpy->pml, NULL, key_repeat_cb);
		pml_timer_set_data(dpy->key_repeat.timer, dpy);
		pml_timer_set_clock(dpy->key_repeat.timer, CLOCK_MONOTONIC);
	}
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void seat_caps(void* data, struct wl_seat* seat, uint32_t caps) {
	struct swa_display_wl* dpy = data;
	if((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !dpy->keyboard) {
		// We couple xkb initialization to keyboard support so we can
		// rely on xkb being initialized in all keyboard callbacks
		if(!swa_xkb_init_default(&dpy->xkb) || !swa_xkb_init_compose(&dpy->xkb)) {
			dlg_warn("Failed to initialize xkb. No keyboard support");
			return;
		}

		dpy->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(dpy->keyboard, &keyboard_listener, dpy);
	} else if(!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && dpy->keyboard) {
		dlg_info("lost wl_keyboard");
		swa_xkb_finish(&dpy->xkb);
		pml_timer_destroy(dpy->key_repeat.timer);
		wl_keyboard_destroy(dpy->keyboard);
		dpy->keyboard = NULL;
		dpy->key_repeat.timer = NULL;
	}

	if((caps & WL_SEAT_CAPABILITY_POINTER) && !dpy->pointer) {
		dpy->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(dpy->pointer, &pointer_listener, dpy);

		// initialize cursor stuff if we additionally have shm
		if(dpy->shm) {
			if(!dpy->cursor.surface) {
				dpy->cursor.surface = wl_compositor_create_surface(dpy->compositor);
			}
			if(!dpy->cursor.theme) {
				const char* theme = getenv("XCURSOR_THEME");
				const char* size_str = getenv("XCURSOR_SIZE");
				unsigned size = 32u;
				if(size_str) {
					long s = strtol(size_str, NULL, 10);
					if(s <= 0) {
						dlg_warn("Invalid XCURSOR_SIZE: %s", size_str);
					} else {
						size = s;
					}
				}

				// if XCURSOR_THEME is not set, we pass in null, which will result
				// in the default cursor theme being used.
				dpy->cursor.theme = wl_cursor_theme_load(theme, size, dpy->shm);
			}
			if(!dpy->cursor.timer) {
				dpy->cursor.timer = pml_timer_new(dpy->pml, NULL, cursor_time_cb);
				pml_timer_set_data(dpy->cursor.timer, dpy);
				pml_timer_set_clock(dpy->cursor.timer, CLOCK_MONOTONIC);
			}
		}
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

static void seat_name(void* data, struct wl_seat* seat, const char* name) {
	dlg_debug("seat name: %s", name);
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

static unsigned min(unsigned a, unsigned b) {
	return a < b ? a : b;
}

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	(void) version;
	struct swa_display_wl* dpy = data;

	// protocol versions understood by swa for stable protocols
	static const unsigned v_compositor = 4u;
	static const unsigned v_dd_manager = 3u;
	static const unsigned v_seat = 6u;
	static const unsigned v_xdg_wm_base = 2u;

	if(!dpy->compositor &&
			strcmp(interface, wl_compositor_interface.name) == 0) {
		unsigned v = min(v_compositor, version);
		dpy->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, v);
	} else if(!dpy->shm && strcmp(interface, wl_shm_interface.name) == 0) {
		dpy->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if(!dpy->seat && strcmp(interface, wl_seat_interface.name) == 0) {
		unsigned v = min(v_seat, version);
		dpy->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
		wl_seat_add_listener(dpy->seat, &seat_listener, dpy);
	} else if(!dpy->xdg_wm_base &&
			strcmp(interface, xdg_wm_base_interface.name) == 0) {
		unsigned v = min(v_xdg_wm_base, version);
		dpy->xdg_wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, v);
		xdg_wm_base_add_listener(dpy->xdg_wm_base, &wm_base_listener, dpy);
	} else if(!dpy->data_dev_manager &&
			strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		unsigned v = min(v_dd_manager, version);
		dpy->data_dev_manager = wl_registry_bind(registry, name,
			&wl_data_device_manager_interface, v);
	} else if(!dpy->decoration_manager &&
			strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		dpy->decoration_manager = wl_registry_bind(registry, name,
			&zxdg_decoration_manager_v1_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// TODO: handle
	// not really sure what to do when something like compositor or shm
	// is gone though. I guess this is mainly for somewhat dynamic
	// globals like output? seat might probably go though, handle that!
	// requires us to track global names (the uint32_t passed when bound)
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void dispatch_display(struct pml_io* io, unsigned revents) {
	struct swa_display_wl* dpy = pml_io_get_data(io);
	if(revents & POLLIN) {
		while(wl_display_prepare_read(dpy->display) == -1) {
			if(wl_display_dispatch_pending(dpy->display) == -1) {
				print_error(dpy, "wl_display_dispatch_pending");
				return;
			}
		}
		if(wl_display_read_events(dpy->display) == -1) {
			print_error(dpy, "wl_display_read_events");
			return;
		}

		int res;
		while((res = wl_display_dispatch_pending(dpy->display)) > 0);
		if(res < 0) {
			print_error(dpy, "wl_display_dispatch_pending");
			return;
		}
	}
}

static void clear_wakeup(struct pml_io* io, unsigned revents) {
	char buf[128];
	int ret;
	int size = sizeof(buf);

	int fd = pml_io_get_fd(io);
	while((ret = read(fd, buf, size)) == size);

	if(ret < 0) {
		dlg_warn("Reading from wakeup pipe failed: %s", strerror(errno));
	}
}

struct swa_display* swa_display_wl_create(const char* appname) {
	errno = 0;
	struct wl_display* wld = wl_display_connect(NULL);
	if(!wld) {
		dlg_error("wl_display_connect: %s", strerror(errno));
		return NULL;
	}

	wl_log_set_handler_client(log_handler);

	struct swa_display_wl* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;
	dpy->display = wld;
	dpy->pml = pml_new();
	dpy->appname = strdup(appname ? appname : "swa");
	dpy->io_source = pml_io_new(dpy->pml, wl_display_get_fd(wld),
		POLLIN, dispatch_display);
	pml_io_set_data(dpy->io_source, dpy);

	// create wakeup pipes
	int fds[2];
	if(!swa_pipe(fds)) {
		goto error;
	}

	dpy->wakeup_pipe_r = fds[0];
	dpy->wakeup_pipe_w = fds[1];
	dpy->wakeup_io = pml_io_new(dpy->pml, dpy->wakeup_pipe_r,
		POLLIN, clear_wakeup);
	pml_io_set_data(dpy->wakeup_io, dpy);

	dpy->wl_queue = wl_display_create_queue(dpy->display);
	dpy->registry = wl_display_get_registry(dpy->display);
	wl_registry_add_listener(dpy->registry, &registry_listener, dpy);

	// We roundtrip here multiple times on the default queue, making sure
	// that all initialization is actually done. This adds some overhead
	// (especially with slow/busy compositors) but makes sure we e.g.
	// can report capabilities correctly.
	// Roundtripping on the default queue here is no problem, we can't
	// call any application callbacks yet.
	// 1: all globals are advertised to us and we added listeners
	wl_display_roundtrip(dpy->display);
	// 2: all global capabilities were advertised to us and we
	//    created keyboard/pointer/touch
	wl_display_roundtrip(dpy->display);
	// 3: those secondary resources received their initial listener
	//    information (such as keyboard keymap) as well.
	//    There is currently no instance where this information is
	//    needed immediately.
	// wl_display_roundtrip(dpy->display);

	// required wayland interfaces
	// only add interfaces without which swa can't really function at all
	// and for which checking existence again and again would be a huge pain
	const char* missing = NULL;
	if(!dpy->compositor) missing = "wl_compositor";

	if(missing) {
		dlg_error("Missing required wayland interface '%s'", missing);
		goto error;
	}

	if(dpy->data_dev_manager && dpy->seat) {
		dpy->data_dev = wl_data_device_manager_get_data_device(
			dpy->data_dev_manager, dpy->seat);
		wl_data_device_add_listener(dpy->data_dev, &data_dev_listener, dpy);
	}

	return &dpy->base;

error:
	display_destroy(&dpy->base);
	return NULL;
}
