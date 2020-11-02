#include <swa/private/x11.h>
#include <dlg/dlg.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/present.h>
#include <xcb/xinput.h>
#include <xcb/shm.h>
#include <xcb/xkb.h>

#include <xkbcommon/xkbcommon-x11.h>

#ifdef SWA_WITH_VK
  #define VK_USE_PLATFORM_XCB_KHR
  #ifndef SWA_WITH_LINKED_VK
	#define VK_NO_PROTOTYPES
  #endif // SWA_WITH_LINKED_VK
  #include <vulkan/vulkan.h>
#endif

#ifdef SWA_WITH_GL
  #include <swa/private/egl.h>
  #include <EGL/egl.h>
#endif

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;
static const unsigned max_prop_length = 0x1fffffff;

// from xcursor.c
const char* const* swa_get_xcursor_names(enum swa_cursor_type type);

// utility
static struct swa_display_x11* get_display_x11(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_x11*) base;
}

static struct swa_window_x11* get_window_x11(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_x11*) base;
}

#define handle_error(dpy, err, txt) do {\
	dlg_assert(err); \
	char buf[256]; \
	XGetErrorText(dpy->display, err->error_code, buf, sizeof(buf)); \
	dlg_error(txt ": %s (%d)", buf, err->error_code); \
	free(err); \
} while(0)


// window api
static void win_destroy(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
	if(!win->dpy) {
		free(win);
		return;
	}

	// destroy surface buffer
	if(win->surface_type == swa_surface_buffer) {
		if(win->buffer.shmseg) xcb_shm_detach(win->dpy->conn, win->buffer.shmseg);
		if(win->buffer.bytes) shmdt(win->buffer.bytes);
		if(win->buffer.shmid) shmctl(win->buffer.shmid, IPC_RMID, 0);
		if(win->buffer.gc) xcb_free_gc(win->dpy->conn, win->buffer.gc);
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		if(win->vk.surface) {
			dlg_assert(win->vk.instance);

			VkInstance instance = (VkInstance) win->vk.instance;
			VkSurfaceKHR surface = (VkSurfaceKHR) win->vk.surface;

			PFN_vkDestroySurfaceKHR fn = (PFN_vkDestroySurfaceKHR)
				win->vk.destroy_surface_pfn;
			if(fn) {
				fn(instance, surface, NULL);
			} else {
				dlg_error("Failed to load 'vkDestroySurfaceKHR' function");
			}
		}
#else
		dlg_error("swa was compiled without vk support; invalid surface");
#endif
	}

	struct swa_display_x11* dpy = win->dpy;
	if(win->next) win->next->prev = win->prev;
	if(win->prev) win->prev->next = win->next;
	if(win->dpy->window_list == win) {
		win->dpy->window_list = NULL;
	}

	if(win->dpy->keyboard.focus == win) win->dpy->keyboard.focus = NULL;
	if(win->dpy->mouse.over == win) win->dpy->mouse.over = NULL;

	if(win->window) xcb_destroy_window(dpy->conn, win->window);
	if(win->cursor) xcb_free_cursor(dpy->conn, win->cursor);
	if(win->colormap) xcb_free_colormap(dpy->conn, win->colormap);
	xcb_flush(win->dpy->conn);

	free(win);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
	return win->dpy->ewmh_caps |
		swa_window_cap_cursor |
		swa_window_cap_icon |
		swa_window_cap_minimize |
		swa_window_cap_size |
		swa_window_cap_size_limits |
		swa_window_cap_title |
		swa_window_cap_visibility;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_x11* win = get_window_x11(base);

	xcb_size_hints_t hints = {0};
	hints.min_width = w;
	hints.min_height = h;
	hints.flags = XCB_ICCCM_SIZE_HINT_P_MIN_SIZE;
	xcb_icccm_set_wm_normal_hints(win->dpy->conn, win->window, &hints);
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_x11* win = get_window_x11(base);

	xcb_size_hints_t hints = {0};
	hints.max_width = w;
	hints.max_height = h;
	hints.flags = XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
	xcb_icccm_set_wm_normal_hints(win->dpy->conn, win->window, &hints);
}

static void win_show(struct swa_window* base, bool show) {
	struct swa_window_x11* win = get_window_x11(base);
	if(show) {
		xcb_map_window(win->dpy->conn, win->window);
	} else {
		xcb_unmap_window(win->dpy->conn, win->window);
	}
}

static void win_set_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_x11* win = get_window_x11(base);
	uint32_t data[] = {w, h};
	xcb_configure_window(win->dpy->conn, win->window,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, data);
}

struct swa_x11_cursor {
	enum swa_cursor_type type;
	xcb_cursor_t cursor;
};

static xcb_cursor_t get_cursor(struct swa_display_x11* dpy,
		enum swa_cursor_type type) {
	for(unsigned i = 0u; i < dpy->n_cursors; ++i) {
		if(dpy->cursors[i].type == type) {
			return dpy->cursors[i].cursor;
		}
	}

	xcb_cursor_t cursor = 0;
	if(type == swa_cursor_none) {
		xcb_pixmap_t pixmap = xcb_generate_id(dpy->conn);
		xcb_create_pixmap(dpy->conn, 1, pixmap, dpy->dummy_window, 1, 1);
		cursor = xcb_generate_id(dpy->conn);
		xcb_create_cursor(dpy->conn, cursor, pixmap, pixmap,
			0, 0, 0, 0, 0, 0, 0, 0);
		xcb_free_pixmap(dpy->conn, pixmap);
	} else {
		// NOTE: the reason we use xcursor here (Xlib api) is because
		// xcb would require us to use xcb_cursor and xcb_render (for custom
		// image cursors). Both are probably not as widely
		// available/installed as libxcursor.
		// We should probably switch at some point though.
		const char* const* names = swa_get_xcursor_names(type);
		if(!names) {
			dlg_warn("failed to convert cursor type %d to xcursor", type);
			return cursor;
		}

		for(; *names; ++names) {
			cursor = XcursorLibraryLoadCursor(dpy->display, *names);
			if(cursor) {
				break;
			} else {
				dlg_debug("failed to retrieve xcursor %s", *names);
			}
		}
	}

	if(!cursor) {
		dlg_warn("Failed to create cursor for cursor type %d", type);
		return cursor;
	}

	++dpy->n_cursors;
	dpy->cursors = realloc(dpy->cursors, dpy->n_cursors * sizeof(*dpy->cursors));

	dpy->cursors[dpy->n_cursors - 1].cursor = cursor;
	dpy->cursors[dpy->n_cursors - 1].type = type;
	return cursor;
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_x11* win = get_window_x11(base);

	// for swa_cursor_default we simply unset the cursor (set it to XCB_NONE)
	// so that the parent cursor will be used
	xcb_cursor_t xcursor = XCB_NONE;
	bool owned = false;
	if(cursor.type == swa_cursor_image) {
		struct swa_image* img = &cursor.image;
		XcursorImage* xcimage = XcursorImageCreate(img->width, img->height);
		xcimage->xhot = cursor.hx;
		xcimage->yhot = cursor.hy;

		struct swa_image dst = {
			.width = img->width,
			.height = img->height,
			.stride = 4 * img->width,
			.data = (uint8_t*) xcimage->pixels,
			.format = swa_image_format_toggle_byte_word(swa_image_format_argb32),
		};

		swa_convert_image(img, &dst);
		xcursor = XcursorImageLoadCursor(win->dpy->display, xcimage);
		if(!xcursor) {
			dlg_warn("XcursorImageLoadCursor failed");
		}

		XcursorImageDestroy(xcimage);
		owned = true;
	} else if(cursor.type != swa_cursor_default) {
		xcursor = get_cursor(win->dpy, cursor.type);
	}

	if(win->cursor) {
		xcb_free_cursor(win->dpy->conn, win->cursor);
	}

	win->cursor = owned ? xcursor : 0;
	xcb_change_window_attributes(win->dpy->conn, win->window,
		XCB_CW_CURSOR, &xcursor);
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);

	if(win->dpy->ext.xpresent && win->present.pending) {
		win->present.redraw = true;
		return;
	}

	xcb_expose_event_t ev = {0};
	ev.response_type = XCB_EXPOSE;
	ev.window = win->window;
	xcb_send_event(win->dpy->conn, 0, win->window,
		XCB_EVENT_MASK_EXPOSURE, (const char*)&ev);
}

