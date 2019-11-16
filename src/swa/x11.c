#include <swa/x11.h>
#include <dlg/dlg.h>
#include <string.h>

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

// display api
static void display_destroy(struct swa_display* base) {
	struct swa_display_x11* dpy = get_display_x11(base);
    free(dpy);
}

static struct swa_window_x11* find_window(struct swa_display_x11* dpy,
		xcb_window_t win) {
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
		} else if(protocol == dpy->atoms.delete_window &&
				(win = find_window(dpy, client->window))) {
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

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_x11* dpy = get_display_x11(base);
	struct swa_window_x11* win = calloc(1, sizeof(*win));

	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;
	return &win->base;
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

static void win_destroy(struct swa_window* base) {
	struct swa_window_x11* win = get_window_x11(base);
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
	return false;
}

static void win_apply_buffer(struct swa_window* base) {
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

#define handle_error(dpy, err, txt) do {\
	dlg_assert(err); \
	char buf[256]; \
	XGetErrorText(dpy->display, err->error_code, buf, sizeof(buf)); \
	dlg_error(txt ": %s (%d)", buf, err->error_code); \
	free(err); \
} while(0)

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
