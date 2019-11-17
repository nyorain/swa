#include <swa/x11.h>
#include <dlg/dlg.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>

#include <xcb/xcb.h>
#include <xcb/present.h>
#include <xcb/xinput.h>
#include <xcb/shm.h>

#ifdef SWA_WITH_VK
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xcb.h>
#endif

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;
static const unsigned max_prop_length = 0x1fffffff;

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
	if(win->dpy) {
		struct swa_display_x11* dpy = win->dpy;
		if(win->next) win->next->prev = win->prev;
		if(win->prev) win->prev->next = win->next;
		if(win->dpy->window_list == win) {
			win->dpy->window_list = NULL;
		}

		if(win->colormap) xcb_free_colormap(dpy->conn, win->colormap);
		if(win->window) xcb_destroy_window(dpy->conn, win->window);
	}
    free(win);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
    return win->dpy->ewmh_caps |
		swa_window_cap_cursor |
        swa_window_cap_minimize |
        swa_window_cap_size |
        swa_window_cap_position |
        swa_window_cap_size_limits |
        swa_window_cap_title |
        swa_window_cap_visibility;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_x11* win = get_window_x11(base);
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
}

static void win_set_position(struct swa_window* base, int x, int y) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);

	// TODO: use present extension
	if(win->base.listener && win->base.listener->draw) {
		win->base.listener->draw(base);
	}
}