static void win_surface_frame(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);

	if(win->dpy->ext.xpresent && !win->present.pending) {
		if(!win->present.context) {
			win->present.context = xcb_generate_id(win->dpy->conn);
			xcb_present_select_input(win->dpy->conn, win->present.context,
				win->window, XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY);
		}

		dlg_debug("present_notify for target %lu", win->present.target_msc);
		xcb_present_notify_msc(win->dpy->conn, win->window,
			++win->present.serial,
			win->present.target_msc, 1, 0);
		win->present.pending = true;
	}

	// no-op otherwise, nothing we can do
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	struct swa_window_x11* win = get_window_x11(base);

	// first remove all relevant state atoms.
	// restoring an unminimized window isn't that easy. Seems to
	// usually work by setting _NET_ACTIVE_WINDOW but that might be
	// unexpected as well. Not sure what expected behavior is. We simply
	// don't support explicit restoring of windows.
	xcb_ewmh_wm_state_action_t action = (state == swa_window_state_fullscreen) ?
		XCB_EWMH_WM_STATE_REMOVE :
		XCB_EWMH_WM_STATE_ADD;
	xcb_ewmh_request_change_wm_state(&win->dpy->ewmh, 0, win->window, action,
		win->dpy->ewmh._NET_WM_STATE_FULLSCREEN, 0,
		XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);

	action = (state == swa_window_state_maximized) ?
		XCB_EWMH_WM_STATE_REMOVE :
		XCB_EWMH_WM_STATE_ADD;
	xcb_ewmh_request_change_wm_state(&win->dpy->ewmh, 0, win->window, action,
		win->dpy->ewmh._NET_WM_STATE_MAXIMIZED_HORZ,
		win->dpy->ewmh._NET_WM_STATE_MAXIMIZED_VERT,
		XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);

	// minimize = iconify
	// we could use XIconifyWindow, it does just this
	if(state == swa_window_state_minimized) {
		xcb_client_message_event_t event = {0};
		event.response_type = XCB_CLIENT_MESSAGE;
		event.window = win->window;
		event.type = win->dpy->atoms.wm_change_state;
		event.format = 32;
		event.data.data32[0] = XCB_ICCCM_WM_STATE_ICONIC;

		xcb_event_mask_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
			XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
		xcb_window_t root = win->dpy->screen->root;
		xcb_send_event(win->dpy->conn, false, root, mask, (const char*) &event);
	}
}

static void win_begin_move(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
	xcb_button_index_t index = XCB_BUTTON_INDEX_ANY;

	// if the display has an active mouse button set, this function was
	// called during the handling of a button press or release event.
	// We therefore ungrab the pointer since on button presses, we
	// get an implicit grab that prevents the moveresize action.
	if(win->dpy->mouse.button) {
		index = win->dpy->mouse.button;
		xcb_ungrab_pointer(win->dpy->conn, XCB_TIME_CURRENT_TIME);
	}

	xcb_ewmh_request_wm_moveresize(&win->dpy->ewmh, 0, win->window,
		win->dpy->mouse.x, win->dpy->mouse.y, XCB_EWMH_WM_MOVERESIZE_MOVE,
		index, XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);
}

static xcb_ewmh_moveresize_direction_t edge_to_x11(enum swa_edge edge) {
	switch(edge) {
		case swa_edge_top: return XCB_EWMH_WM_MOVERESIZE_SIZE_TOP;
		case swa_edge_bottom: return XCB_EWMH_WM_MOVERESIZE_SIZE_BOTTOM;
		case swa_edge_right: return XCB_EWMH_WM_MOVERESIZE_SIZE_RIGHT;
		case swa_edge_left: return XCB_EWMH_WM_MOVERESIZE_SIZE_LEFT;
		case swa_edge_top_right: return XCB_EWMH_WM_MOVERESIZE_SIZE_TOPRIGHT;
		case swa_edge_top_left: return XCB_EWMH_WM_MOVERESIZE_SIZE_TOPLEFT;
		case swa_edge_bottom_left: return XCB_EWMH_WM_MOVERESIZE_SIZE_BOTTOMLEFT;
		case swa_edge_bottom_right: return XCB_EWMH_WM_MOVERESIZE_SIZE_BOTTOMRIGHT;
		default: return 0;
	}
}

static void win_begin_resize(struct swa_window* base, enum swa_edge edges) {
	struct swa_window_x11* win = get_window_x11(base);
	xcb_ewmh_moveresize_direction_t action = edge_to_x11(edges);
	if(!action) {
		dlg_warn("Invalid edge: %d", edges);
		return;
	}

	xcb_button_index_t index = XCB_BUTTON_INDEX_ANY;

	// if the display has an active mouse button set, this function was
	// called during the handling of a button press or release event.
	// We therefore ungrab the pointer since on button presses, we
	// get an implicit grab that prevents the moveresize action.
	if(win->dpy->mouse.button) {
		index = win->dpy->mouse.button;
		xcb_ungrab_pointer(win->dpy->conn, XCB_TIME_CURRENT_TIME);
	}

	xcb_ewmh_request_wm_moveresize(&win->dpy->ewmh, 0, win->window,
		win->dpy->mouse.x, win->dpy->mouse.y, action,
		index, XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);
}

static void win_set_title(struct swa_window* base, const char* title) {
	struct swa_window_x11* win = get_window_x11(base);
	xcb_ewmh_set_wm_name(&win->dpy->ewmh, win->window, strlen(title), title);
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
	struct swa_window_x11* win = get_window_x11(base);
	if(img && img->data) {
		size_t count = 2 + img->width * img->height * 4;
		uint32_t* data = malloc(count);
		data[0] = img->width;
		data[1] = img->height;

		struct swa_image dst = {
			.width = img->width,
			.height = img->height,
			.stride = 4 * img->width,
			.data = (uint8_t*) (data + 2),
			.format = swa_image_format_toggle_byte_word(swa_image_format_rgba32),
		};

		swa_convert_image(img, &dst);
		xcb_ewmh_set_wm_icon(&win->dpy->ewmh, XCB_PROP_MODE_REPLACE,
			win->window, count, data);
		free(data);
	} else {
		uint32_t buffer[2] = {0};
		xcb_ewmh_set_wm_icon(&win->dpy->ewmh, XCB_PROP_MODE_REPLACE,
			win->window, 2, buffer);
	}
}

static bool win_is_client_decorated(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
	return win->client_decorated;
}

static uint64_t win_get_vk_surface(struct swa_window* base) {
#ifdef SWA_WITH_VK
	struct swa_window_x11* win = get_window_x11(base);
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
	struct swa_window_x11* win = get_window_x11(base);
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
	struct swa_window_x11* win = get_window_x11(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);

	win_surface_frame(&win->base);
	return eglSwapBuffers(win->dpy->egl->display, win->gl.surface);
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
#ifdef SWA_WITH_GL
	dlg_error("unimplemented");
	return false;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	struct swa_window_x11* win = get_window_x11(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return false;
	}

	struct swa_x11_buffer_surface* buf = &win->buffer;
	if(buf->active) {
		dlg_error("There is already an active buffer");
		return false;
	}

	xcb_connection_t* conn = win->dpy->conn;

	// check if we have to recreate the buffer
	unsigned fmt_size = swa_image_format_size(win->buffer.format);
	unsigned stride = win->width * fmt_size;
	unsigned m = stride % win->buffer.scanline_align;
	if(m) {
		stride += (win->buffer.scanline_align - m);
	}
	uint64_t n_bytes = win->height * stride;
	if(n_bytes > win->buffer.n_bytes) {
		buf->n_bytes = n_bytes * 4; // overallocate for resizing
		if(win->dpy->ext.shm) {
			if(buf->shmseg) {
				xcb_shm_detach(conn, buf->shmseg);
				shmdt(win->buffer.bytes);
				shmctl(win->buffer.shmid, IPC_RMID, 0);
			}

			buf->shmid = shmget(IPC_PRIVATE, buf->n_bytes, IPC_CREAT | 0777);
			buf->bytes = shmat(buf->shmid, 0, 0);
			buf->shmseg = xcb_generate_id(conn);
			xcb_shm_attach(conn, buf->shmseg, buf->shmid, 0);
		} else {
			buf->bytes = malloc(n_bytes);
		}
	}

	buf->active = true;
	img->data = buf->bytes;
	img->format = buf->format;
	img->width = win->width;
	img->height = win->height;
	img->stride = stride;

	return true;
}

static void win_apply_buffer(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return;
	}

	struct swa_x11_buffer_surface* buf = &win->buffer;
	if(!buf->active) {
		dlg_error("Window has no active buffer");
		return;
	}

	win_surface_frame(base);

	buf->active = false;
	xcb_void_cookie_t cookie = xcb_shm_put_image_checked(win->dpy->conn,
		win->window, buf->gc, win->width, win->height, 0, 0,
		win->width, win->height, 0, 0, win->depth,
		XCB_IMAGE_FORMAT_Z_PIXMAP, 0, buf->shmseg, 0);
	xcb_generic_error_t* err = xcb_request_check(win->dpy->conn, cookie);
	if(err) {
		handle_error(win->dpy, err, "xcb_shm_put_image");
		return;
	}
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
static void display_destroy(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
	if(!dpy->display) {
		free(dpy);
		return;
	}

	dlg_assertm(!dpy->window_list, "Still windows left");
	dlg_assert(dpy->conn || !dpy->n_cursors);

	for(unsigned i = 0u; i < dpy->n_cursors; ++i) {
		xcb_free_cursor(dpy->conn, dpy->cursors[i].cursor);
	}

	free(dpy->cursors);
	swa_xkb_finish(&dpy->keyboard.xkb);
	if(dpy->next_event) free(dpy->next_event);
	xcb_ewmh_connection_wipe(&dpy->ewmh);

	if(dpy->conn) xcb_flush(dpy->conn);
	if(dpy->display) XCloseDisplay(dpy->display);
	free(dpy);
}

static struct swa_window_x11* find_window(struct swa_display_x11* dpy,
		xcb_window_t xcb_win) {
	struct swa_window_x11* win = dpy->window_list;
	while(win) {
		if(win->window == xcb_win) {
			return win;
		}
		win = win->next;
	}

	return NULL;
}

static void handle_present_event(struct swa_display_x11* dpy,
		xcb_present_generic_event_t* ev) {
	switch(ev->evtype) {
	case XCB_PRESENT_COMPLETE_NOTIFY: {
		xcb_present_complete_notify_event_t* complete =
			(xcb_present_complete_notify_event_t*) ev;
		struct swa_window_x11* win = find_window(dpy, complete->window);
		if(win) {
			if(win->present.context != complete->event) {
				break;
			}

			dlg_debug("complete.msc: %lu, target_msc: %lu",
				complete->msc, win->present.target_msc);
			if(complete->msc < win->present.target_msc) {
				break;
			}

			// Older event or doesn't come from us but from another api
			if(complete->serial != win->present.serial) {
				break;
			}

			win->present.target_msc = complete->msc + 1; // for next frame
			win->present.pending = false;
			if(win->present.redraw) {
				win->present.redraw = false;
				if(win->base.listener->draw) {
					win->base.listener->draw(&win->base);
				}
			}
		}
		break;
	}
	}
}

static void handle_xinput_event(struct swa_display_x11* dpy,
		xcb_ge_generic_event_t* gev) {
	struct swa_window_x11* win;
	// no matter the event type, always has the same basic layout
	xcb_input_touch_begin_event_t* tev =
		(xcb_input_touch_begin_event_t*)(gev);
	if((win = find_window(dpy, tev->event)) && win->base.listener) {
		const float fp16 = 65536.f;
		float x = tev->event_x / fp16;
		float y = tev->event_y / fp16;
		unsigned id = tev->detail;
		switch(gev->event_type) {
		case XCB_INPUT_TOUCH_BEGIN:
			if(win->base.listener->touch_begin) {
				struct swa_touch_event ev = {
					.id = id,
					.x = x,
					.y = y,
				};
				win->base.listener->touch_begin(&win->base, &ev);
			} return;
		case XCB_INPUT_TOUCH_UPDATE:
			if(win->base.listener->touch_update) {
				struct swa_touch_event ev = {
					.id = id,
					.x = x,
					.y = y,
				};
				win->base.listener->touch_update(&win->base, &ev);
			}
			return;
		 case XCB_INPUT_TOUCH_END:
			if(win->base.listener->touch_end)
				win->base.listener->touch_end(&win->base, id);
			return;
	}
	}
}

static enum swa_mouse_button x11_to_button_and_wheel(unsigned detail,
		float* wheel_x, float* wheel_y) {
	switch(detail) {
		case 1: return swa_mouse_button_left;
		case 2: return swa_mouse_button_middle;
		case 3: return swa_mouse_button_right;
		case 4: *wheel_y = 1.f; return swa_mouse_button_none;
		case 5: *wheel_y = -1.f; return swa_mouse_button_none;
		case 6: *wheel_x = 1.f; return swa_mouse_button_none;
		case 7: *wheel_x = -1.f; return swa_mouse_button_none;
		case 8: return swa_mouse_button_custom1;
		case 9: return swa_mouse_button_custom2;
		default: return swa_mouse_button_none;
	}
}

static bool init_keymap(struct swa_display_x11* dpy) {
	int flags = XKB_KEYMAP_COMPILE_NO_FLAGS;
	int32_t dev = dpy->keyboard.device_id;

	struct xkb_keymap* keymap = xkb_x11_keymap_new_from_device(
		dpy->keyboard.xkb.context, dpy->conn, dev, flags);
	if(!keymap) {
		dlg_error("xkb_x11_keymap_new_from_device failed");
		return false;
	}

	struct xkb_state* state = xkb_x11_state_new_from_device(keymap,
		dpy->conn, dev);
	if(!state) {
		dlg_error("xkb_x11_state_new_from_device failed");
		return false;
	}

	struct swa_xkb_context* xkb = &dpy->keyboard.xkb;
	if(xkb->keymap) xkb_keymap_unref(xkb->keymap);
	if(xkb->state) xkb_state_unref(xkb->state);
	xkb->keymap = keymap;
	xkb->state = state;
	return true;
}