static void win_surface_frame(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_begin_move(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_begin_resize(struct swa_window* base, enum swa_edge edges) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_set_title(struct swa_window* base, const char* title) {
	struct swa_window_x11* win = get_window_x11(base);
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
	struct swa_window_x11* win = get_window_x11(base);
}

static bool win_is_client_decorated(struct swa_window* base) {
	return false;
}

static uint64_t win_get_vk_surface(struct swa_window* base) {
	return false;
}

static bool win_gl_make_current(struct swa_window* base) {
	return false;
}

static bool win_gl_swap_buffers(struct swa_window* base) {
	return false;
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
	return false;
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
	uint64_t n_bytes = win->width * win->height * fmt_size;
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
	img->stride = fmt_size * win->width;

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
	.set_position = win_set_position,
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

static void process_event(struct swa_display_x11* dpy,
		const xcb_generic_event_t* ev) {
	unsigned type = ev->response_type & ~0x80;
	struct swa_window_x11* win;

	switch(type) {
	case XCB_EXPOSE: {
		xcb_expose_event_t* expose = (xcb_expose_event_t*) ev;
		if((win = find_window(dpy, expose->window))) {
			swa_window_refresh(&win->base);
		}
		break;
	} case XCB_MAP_NOTIFY: {
		xcb_map_notify_event_t* map = (xcb_map_notify_event_t*) ev;
		if((win = find_window(dpy, map->window))) {
			swa_window_refresh(&win->base);
		}
		break;
	} case XCB_CONFIGURE_NOTIFY: {
		xcb_configure_notify_event_t* configure =
			(xcb_configure_notify_event_t*) ev;
		if((win = find_window(dpy, configure->window))) {
			win->width = configure->width;
			win->height = configure->height;
		}
		break;
	} case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t* client = (xcb_client_message_event_t*) ev;
		unsigned protocol = client->data.data32[0];

		if(protocol == dpy->ewmh._NET_WM_PING) {
			xcb_ewmh_send_wm_ping(&dpy->ewmh, dpy->screen->root,
				client->data.data32[1]);
		} else if(protocol == dpy->atoms.wm_delete_window) {
			win = find_window(dpy, client->window);
			if(win->base.listener && win->base.listener->close) {
				win->base.listener->close(&win->base);
			}
		}
		break;
	} case 0u: {
		int code = ((xcb_generic_error_t*)ev)->error_code;
		char buf[256];
		XGetErrorText(dpy->display, code, buf, sizeof(buf));
		dlg_error("retrieved x11 error code:: %s (%d)", buf, code);
		break;
	} default:
		break;
	}

	// touch events
	xcb_ge_generic_event_t* gev = (xcb_ge_generic_event_t*)ev;
	if(dpy->ext.xinput && ev->response_type == XCB_GE_GENERIC &&
			gev->extension == dpy->ext.xinput) {
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
						struct swa_touch_begin_event ev = {
							.id = id,
							.x = x,
							.y = y,
						};
						win->base.listener->touch_begin(&win->base, &ev);
					} return;
				case XCB_INPUT_TOUCH_UPDATE:
					if(win->base.listener->touch_update) {
						struct swa_touch_update_event ev = {
							.id = id,
							.x = x,
							.y = y,
							// TODO: dx, dy
							// or remove them from the event?
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

	// // present event
	// if(presentOpcode_ && gev.response_type == XCB_GE_GENERIC &&
	// 		gev.extension == presentOpcode_) {
	// 	switch(gev.event_type) {
	// 		case XCB_PRESENT_COMPLETE_NOTIFY: {
	// 			auto pev = copyu<xcb_present_complete_notify_event_t>(gev);
	// 			if(pev.window == xDummyWindow()) {
	// 				for(auto& wc : present_) {
	// 					wc->presentCompleteEvent(pev.serial);
	// 				}
	// 				present_.clear();
	// 			} else {
	// 				if(auto wc = windowContext(pev.window); wc) {
	// 					wc->presentCompleteEvent(pev.serial);
	// 				}
	// 			}
	// 			return;
	// 		} case XCB_PRESENT_IDLE_NOTIFY:
	// 		case XCB_PRESENT_CONFIGURE_NOTIFY:
	// 			return;
	// 	}
	// }
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
		process_event(dpy, event);
		xcb_flush(dpy->conn);
		free(event);
	}

	return !check_error(dpy);
}

static void display_wakeup(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
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
		swa_display_cap_client_decoration | // TODO: depends on wm i guess
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
    return false;
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_x11* dpy = get_display_x11(base);
    return NULL;
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
    return swa_keyboard_mod_none;
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
    return NULL;
}

static bool display_mouse_button_pressed(struct swa_display* base, enum swa_mouse_button button) {
	struct swa_display_x11* dpy = get_display_x11(base);
    return false;
}
static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_x11* dpy = get_display_x11(base);
}
static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
    return NULL;
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

static enum swa_image_format visual_to_format(const xcb_visualtype_t* v,
		unsigned int depth) {
	if(depth != 24 && depth != 32) {
		return swa_image_format_none;
	}

	// this is not explicitly documented anywhere, but given that the fields
	// of the visual type are called "<color>_mask" i assume that they assume
	// the logical mask for <color>, i.e. the color format on a native word.
	// We therefore first map the masks to a format in word order and
	// then use toggle_byte_word below to get the byte order format.
	static const struct {
		uint32_t r, g, b, a;
		enum swa_image_format format; // in word order
	} formats[] = {
		{ 0xFF000000u, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, swa_image_format_rgba32 },
		{ 0x0000FF00u, 0x00FF0000u, 0xFF000000u, 0x000000FFu, swa_image_format_bgra32 },
		{ 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u, swa_image_format_argb32 },
		{ 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0u, swa_image_format_rgb24 },
		{ 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0u, swa_image_format_bgr24 },
	};
	const unsigned len = sizeof(formats) / sizeof(formats[0]);

	// NOTE: this is hacky and we shouldn't depend on it.
	// Usually works though
	unsigned a = 0u;
	if(depth == 32) {
		a = 0xFFFFFFFFu & ~(v->red_mask | v->green_mask | v->blue_mask);
	}

	for(unsigned i = 0u; i < len; ++i) {
		if(v->red_mask == formats[i].r &&
				v->green_mask == formats[i].g &&
				v->blue_mask == formats[i].b &&
				a == formats[i].a) {
			return swa_image_format_toggle_byte_word(formats[i].format);
		}
	}

	return swa_image_format_none;
}

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_x11* dpy = get_display_x11(base);
	struct swa_window_x11* win = calloc(1, sizeof(*win));

	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;

	// link
	win->next = dpy->window_list;
	if(!dpy->window_list) {
		dpy->window_list = win;
	} else {
		dpy->window_list->prev = win;
	}

	// find visual
	xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(dpy->screen);
	for(; di.rem; xcb_depth_next(&di)) {
		unsigned depth = di.data->depth;
		xcb_visualtype_iterator_t vi = xcb_depth_visuals_iterator(di.data);
		for(; vi.rem; xcb_visualtype_next(&vi)) {
			if(vi.data->_class != XCB_VISUAL_CLASS_DIRECT_COLOR &&
					vi.data->_class != XCB_VISUAL_CLASS_TRUE_COLOR) {
				continue;
			}

			// TODO: rate by score and settings.transparent
			enum swa_image_format fmt = visual_to_format(vi.data, depth);
			if(depth >= 24 && fmt != swa_image_format_none) {
				win->visualtype = vi.data;
				win->depth = depth;
				if(depth == 32) break;
			}

		}
	}

	if(!win->visualtype) {
		dlg_error("Could not find valid visual");
		return false;
	}

	unsigned x = 0;
	unsigned y = 0;
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
	// with creating opengl windows.
	uint32_t valuemask = XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
	uint32_t valuelist[] = {0, eventmask, win->colormap};

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

	if(!settings->hide) {
		xcb_map_window(dpy->conn, win->window);
	}

	// set properties
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

	// create surface
	win->surface_type = settings->surface;
	if(win->surface_type == swa_surface_buffer) {
		win->buffer.gc = xcb_generate_id(dpy->conn);
		uint32_t value[] = {0, 0};
		xcb_void_cookie_t c = xcb_create_gc_checked(dpy->conn, win->buffer.gc,
			win->window, XCB_GC_FOREGROUND, value);
		xcb_generic_error_t* e = xcb_request_check(dpy->conn, c);
		if(e) {
			handle_error(dpy, e, "xcb_create_gc");
			goto error;
		}

		win->buffer.format = visual_to_format(win->visualtype, win->depth);
		dlg_assert(win->buffer.format != swa_image_format_none);
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
	.create_window = display_create_window,
};

struct swa_display* swa_display_x11_create(void) {
	// We start by opening a display since we need that for gl
	// Neither egl nor glx support xcb. And since xlib is implemented
	// using xcb these days, we can get the xcb connection from the
	// xlib display but not the other way around.
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

	xcb_generic_error_t* err;
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
	} else if(sreply->shared_pixmaps &&
			sreply->major_version >= 1 &&
			sreply->minor_version >= 2) {
		dpy->ext.shm = true;
	} else {
		dlg_warn("xshm not fully supported: version %d.%d, pixmaps: %d",
			sreply->major_version, sreply->minor_version,
			sreply->shared_pixmaps);
	}
	free(sreply);

    return &dpy->base;
}