static void handle_event(struct swa_display_x11* dpy,
		const xcb_generic_event_t* ev) {
	unsigned type = ev->response_type & ~0x80;
	struct swa_window_x11* win;

	switch(type) {
	case XCB_EXPOSE: {
		xcb_expose_event_t* expose = (xcb_expose_event_t*) ev;
		if((win = find_window(dpy, expose->window))) {
			// When we receive the first expose even for this window
			// we just assume that no resize event - forcing it into
			// a different size - will come.
			// This is just a guess and no guarantee.
			if(win->init_size_pending) {
				win->init_size_pending = false;
				if(win->base.listener->resize) {
					win->base.listener->resize(&win->base,
						win->width, win->height);
				}
			}

			if(win->base.listener->draw) {
				if(win->present.pending) {
					win->present.redraw = true;
				} else {
					win->present.redraw = false;
					win->base.listener->draw(&win->base);
				}
			}
		}
		break;
	} case XCB_CONFIGURE_NOTIFY: {
		xcb_configure_notify_event_t* configure =
			(xcb_configure_notify_event_t*) ev;
		// TODO: check states and send state event if needed?
		if((win = find_window(dpy, configure->window))) {
			if(win->init_size_pending ||
					win->width != configure->width ||
					win->height != configure->height) {
				win->init_size_pending = false;
				win->width = configure->width;
				win->height = configure->height;
				if(win->base.listener->resize) {
					win->base.listener->resize(&win->base, win->width, win->height);
				}
			}
		}
		// we don't have to draw, the xserver will send an expose event
		break;
	} case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t* client = (xcb_client_message_event_t*) ev;
		unsigned protocol = client->data.data32[0];

		if(protocol == dpy->ewmh._NET_WM_PING) {
			xcb_ewmh_send_wm_ping(&dpy->ewmh, dpy->screen->root,
				client->data.data32[1]);
		} else if(protocol == dpy->atoms.wm_delete_window) {
			win = find_window(dpy, client->window);
			if(win->base.listener->close) {
				win->base.listener->close(&win->base);
			}
		}
		break;
	} case XCB_MOTION_NOTIFY: {
		xcb_motion_notify_event_t* motion = (xcb_motion_notify_event_t*) ev;
		if((win = find_window(dpy, motion->event))) {
			dlg_assert(win == dpy->mouse.over);
			if(win->base.listener->mouse_move) {
				struct swa_mouse_move_event lev;
				lev.x = motion->event_x;
				lev.y = motion->event_y;
				lev.dx = lev.x - dpy->mouse.x;
				lev.dy = lev.y - dpy->mouse.y;
				win->base.listener->mouse_move(&win->base, &lev);
			}
			dpy->mouse.x = motion->event_x;
			dpy->mouse.y = motion->event_y;
		}
		break;
	} case XCB_BUTTON_PRESS: {
		xcb_button_press_event_t* bev = (xcb_button_press_event_t*) ev;
		if((win = find_window(dpy, bev->event))) {
			dlg_assert(win == dpy->mouse.over);
			float sx = 0.f;
			float sy = 0.f;
			enum swa_mouse_button button = x11_to_button_and_wheel(bev->detail,
				&sx, &sy);
			dpy->mouse.button_states |= (1ul << (unsigned) button);

			if((sx != 0.f || sy != 0.f) && win->base.listener->mouse_wheel) {
				win->base.listener->mouse_wheel(&win->base, sx, sy);
			} else if(button != swa_mouse_button_none &&
					win->base.listener->mouse_button) {
				// See begin_move and begin_resize functions
				dpy->mouse.button = bev->detail;
				struct swa_mouse_button_event lev;
				lev.button = button;
				lev.pressed = true;
				lev.x = bev->event_x;
				lev.y = bev->event_y;
				win->base.listener->mouse_button(&win->base, &lev);
				dpy->mouse.button = 0;
			}
		}

		break;
	} case XCB_BUTTON_RELEASE: {
		xcb_button_release_event_t* bev = (xcb_button_release_event_t*) ev;
		if((win = find_window(dpy, bev->event))) {
			dlg_assert(dpy->mouse.over == win);
			float sx = 0.f;
			float sy = 0.f;
			enum swa_mouse_button button = x11_to_button_and_wheel(bev->detail,
				&sx, &sy);
			dpy->mouse.button_states &= ~(1ul << (unsigned) button);
			if(button != swa_mouse_button_none &&
					win->base.listener->mouse_button) {
				// See begin_move and begin_resize functions
				dpy->mouse.button = bev->detail;
				struct swa_mouse_button_event lev;
				lev.button = button;
				lev.pressed = false;
				lev.x = bev->event_x;
				lev.y = bev->event_y;
				win->base.listener->mouse_button(&win->base, &lev);
				dpy->mouse.button = 0;
			}
		}

		break;
	} case XCB_ENTER_NOTIFY: {
		xcb_enter_notify_event_t* eev = (xcb_enter_notify_event_t*) ev;
		if(eev->mode == XCB_NOTIFY_MODE_GRAB ||
				eev->mode == XCB_NOTIFY_MODE_UNGRAB) {
			return;
		}

		dlg_assert(!dpy->mouse.over);
		if((win = find_window(dpy, eev->event))) {
			dpy->mouse.over = win;
			if(win->base.listener->mouse_cross) {
				struct swa_mouse_cross_event lev;
				lev.entered = true;
				lev.x = eev->event_x;
				lev.y = eev->event_y;
				win->base.listener->mouse_cross(&win->base, &lev);
			}
		}
		break;
	} case XCB_LEAVE_NOTIFY: {
		xcb_leave_notify_event_t* eev = (xcb_leave_notify_event_t*) ev;
		if(eev->mode == XCB_NOTIFY_MODE_GRAB ||
				eev->mode == XCB_NOTIFY_MODE_UNGRAB) {
			return;
		}

		if((win = find_window(dpy, eev->event))) {
			dlg_assert(dpy->mouse.over == win);
			dpy->mouse.over = NULL;
			if(win->base.listener->mouse_cross) {
				struct swa_mouse_cross_event lev;
				lev.entered = false;
				lev.x = eev->event_x;
				lev.y = eev->event_y;
				win->base.listener->mouse_cross(&win->base, &lev);
			}
		}
		break;

	} case XCB_FOCUS_IN: {
		xcb_focus_in_event_t* fev = (xcb_focus_in_event_t*) ev;
		if(fev->mode == XCB_NOTIFY_MODE_GRAB ||
				fev->mode == XCB_NOTIFY_MODE_UNGRAB) {
			return;
		}

		dlg_assert(!dpy->keyboard.focus);
		if((win = find_window(dpy, fev->event))) {
			dpy->keyboard.focus = win;
			if(win->base.listener->focus) {
				win->base.listener->focus(&win->base, true);
			}
		}
		break;
	} case XCB_FOCUS_OUT: {
		xcb_focus_out_event_t* fev = (xcb_focus_out_event_t*) ev;
		if(fev->mode == XCB_NOTIFY_MODE_GRAB ||
				fev->mode == XCB_NOTIFY_MODE_UNGRAB) {
			return;
		}

		if((win = find_window(dpy, fev->event))) {
			dlg_assert(dpy->keyboard.focus == win);
			dpy->keyboard.focus = NULL;
			if(win->base.listener->focus) {
				win->base.listener->focus(&win->base, false);
			}
		}
		break;
	} case XCB_KEY_PRESS: {
		xcb_key_press_event_t* kev = (xcb_key_press_event_t*) ev;
		if(!(win = find_window(dpy, kev->event))) {
			dpy->keyboard.repeated = false;
			break;
		}

		enum swa_key key = kev->detail - 8;

		// store the key state in the local state
		unsigned idx = key / 64;
		unsigned bit = key % 64;
		dpy->keyboard.key_states[idx] |= ((uint64_t) 1) << bit;

		char* utf8 = NULL;
		bool canceled;
		swa_xkb_key(&dpy->keyboard.xkb, kev->detail, &utf8, &canceled);

		dlg_assert(win == dpy->keyboard.focus);
		if(win->base.listener->key) {
			struct swa_key_event lev = {
				.keycode = key,
				.pressed = true,
				.utf8 = utf8,
				.repeated = dpy->keyboard.repeated,
				.modifiers = swa_xkb_modifiers(&dpy->keyboard.xkb),
			};
			win->base.listener->key(&win->base, &lev);
		}

		free(utf8);
		dpy->keyboard.repeated = false;
		break;
	}
	case XCB_KEY_RELEASE: {
		xcb_key_release_event_t* kev = (xcb_key_release_event_t*) ev;
		if(!(win = find_window(dpy, kev->event))) {
			break;
		}

		// check for repeat
		struct xcb_key_press_event_t* kp =
			(struct xcb_key_press_event_t*) dpy->next_event;
		if(kp && (kp->response_type & ~0x80) == XCB_KEY_PRESS) {
			if(kev->time == kp->time && kev->detail == kp->detail) {
				// just ignore this event
				dpy->keyboard.repeated = true;
				break;
			}
		}

		enum swa_key key = kev->detail - 8;

		// store the key state in the local state
		unsigned idx = key / 64;
		unsigned bit = key % 64;
		dpy->keyboard.key_states[idx] &= ~(((uint64_t) 1) << bit);

		if(win->base.listener->key) {
			struct swa_key_event lev = {
				.keycode = key,
				.pressed = false,
				.utf8 = NULL,
				.repeated = false,
				.modifiers = swa_xkb_modifiers(&dpy->keyboard.xkb),
			};
			win->base.listener->key(&win->base, &lev);
		}
		break;
	} case XCB_GE_GENERIC: {
		xcb_ge_generic_event_t* gev = (xcb_ge_generic_event_t*)ev;
		if(gev->extension == dpy->ext.xinput) {
			handle_xinput_event(dpy, gev);
		} else if(gev->extension == dpy->ext.xpresent) {
			handle_present_event(dpy, (xcb_present_generic_event_t*) gev);
		}

		break;
	} case 0u: {
		xcb_generic_error_t* eev = (xcb_generic_error_t*) ev;
		int code = eev->error_code;
		char buf[256];
		XGetErrorText(dpy->display, code, buf, sizeof(buf));
		dlg_error("retrieved x11 error code: %s (%d)", buf, code);
		break;
	} default:
		break;
	}

	if(dpy->ext.xkb && ev->response_type == dpy->ext.xkb) {
		union xkb_event {
			struct {
				uint8_t response_type;
				uint8_t xkb_type;
				uint16_t sequence;
				xcb_timestamp_t time;
				uint8_t device_id;
			} any;
			xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
			xcb_xkb_map_notify_event_t map_notify;
			xcb_xkb_state_notify_event_t state_notify;
		};

		union xkb_event* xkbev = (union xkb_event*) ev;
		if(xkbev->any.device_id == dpy->keyboard.device_id) {
			switch(xkbev->any.xkb_type) {
			case XCB_XKB_NEW_KEYBOARD_NOTIFY:
				if(xkbev->new_keyboard_notify.changed & XCB_XKB_NKN_DETAIL_KEYCODES)
					init_keymap(dpy);
				break;
			case XCB_XKB_MAP_NOTIFY:
				init_keymap(dpy);
				break;
			case XCB_XKB_STATE_NOTIFY:
				xkb_state_update_mask(dpy->keyboard.xkb.state,
					xkbev->state_notify.baseMods,
					xkbev->state_notify.latchedMods,
					xkbev->state_notify.lockedMods,
					xkbev->state_notify.baseGroup,
					xkbev->state_notify.latchedGroup,
					xkbev->state_notify.lockedGroup);
				break;
			}
		}
	}
}

static bool check_error(struct swa_display_x11* dpy) {
	int err = xcb_connection_has_error(dpy->conn);
	if(!err) {
		return false;
	}

	const char* name = "<unknown error>";
	#define ERR_CASE(x) case x: name = #x; break;
	switch(err) {
		ERR_CASE(XCB_CONN_ERROR);
		ERR_CASE(XCB_CONN_CLOSED_EXT_NOTSUPPORTED);
		ERR_CASE(XCB_CONN_CLOSED_REQ_LEN_EXCEED);
		ERR_CASE(XCB_CONN_CLOSED_PARSE_ERR);
		ERR_CASE(XCB_CONN_CLOSED_INVALID_SCREEN);
		default: break;
	}
	#undef ERR_CASE

	dlg_error("Critical xcb error: %s (%d)", name, err);
	return dpy->error = true;
}

static bool display_dispatch(struct swa_display* base, bool block) {
	struct swa_display_x11* dpy = get_display_x11(base);
	if(check_error(dpy)) {
		return false;
	}

	// when processing an event, we always have the next event ready
	// as well (if there already is one) since this is useful
	// in some cases, e.g. the only way to determine whether
	// a key press is a repeat

	xcb_flush(dpy->conn);
	if(block && !dpy->next_event) {
		dpy->next_event = xcb_wait_for_event(dpy->conn);
		if(!dpy->next_event) {
			dlg_warn("xcb_wait_for_event failed");
			return !check_error(dpy);
		}
	}

	while(true) {
		xcb_generic_event_t* event;
		if(dpy->next_event) {
			event = dpy->next_event;
			dpy->next_event = NULL;
		} else if(!(event = xcb_poll_for_event(dpy->conn))) {
			break;
		}

		dpy->next_event = xcb_poll_for_event(dpy->conn);
		handle_event(dpy, event);
		xcb_flush(dpy->conn);
		free(event);
	}

	return !check_error(dpy);
}

// We can implement this function simply using an xserver roundtrip
// and xcb since the library is threadsafe by design.
// Would be slightly more efficient using an eventfd and a custom
// mainloop (we could do that using pml) though.
static void display_wakeup(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);

	xcb_client_message_event_t ev = {0};
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 8;
	xcb_send_event(dpy->conn, 0, dpy->dummy_window, 0, (const char*) &ev);

	// flushing here is important, if the main thread is sleeping
	// it won't be doing it
	xcb_flush(dpy->conn);
}

static enum swa_display_cap display_capabilities(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
	enum swa_display_cap caps =
#ifdef SWA_WITH_GL
		swa_display_cap_gl |
#endif
#ifdef SWA_WITH_VK
		swa_display_cap_vk |
#endif
		// TODO: only depends this if wm supports motif wm hints?
		// would depend on hardcoded list of wms though, can't be queried...
		swa_display_cap_client_decoration |
		swa_display_cap_server_decoration |
		swa_display_cap_keyboard |
		swa_display_cap_mouse |
		// TODO: implement data exchange
		// swa_display_cap_dnd |
		// swa_display_cap_clipboard |
		swa_display_cap_buffer_surface;
	if(dpy->ext.xinput) caps |= swa_display_cap_touch;
	return caps;
}

static const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
#ifdef SWA_WITH_VK
	static const char* names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_XCB_SURFACE_EXTENSION_NAME,
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
	struct swa_display_x11* dpy = get_display_x11(base);
	const unsigned n_bits = 8 * sizeof(dpy->keyboard.key_states);
	if(key >= n_bits) {
		dlg_warn("keycode not tracked (too high)");
		return false;
	}

	unsigned idx = key / 64;
	unsigned bit = key % 64;
	return (dpy->keyboard.key_states[idx] & (((uint64_t) 1) << bit));
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_x11* dpy = get_display_x11(base);
	return swa_xkb_key_name(&dpy->keyboard.xkb, key);
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
	return swa_xkb_modifiers(&dpy->keyboard.xkb);
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
	return &dpy->keyboard.focus->base;
}

static bool display_mouse_button_pressed(struct swa_display* base,
		enum swa_mouse_button button) {
	struct swa_display_x11* dpy = get_display_x11(base);
	return dpy->mouse.button_states & (1ul << (unsigned) button);
}
static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_x11* dpy = get_display_x11(base);
	*x = dpy->mouse.x;
	*y = dpy->mouse.y;
}
static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
	return dpy->mouse.over ? &dpy->mouse.over->base : NULL;
}
static struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	// struct swa_display_x11* dpy = get_display_x11(base);
	return NULL;
}
static bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_x11* dpy = get_display_x11(base);
	return false;
}
static bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_x11* dpy = get_display_x11(base);
	return false;
}

// The difference between depth and bpp (bits per pixel):
// bits per pixel describes how many bits each pixel occupies in
// memory while depth describes how many of those hold meaningful
// values.
static enum swa_image_format visual_to_format(const xcb_visualtype_t* v,
		unsigned int depth, unsigned int bpp) {
	// We only handle the following cases:
	// - depth = bpp = 24: rgb 8-bit format
	// - depth = bpp = 32: rgba 8-bit format
	// - depth = 24, bpp = 32: rgbx 8-bit format
	// These cover all formats in swa_image_format
	if(depth != 24 && depth != 32) {
		return swa_image_format_none;
	}

	if(depth != bpp && (depth != 24 || bpp != 32)) {
		return swa_image_format_none;
	}

	// this is not explicitly documented anywhere, but given that the fields
	// of the visual type are called "<color>_mask" i assume that they assume
	// the logical mask for <color>, i.e. the color format on a native word.
	// We therefore first map the masks to a format in word order and
	// then use toggle_byte_word below to get the byte order format.
	const uint32_t b1 = 0xFF000000u;
	const uint32_t b2 = 0x00FF0000u;
	const uint32_t b3 = 0x0000FF00u;
	const uint32_t b4 = 0x000000FFu;
	static const struct {
		uint32_t bpp;
		uint32_t r, g, b, a;
		enum swa_image_format format; // in word order
	} formats[] = {
		{32, b1, b2, b3, b4, swa_image_format_rgba32},
		{32, b3, b2, b1, b4, swa_image_format_bgra32},
		{32, b2, b3, b4, b1, swa_image_format_argb32},
		{24, b1, b2, b3, 0u, swa_image_format_rgb24},
		{24, b3, b2, b1, 0u, swa_image_format_bgr24},
		{32, b3, b2, b1, 0u, swa_image_format_bgrx32},
		{32, b2, b3, b4, 0u, swa_image_format_xrgb32},
	};
	const unsigned len = sizeof(formats) / sizeof(formats[0]);

	// NOTE: this is kinda hacky and we shouldn't depend on it.
	unsigned a = 0u;
	if(depth == 32) {
		a = 0xFFFFFFFFu & ~(v->red_mask | v->green_mask | v->blue_mask);
	}

	for(unsigned i = 0u; i < len; ++i) {
		if(v->red_mask == formats[i].r &&
				v->green_mask == formats[i].g &&
				v->blue_mask == formats[i].b &&
				a == formats[i].a &&
				bpp == formats[i].bpp) {
			return swa_image_format_toggle_byte_word(formats[i].format);
		}
	}

	return swa_image_format_none;
}

// Returns the score of the visual.
// If the score is negative, the visual is already perfect.
static int rate_visual(struct swa_display_x11* dpy,
		const struct swa_window_settings* settings, xcb_visualtype_t* visual,
		enum swa_image_format format, unsigned depth) {
	int s = 1; // baseline
	bool perfect = true;
	if(visual->_class == XCB_VISUAL_CLASS_DIRECT_COLOR ||
			visual->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
		s += (1 << 4);
	} else {
		perfect = false;
	}

	bool known_format = format != swa_image_format_none;
	if(known_format) {
		if(settings->surface == swa_surface_buffer) {
			s += (1 << 3);
			enum swa_image_format pref =
				settings->surface_settings.buffer.preferred_format;
			if(format == pref) {
				s += (1 << 5);
				dlg_assertlm(dlg_level_warn,
					settings->transparent == (depth == 32),
					"Preferred buffer format and 'transparent' don't match");
			} else if(pref != swa_image_format_none) {
				perfect = false;
			}
		} else {
			s += (1 << 1); // always nice to have a common format
		}
	} else {
		perfect = false;
	}

	if(settings->transparent == (depth == 32)) {
		s += (1 << 2);
	} else {
		perfect = false;
	}

	return perfect ? -s : s;
}

static void find_visual(struct swa_window_x11* win,
		const struct swa_window_settings* settings,
		unsigned* scanline_pad, enum swa_image_format* format) {
	struct swa_display_x11* dpy = win->dpy;
	xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(dpy->screen);

	const xcb_setup_t* setup = xcb_get_setup(dpy->conn);
	xcb_format_t* formats = xcb_setup_pixmap_formats(setup);
	unsigned fmtcount = xcb_setup_pixmap_formats_length(setup);
	int best = 0;

	for(; di.rem; xcb_depth_next(&di)) {
		unsigned depth = di.data->depth;
		xcb_visualtype_iterator_t vi = xcb_depth_visuals_iterator(di.data);
		bool done = false;

		// find matching bpp for given depth
		// bpp and depth are different concepts. See visual_to_format
		// for more details
		unsigned fmt;
		for(fmt = 0u; fmt < fmtcount; ++fmt) {
			if(formats[fmt].depth == depth) {
				break;
			}
		}

		if(fmt == fmtcount) {
			dlg_warn("Couldn't find xcb pixmap format for depth");
			continue;
		}

		unsigned bpp = formats[fmt].bits_per_pixel;
		for(; vi.rem; xcb_visualtype_next(&vi)) {
			enum swa_image_format fmti = visual_to_format(vi.data, depth, bpp);
			int score = rate_visual(dpy, settings, vi.data, fmti, depth);
			if(score < 0) { // perfect visual
				*scanline_pad = formats[fmt].scanline_pad;
				*format = fmti;
				win->visualtype = vi.data;
				win->depth = depth;
				best = score;
				done = true;
				break;
			}

			if(score > best) {
				*scanline_pad = formats[fmt].scanline_pad;
				*format = fmti;
				win->visualtype = vi.data;
				win->depth = depth;
				best = score;
			}
		}

		if(done) {
			break;
		}
	}
}

static swa_proc display_get_gl_proc_addr(struct swa_display* base,
		const char* name) {
#ifdef SWA_WITH_GL
	return (swa_proc) eglGetProcAddress(name);
#else
	dlg_error("swa was built without gl support");
	return NULL;
#endif
}

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_x11* dpy = get_display_x11(base);
	struct swa_window_x11* win = calloc(1, sizeof(*win));

	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;
	win->init_size_pending =
		(settings->width == SWA_DEFAULT_SIZE) ||
		(settings->height == SWA_DEFAULT_SIZE);

	// link
	win->next = dpy->window_list;
	if(!dpy->window_list) {
		dpy->window_list = win;
	} else {
		dpy->window_list->prev = win;
	}

	// find visual
	// data for later when using buffer surface
	unsigned visual_scanline_pad;
	enum swa_image_format visual_format;

#ifdef SWA_WITH_GL
	EGLConfig egl_config = {0};
#endif

	if(settings->surface == swa_surface_gl) {
#ifdef SWA_WITH_GL
		if(!dpy->egl) {
			dpy->egl = swa_egl_display_create(EGL_PLATFORM_X11_EXT,
				dpy->display);
			if(!dpy->egl) {
				goto error;
			}
		}

		const struct swa_gl_surface_settings* gls = &settings->surface_settings.gl;
		bool alpha = settings->transparent;
		EGLContext* ctx = &win->gl.context;
		if(!swa_egl_init_context(dpy->egl, gls, alpha, &egl_config, ctx)) {
			goto error;
		}

		// find the visualtype for this egl config
		// we have to do this to find the depth for window creation
		EGLint visualid;
		if(!eglGetConfigAttrib(dpy->egl->display, egl_config,
				EGL_NATIVE_VISUAL_ID, &visualid)) {
			dlg_error("eglGetConfigAttrib failed");
			goto error;
		}

		xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(dpy->screen);
		for (; di.rem; xcb_depth_next (&di)) {
			xcb_visualtype_iterator_t vi = xcb_depth_visuals_iterator(di.data);
			for(; vi.rem; xcb_visualtype_next(&vi)) {
				if((int) vi.data->visual_id == visualid) {
					win->visualtype = vi.data;
					win->depth = di.data->depth;
					break;
				}
			}
	    }
#else
		dlg_error("swa was compiled without GL support");
		goto err;
#endif
	} else {
		find_visual(win, settings, &visual_scanline_pad, &visual_format);
	}

	if(!win->visualtype) {
		dlg_error("Could not find valid visual");
		return false;
	}

	dlg_debug("visualid: %d, depth: %d", win->visualtype->visual_id,
		win->depth);

	unsigned x = 0;
	unsigned y = 0;

	// there is no concept for default size on x11
	win->width = settings->width == SWA_DEFAULT_SIZE ?
		SWA_FALLBACK_WIDTH : settings->width;
	win->height = settings->height == SWA_DEFAULT_SIZE ?
		SWA_FALLBACK_HEIGHT : settings->height;
	xcb_window_t xparent = dpy->screen->root;

	win->colormap = xcb_generate_id(dpy->conn);
	xcb_create_colormap(dpy->conn, XCB_COLORMAP_ALLOC_NONE, win->colormap,
		dpy->screen->root, win->visualtype->visual_id);
	uint32_t eventmask =
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_FOCUS_CHANGE;

	// Setting the background pixel here may introduce flicker but may fix issues
	// with creating opengl windows. To get the default (parent) cursor
	// on xwayland we apparently have to explicitly specify XCB_NONE as cursor.
	uint32_t valuemask = XCB_CW_BORDER_PIXEL |
		XCB_CW_EVENT_MASK |
		XCB_CW_COLORMAP |
		XCB_CW_CURSOR;
	uint32_t valuelist[] = {0, eventmask, win->colormap, XCB_NONE};

	win->window = xcb_generate_id(dpy->conn);
	xcb_void_cookie_t cookie = xcb_create_window_checked(dpy->conn, win->depth,
		win->window, xparent, x, y, win->width, win->height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, win->visualtype->visual_id,
		valuemask, valuelist);
	xcb_generic_error_t* err = xcb_request_check(dpy->conn, cookie);
	if(err) {
		handle_error(dpy, err, "xcb_create_window");
		goto error;
	}

	// set properties
	if(settings->state != swa_window_state_none &&
			settings->state != swa_window_state_normal) {
		win_set_state(&win->base, settings->state);
	}

	if(settings->title) {
		win_set_title(&win->base, settings->title);
	}

	if(settings->cursor.type != swa_cursor_default) {
		win_set_cursor(&win->base, settings->cursor);
	}

	// supported protocols
	xcb_atom_t sup_prots[] = {
		dpy->atoms.wm_delete_window,
		dpy->ewmh._NET_WM_PING,
	};
	unsigned n_sup_prots = sizeof(sup_prots) / sizeof(sup_prots[0]);
	xcb_change_property(dpy->conn, XCB_PROP_MODE_REPLACE, win->window,
		dpy->ewmh.WM_PROTOCOLS, XCB_ATOM_ATOM, 32, n_sup_prots, sup_prots);

	pid_t pid = getpid();
	xcb_ewmh_set_wm_pid(&dpy->ewmh, win->window, pid);

	// register for touch xinput events
	// we only need touch events if the window listener implements it
	struct {
		xcb_input_event_mask_t info;
		xcb_input_xi_event_mask_t events;
	} mask;

	// NOTE: not sure how to test this but we might want to listen
	// for TOUCH_OWNERSHIP events. See
	// https://lwn.net/Articles/475886/
	// https://lwn.net/Articles/485484/
	const struct swa_window_listener* l = win->base.listener;
	mask.info.deviceid = XCB_INPUT_DEVICE_ALL_MASTER; // or ALL?
	mask.info.mask_len = sizeof(mask.events) / sizeof(uint32_t);
	mask.events =
		(l->touch_begin ? XCB_INPUT_XI_EVENT_MASK_TOUCH_BEGIN : 0) |
		(l->touch_end ? XCB_INPUT_XI_EVENT_MASK_TOUCH_END : 0) |
		(l->touch_update ? XCB_INPUT_XI_EVENT_MASK_TOUCH_UPDATE : 0);
	if(mask.events) {
		xcb_input_xi_select_events(win->dpy->conn, win->window, 1, &mask.info);
	}

	if(settings->client_decorate == swa_preference_yes) {
		// Motif WM hints are legacy stuff and shouldn't really be
		// used anymore. But it's the only way we can try really.
		// ewmh has _NET_WM_WINDOW_TYPE but that's not what this is
		// about.
		typedef struct {
			unsigned long flags;
			unsigned long functions;
			unsigned long decorations;
			long input_mode;
			unsigned long status;
		} MotifWmHints;

		MotifWmHints hints = {0};
		hints.flags = 2u;
		hints.decorations = 0u;

		unsigned len = sizeof(hints) / sizeof(uint32_t);
		xcb_change_property(win->dpy->conn, XCB_PROP_MODE_REPLACE,
			win->window, win->dpy->atoms.motif_wm_hints,
			win->dpy->atoms.motif_wm_hints, 32, len, &hints);
		win->client_decorated = true;
	}

	if(!settings->hide) {
		xcb_map_window(dpy->conn, win->window);
	}

	// create surface
	win->surface_type = settings->surface;
	if(win->surface_type == swa_surface_buffer) {
		if(visual_format == swa_image_format_none) {
			dlg_error("Couldn't find visual with known format");
			goto error;
		}

		win->buffer.gc = xcb_generate_id(dpy->conn);
		uint32_t value[] = {0, 0};
		xcb_void_cookie_t c = xcb_create_gc_checked(dpy->conn, win->buffer.gc,
			win->window, XCB_GC_FOREGROUND, value);
		xcb_generic_error_t* e = xcb_request_check(dpy->conn, c);
		if(e) {
			handle_error(dpy, e, "xcb_create_gc");
			goto error;
		}

		dlg_assert(visual_scanline_pad % 8 == 0);
		win->buffer.format = visual_format;
		win->buffer.scanline_align = visual_scanline_pad / 8;
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		if(!(win->gl.surface = swa_egl_create_surface(dpy->egl, &win->window,
				egl_config, settings->surface_settings.gl.srgb))) {
			goto error;
		}
#else
		// we already excluded that case above
		dlg_fatal("unreachable");
#endif
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		win->vk.instance = settings->surface_settings.vk.instance;
		if(!win->vk.instance) {
			dlg_error("No vulkan instance passed for vulkan window");
			goto error;
		}

		VkXcbSurfaceCreateInfoKHR info = {0};
		info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
		info.connection = win->dpy->conn;
		info.window = win->window;

		VkInstance instance = (VkInstance) win->vk.instance;
		VkSurfaceKHR surface;

		PFN_vkGetInstanceProcAddr fpGetProcAddr = (PFN_vkGetInstanceProcAddr)
			settings->surface_settings.vk.get_instance_proc_addr;
#ifdef SWA_WITH_LINKED_VK
		if(!fpGetProcAddr) {
			fpGetProcAddr = &vkGetInstanceProcAddr;
		}
#else // SWA_WITH_LINKED_VK
		if(!fpGetProcAddr) {
			dlg_error("No vkGetInstanceProcAddr provided, swa wasn't linked "
				"against vulkan");
			goto error;
		}
#endif // SWA_WITH_LINKED_VK

		PFN_vkCreateXcbSurfaceKHR fn = (PFN_vkCreateXcbSurfaceKHR)
			fpGetProcAddr(instance, "vkCreateXcbSurfaceKHR");
		if(!fn) {
			dlg_error("Failed to load 'vkCreateXcbSurfaceKHR' function");
			goto error;
		}

		VkResult res = fn(instance, &info, NULL, &surface);
		if(res != VK_SUCCESS) {
			dlg_error("Failed to create vulkan surface: %d", res);
			goto error;
		}

		win->vk.surface = (uint64_t) surface;
		win->vk.destroy_surface_pfn = (void*)
			fpGetProcAddr(instance, "vkDestroySurfaceKHR");
#else
		dlg_error("swa was compiled without vulkan support");
		goto err;
#endif
	}

	return &win->base;

error:
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

struct swa_display* swa_display_x11_create(const char* appname) {
	(void) appname;

	// We start by opening a display since we need that for gl
	// Neither egl nor glx support xcb. And since xlib is implemented
	// using xcb these days, we can get the xcb connection from the
	// xlib display but not the other way around.
	// We need multi threading since we implement display wakeup
	// using an event.
	XInitThreads();
	Display* display = XOpenDisplay(NULL);
	if(!display) {
		return NULL;
	}

	struct swa_display_x11* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;
	dpy->display = display;
	dpy->conn = XGetXCBConnection(display);
	dpy->screen = xcb_setup_roots_iterator(xcb_get_setup(dpy->conn)).data;

	// make sure we can retrieve events using xcb
	XSetEventQueueOwner(dpy->display, XCBOwnsEventQueue);

	// create dummy window used for selections and wakeup
	dpy->dummy_window = xcb_generate_id(dpy->conn);
	xcb_void_cookie_t cookie = xcb_create_window_checked(dpy->conn,
		XCB_COPY_FROM_PARENT, dpy->dummy_window,
		dpy->screen->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
		XCB_COPY_FROM_PARENT, 0, NULL);
	xcb_generic_error_t* err = xcb_request_check(dpy->conn, cookie);
	if(err) {
		handle_error(dpy, err, "xcb_create_window (dummy)");
		goto err;
	}

	// load atoms
	xcb_intern_atom_cookie_t* ewmh_cookie =
		xcb_ewmh_init_atoms(dpy->conn, &dpy->ewmh);

	struct {
		xcb_atom_t* atom;
		const char* name;
		xcb_intern_atom_cookie_t cookie;
	} atoms[] = {
		{&dpy->atoms.xdnd.enter, "XdndEnter", {0}},
		{&dpy->atoms.xdnd.position, "XdndPosition", {0}},
		{&dpy->atoms.xdnd.status, "XdndStatus", {0}},
		{&dpy->atoms.xdnd.type_list, "XdndTypeList", {0}},
		{&dpy->atoms.xdnd.action_copy, "XdndActionCopy", {0}},
		{&dpy->atoms.xdnd.action_move, "XdndActionMove", {0}},
		{&dpy->atoms.xdnd.action_ask, "XdndActionAsk", {0}},
		{&dpy->atoms.xdnd.action_link, "XdndActionLink", {0}},
		{&dpy->atoms.xdnd.drop, "XdndDrop", {0}},
		{&dpy->atoms.xdnd.leave, "XdndLeave", {0}},
		{&dpy->atoms.xdnd.finished, "XdndFinished", {0}},
		{&dpy->atoms.xdnd.selection, "XdndSelection", {0}},
		{&dpy->atoms.xdnd.proxy, "XdndProxy", {0}},
		{&dpy->atoms.xdnd.aware, "XdndAware", {0}},

		{&dpy->atoms.clipboard, "CLIPBOARD", {0}},
		{&dpy->atoms.targets, "TARGETS", {0}},
		{&dpy->atoms.text, "TEXT", {0}},
		{&dpy->atoms.utf8_string, "UTF8_STRING", {0}},
		{&dpy->atoms.file_name, "FILE_NAME", {0}},

		{&dpy->atoms.wm_delete_window, "WM_DELETE_WINDOW", {0}},
		{&dpy->atoms.wm_change_state, "WM_CHANGE_STATE", {0}},
		{&dpy->atoms.motif_wm_hints, "_MOTIF_WM_HINTS", {0}},

		{&dpy->atoms.mime.text, "text/plain", {0}},
		{&dpy->atoms.mime.utf8, "text/plain;charset=utf8", {0}},
		{&dpy->atoms.mime.uri_list, "text/uri-list", {0}},
		{&dpy->atoms.mime.binary, "application/octet-stream", {0}}
	};

	unsigned length = sizeof(atoms) / sizeof(atoms[0]);
	for(unsigned i = 0u; i < length; ++i) {
		unsigned len = strlen(atoms[i].name);
		atoms[i].cookie = xcb_intern_atom(dpy->conn, 0, len, atoms[i].name);
	}

	for(unsigned i = 0u; i < length; ++i) {
		xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(dpy->conn,
			atoms[i].cookie, &err);
		if(reply) {
			*atoms[i].atom = reply->atom;
			free(reply);
		} else {
			char buf[256];
			XGetErrorText(dpy->display, err->error_code, buf, sizeof(buf));
			dlg_warn("Failed to load atom %s: %s", atoms[i].name, buf);
			free(err);
		}
	}

	xcb_ewmh_init_atoms_replies(&dpy->ewmh, ewmh_cookie, &err);
	if(err) {
		handle_error(dpy, err, "xcb_ewmh_init_atoms");
	}

	// read out supported ewmh stuff
	xcb_get_property_cookie_t c = xcb_get_property(dpy->conn, false,
		dpy->screen->root, dpy->ewmh._NET_SUPPORTED,
		XCB_ATOM_ANY, 0, max_prop_length);
	xcb_get_property_reply_t* reply = xcb_get_property_reply(dpy->conn, c, &err);
	if(reply) {
		dlg_assert(reply->format == 32);
		dlg_assert(reply->type == XCB_ATOM_ATOM);

		xcb_atom_t* supported = xcb_get_property_value(reply);
		unsigned count = xcb_get_property_value_length(reply) / 4;
		bool state = false;
		bool max_horz = false;
		bool max_vert = false;
		bool fullscreen = false;
		for(unsigned i = 0u; i < count; ++i) {
			if(supported[i] == dpy->ewmh._NET_WM_MOVERESIZE) {
				dpy->ewmh_caps |= swa_window_cap_begin_resize;
				dpy->ewmh_caps |= swa_window_cap_begin_move;
			} else if(supported[i] == dpy->ewmh._NET_WM_STATE) {
				state = true;
			} else if(supported[i] == dpy->ewmh._NET_WM_STATE_FULLSCREEN) {
				fullscreen = true;
			} else if(supported[i] == dpy->ewmh._NET_WM_STATE_MAXIMIZED_HORZ) {
				max_horz = true;
			} else if(supported[i] == dpy->ewmh._NET_WM_STATE_MAXIMIZED_VERT) {
				max_vert = true;
			}
		}

		if(state) {
			if(max_horz && max_vert) dpy->ewmh_caps |= swa_window_cap_maximize;
			if(fullscreen) dpy->ewmh_caps |= swa_window_cap_fullscreen;
		}
		free(reply);
	} else {
		handle_error(dpy, err, "get_property on _NET_SUPPORTED");
	}

	// read out extension support
	// NOTE: not really sure about those versions, kinda strict atm
	// since that's what other libraries seem to do. We might be fine
	// with lower versions of the extensions

	// check for xpresent extension support
	const xcb_query_extension_reply_t* ext;
	ext = xcb_get_extension_data(dpy->conn, &xcb_input_id);
	if(ext && ext->present) {
		xcb_input_xi_query_version_cookie_t c =
			xcb_input_xi_query_version(dpy->conn, 2, 0);
		xcb_input_xi_query_version_reply_t* reply =
			xcb_input_xi_query_version_reply(dpy->conn, c, &err);
		if(!reply) {
			handle_error(dpy, err, "xcb_input_xi_query_version");
		} else if(reply->major_version < 2) {
			dlg_info("xinput version too low: %d.%d",
				reply->major_version, reply->minor_version);
		} else {
			dpy->ext.xinput = ext->major_opcode;
		}
		free(reply);
	} else {
		dlg_info("xinput not available, no touch input");
	}

	// check for present extension support
	ext = xcb_get_extension_data(dpy->conn, &xcb_present_id);
	if(ext && ext->present) {
		xcb_present_query_version_cookie_t c =
			xcb_present_query_version(dpy->conn, 1, 2);
		xcb_present_query_version_reply_t* reply =
			xcb_present_query_version_reply(dpy->conn, c, &err);
		if(!reply) {
			handle_error(dpy, err, "xcb_present_query_version");
		} else if(reply->major_version < 1 || reply->minor_version < 2) {
			dlg_info("xpresent version too low: %d.%d",
				reply->major_version, reply->minor_version);
		} else {
			dpy->ext.xpresent = ext->major_opcode;
		}
		free(reply);
	} else {
		dlg_warn("xpresent not available, no frame callbacks");
	}

	// check for shm extension support
	xcb_shm_query_version_cookie_t sc = xcb_shm_query_version(dpy->conn);
	xcb_shm_query_version_reply_t* sreply =
		xcb_shm_query_version_reply(dpy->conn, sc, &err);
	if(!sreply) {
		handle_error(dpy, err, "xcb_shm_query_version");
	} else if(/*sreply->shared_pixmaps && */ // we don't need shared pixmaps
			sreply->major_version >= 1 &&
			sreply->minor_version >= 2) {
		dpy->ext.shm = true;
	} else {
		dlg_warn("xshm not fully supported: version %d.%d, pixmaps: %d",
			sreply->major_version, sreply->minor_version,
			sreply->shared_pixmaps);
	}
	free(sreply);

	// xkb: we require this extension for keyboard support
	// NOTE: instead of erroring out, we could simply not report
	// the keyboard capability
	uint16_t major, minor;
	int ret = xkb_x11_setup_xkb_extension(dpy->conn,
		XKB_X11_MIN_MAJOR_XKB_VERSION,
		XKB_X11_MIN_MINOR_XKB_VERSION,
		XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
		&major, &minor, &dpy->ext.xkb, NULL);
	if(!ret) {
		dlg_error("Could not setup xkb");
		goto err;
	}

	dpy->keyboard.device_id = xkb_x11_get_core_keyboard_device_id(dpy->conn);
	if(dpy->keyboard.device_id == -1) {
		dlg_error("Failed to get x11 core keyboard device id");
		goto err;
	}

	struct swa_xkb_context* xkb = &dpy->keyboard.xkb;
	xkb->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if(!xkb->context) {
		dlg_error("xkb_context_new failed");
		goto err;
	}

	if(!init_keymap(dpy)) {
		goto err;
	}

	const int req_events =
		XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
		XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
		XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
	const int req_nkn_details = XCB_XKB_NKN_DETAIL_KEYCODES;
	const int req_map_parts =
		XCB_XKB_MAP_PART_KEY_TYPES |
		XCB_XKB_MAP_PART_KEY_SYMS |
		XCB_XKB_MAP_PART_MODIFIER_MAP |
		XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
		XCB_XKB_MAP_PART_KEY_ACTIONS |
		XCB_XKB_MAP_PART_VIRTUAL_MODS |
		XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
	const int req_state_details =
		XCB_XKB_STATE_PART_MODIFIER_BASE |
		XCB_XKB_STATE_PART_MODIFIER_LATCH |
		XCB_XKB_STATE_PART_MODIFIER_LOCK |
		XCB_XKB_STATE_PART_GROUP_BASE |
		XCB_XKB_STATE_PART_GROUP_LATCH |
		XCB_XKB_STATE_PART_GROUP_LOCK;

	xcb_xkb_select_events_details_t details = {0};
	details.affectNewKeyboard = req_nkn_details;
	details.newKeyboardDetails = req_nkn_details;
	details.affectState = req_state_details;
	details.stateDetails = req_state_details;

	xcb_void_cookie_t xkbc = xcb_xkb_select_events_aux_checked(dpy->conn,
		dpy->keyboard.device_id, req_events, 0, 0,
		req_map_parts, req_map_parts, &details);
	err = xcb_request_check(dpy->conn, xkbc);
	if(err) {
		handle_error(dpy, err, "xcb_xkb_select_events_aux_checked");
		goto err;
	}

	if(!swa_xkb_init_compose(xkb)) {
		goto err;
	}

	return &dpy->base;

err:
	display_destroy(&dpy->base);
	return NULL;
}
