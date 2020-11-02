#include <swa/swa.h>
#include <swa/private/impl.h>
#include <dlg/dlg.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWA_WITH_WL
  #include <swa/private/wayland.h>
#endif
#ifdef SWA_WITH_WIN
  #include <swa/private/winapi.h>
#endif
#ifdef SWA_WITH_X11
  #include <swa/private/x11.h>
#endif
#ifdef SWA_WITH_KMS
  #include <swa/private/kms/kms.h>
#endif
#ifdef SWA_WITH_ANDROID
  #include <swa/android.h>
#endif

typedef struct swa_display* (*display_constructor)(const char*);

struct {
	const char* name;
	display_constructor constructor;
} backends[] = {
#ifdef SWA_WITH_WL
	{"wayland", swa_display_wl_create},
#endif
#ifdef SWA_WITH_X11
	{"x11", swa_display_x11_create},
#endif
#ifdef SWA_WITH_WIN
	{"winapi", swa_display_win_create},
#endif
#ifdef SWA_WITH_ANDROID
	{"android", swa_display_android_create},
#endif
#ifdef SWA_WITH_KMS
	{"kms", swa_display_kms_create},
#endif
};

struct swa_display* swa_display_autocreate(const char* appname) {
	const char* backend = getenv("SWA_BACKEND");
	unsigned backend_count = sizeof(backends) / sizeof(backends[0]);
	if(backend) {
		dlg_debug("SWA_BACKEND set to '%s'", backend);
		for(unsigned i = 0u; i < backend_count; ++i) {
			if(strcmp(backends[i].name, backend) == 0) {
				struct swa_display* dpy = backends[i].constructor(appname);
				if(!dpy) {
					dlg_error("Requested backend '%s' not available", backend);
				}
				return dpy;
			}
		}

		dlg_error("Requested backend '%s' unknown", backend);
		return NULL;
	}

	for(unsigned i = 0u; i < backend_count; ++i) {
		struct swa_display* dpy = backends[i].constructor(appname);
		if(dpy) {
			return dpy;
		}
	}

	return NULL;
}

void swa_window_settings_default(struct swa_window_settings* settings) {
	memset(settings, 0, sizeof(*settings));
	settings->cursor.type = swa_cursor_default;
	settings->title = "Default Window Title (swa)";
	settings->state = swa_window_state_normal;
	settings->width = settings->height = SWA_DEFAULT_SIZE;
}

unsigned swa_image_format_size(enum swa_image_format fmt) {
	switch(fmt) {
		case swa_image_format_rgba32:
		case swa_image_format_argb32:
		case swa_image_format_xrgb32:
		case swa_image_format_bgra32:
		case swa_image_format_bgrx32:
		case swa_image_format_abgr32:
			return 4;
		case swa_image_format_rgb24:
		case swa_image_format_bgr24:
			return 3;
		case swa_image_format_a8:
			return 1;
		case swa_image_format_none:
			return 0;
	}

	// unreachable for valid formats
	// dont' put it in default so we get warnings about unhandled enum values
	dlg_error("Invalid image format %d", fmt);
	return 0;
}

struct swa_image swa_convert_image_new(const struct swa_image* src,
		enum swa_image_format format, unsigned new_stride) {
	dlg_assert(src);
	dlg_assert(!src->width || !src->height || src->data);
	if(new_stride == 0) {
		new_stride = src->width * swa_image_format_size(format);
	}

	struct swa_image dst = {
		.width = src->width,
		.height = src->height,
		.format = format,
		.stride = new_stride,
		.data = malloc(src->height * new_stride)
	};
	swa_convert_image(src, &dst);
	return dst;
}

void swa_write_pixel(uint8_t* data, enum swa_image_format fmt,
		struct swa_pixel pixel) {
	switch(fmt) {
		case swa_image_format_rgba32:
			data[0] = pixel.r;
			data[1] = pixel.g;
			data[2] = pixel.b;
			data[3] = pixel.a;
			break;
		case swa_image_format_rgb24:
			data[0] = pixel.r;
			data[1] = pixel.g;
			data[2] = pixel.b;
			data[3] = 255;
			break;
		case swa_image_format_bgr24:
			data[0] = pixel.b;
			data[1] = pixel.g;
			data[2] = pixel.r;
			data[3] = 255;
			break;
		case swa_image_format_xrgb32:
			data[0] = 255;
			data[1] = pixel.r;
			data[2] = pixel.g;
			data[3] = pixel.b;
			break;
		case swa_image_format_argb32:
			data[0] = pixel.a;
			data[1] = pixel.r;
			data[2] = pixel.g;
			data[3] = pixel.b;
			break;
		case swa_image_format_abgr32:
			data[0] = pixel.a;
			data[1] = pixel.b;
			data[2] = pixel.g;
			data[3] = pixel.r;
			break;
		case swa_image_format_bgra32:
			data[0] = pixel.b;
			data[1] = pixel.g;
			data[2] = pixel.r;
			data[3] = pixel.a;
			break;
		case swa_image_format_bgrx32:
			data[0] = pixel.b;
			data[1] = pixel.g;
			data[2] = pixel.r;
			data[3] = 255;
			break;
		case swa_image_format_a8:
			data[0] = pixel.a;
			break;
		case swa_image_format_none:
			break;
	}
}

struct swa_pixel swa_read_pixel(const uint8_t* data, enum swa_image_format fmt) {
	switch(fmt) {
		case swa_image_format_rgba32:
			return (struct swa_pixel){data[0], data[1], data[2], data[3]};
		case swa_image_format_rgb24:
			return (struct swa_pixel){data[0], data[1], data[2], 255};
		case swa_image_format_bgr24:
			return (struct swa_pixel){data[2], data[1], data[0], 255};
		case swa_image_format_xrgb32:
			return (struct swa_pixel){data[1], data[2], data[3], 255};
		case swa_image_format_argb32:
			return (struct swa_pixel){data[1], data[2], data[3], data[0]};
		case swa_image_format_abgr32:
			return (struct swa_pixel){data[3], data[2], data[1], data[0]};
		case swa_image_format_bgra32:
			return (struct swa_pixel){data[2], data[1], data[0], data[3]};
		case swa_image_format_bgrx32:
			return (struct swa_pixel){data[2], data[1], data[0], 255};
		case swa_image_format_a8:
			return (struct swa_pixel){data[0], data[0], data[0], data[0]};
		case swa_image_format_none:
			return (struct swa_pixel){0, 0, 0, 0};
	}

	// unreachable for valid formats
	// dont' put it in default so we get warnings about unhandled enum values
	dlg_error("Invalid image format %d", fmt);
	return (struct swa_pixel){0, 0, 0, 0};
}

void swa_convert_image(const struct swa_image* src, const struct swa_image* dst) {
	dlg_assert(dst->width == src->width);
	dlg_assert(dst->height == src->height);

	unsigned src_size = swa_image_format_size(src->format);
	unsigned dst_size = swa_image_format_size(dst->format);

	const uint8_t* src_data = src->data;
	uint8_t* dst_data = dst->data;
	for(unsigned y = 0u; y < src->height; ++y) {
		const uint8_t* src_row = src_data;
		uint8_t* dst_row = dst_data;
		for(unsigned x = 0u; x < src->width; ++x) {
			struct swa_pixel pixel = swa_read_pixel(src_data, src->format);
			swa_write_pixel(dst_data, dst->format, pixel);
			src_data += src_size;
			dst_data += dst_size;
		}
		src_data = src_row + src->stride;
		dst_data = dst_row + dst->stride;
	}
}

enum swa_image_format swa_image_format_reversed(enum swa_image_format fmt) {
	switch(fmt) {
		case swa_image_format_rgba32:
			return swa_image_format_abgr32;
		case swa_image_format_argb32:
			return swa_image_format_bgra32;
		case swa_image_format_xrgb32:
			return swa_image_format_bgrx32;
		case swa_image_format_bgra32:
			return swa_image_format_argb32;
		case swa_image_format_bgrx32:
			return swa_image_format_xrgb32;
		case swa_image_format_abgr32:
			return swa_image_format_rgba32;
		case swa_image_format_rgb24:
			return swa_image_format_bgr24;
		case swa_image_format_bgr24:
			return swa_image_format_rgb24;
		case swa_image_format_a8:
		case swa_image_format_none:
			return fmt;
	}

	// unreachable for valid formats
	// dont' put it in default so we get warnings about unhandled enum values
	dlg_error("Invalid image format %d", fmt);
	return swa_image_format_none;
}

// 1: big
// 2: little
// other: something weird, no clue.
static int endianess(void) {
	union {
		uint32_t i;
		char c[4];
	} v = { 0x01000002 };
	return v.c[0];
}

enum swa_image_format swa_image_format_toggle_byte_word(enum swa_image_format fmt) {
	switch(endianess()) {
		case 1: return fmt;
		case 2: return swa_image_format_reversed(fmt);
		default:
			dlg_error("Invalid endianess");
			return swa_image_format_none;
	}
}

// diplay api
void swa_display_destroy(struct swa_display* dpy) {
	if(dpy) {
		dpy->impl->destroy(dpy);
	}
}
bool swa_display_dispatch(struct swa_display* dpy, bool block) {
	return dpy->impl->dispatch(dpy, block);
}
void swa_display_wakeup(struct swa_display* dpy) {
	dpy->impl->wakeup(dpy);
}
enum swa_display_cap swa_display_capabilities(struct swa_display* dpy) {
	return dpy->impl->capabilities(dpy);
}
const char** swa_display_vk_extensions(struct swa_display* dpy, unsigned* count) {
	return dpy->impl->vk_extensions(dpy, count);
}
bool swa_display_key_pressed(struct swa_display* dpy, enum swa_key key) {
	return dpy->impl->key_pressed(dpy, key);
}
const char* swa_display_key_name(struct swa_display* dpy, enum swa_key key) {
	return dpy->impl->key_name(dpy, key);
}
enum swa_keyboard_mod swa_display_active_keyboard_mods(struct swa_display* dpy) {
	return dpy->impl->active_keyboard_mods(dpy);
}
struct swa_window* swa_display_get_keyboard_focus(struct swa_display* dpy) {
	return dpy->impl->get_keyboard_focus(dpy);
}
bool swa_display_mouse_button_pressed(struct swa_display* dpy, enum swa_mouse_button button) {
	return dpy->impl->mouse_button_pressed(dpy, button);
}
void swa_display_mouse_position(struct swa_display* dpy, int* x, int* y) {
	dpy->impl->mouse_position(dpy, x, y);
}
struct swa_window* swa_display_get_mouse_over(struct swa_display* dpy) {
	return dpy->impl->get_mouse_over(dpy);
}
struct swa_data_offer* swa_display_get_clipboard(struct swa_display* dpy) {
	return dpy->impl->get_clipboard(dpy);
}
bool swa_display_set_clipboard(struct swa_display* dpy,
		struct swa_data_source* source) {
	return dpy->impl->set_clipboard(dpy, source);
}
bool swa_display_start_dnd(struct swa_display* dpy,
		struct swa_data_source* source) {
	return dpy->impl->start_dnd(dpy, source);
}
swa_proc swa_display_get_gl_proc_addr(struct swa_display* dpy,
		const char* name) {
	return dpy->impl->get_gl_proc_addr(dpy, name);
}
struct swa_window* swa_display_create_window(struct swa_display* dpy,
		const struct swa_window_settings* settings) {
	return dpy->impl->create_window(dpy, settings);
}

// window api
void swa_window_destroy(struct swa_window* win) {
	if(win) {
		win->impl->destroy(win);
	}
}
enum swa_window_cap swa_window_get_capabilities(struct swa_window* win) {
	return win->impl->get_capabilities(win);
}
void swa_window_set_min_size(struct swa_window* win, unsigned w, unsigned h) {
	win->impl->set_min_size(win, w, h);
}
void swa_window_set_max_size(struct swa_window* win, unsigned w, unsigned h) {
	win->impl->set_max_size(win, w, h);
}
void swa_window_show(struct swa_window* win, bool show) {
	win->impl->show(win, show);
}
void swa_window_set_size(struct swa_window* win, unsigned w, unsigned h) {
	win->impl->set_size(win, w, h);
}
void swa_window_set_cursor(struct swa_window* win, struct swa_cursor cursor) {
	win->impl->set_cursor(win, cursor);
}
void swa_window_refresh(struct swa_window* win) {
	win->impl->refresh(win);
}
void swa_window_surface_frame(struct swa_window* win) {
	win->impl->surface_frame(win);
}
void swa_window_set_state(struct swa_window* win, enum swa_window_state state) {
	win->impl->set_state(win, state);
}
void swa_window_begin_move(struct swa_window* win) {
	win->impl->begin_move(win);
}
void swa_window_begin_resize(struct swa_window* win, enum swa_edge edges) {
	win->impl->begin_resize(win, edges);
}
void swa_window_set_title(struct swa_window* win, const char* title) {
	win->impl->set_title(win, title);
}
void swa_window_set_icon(struct swa_window* win, const struct swa_image* image) {
	win->impl->set_icon(win, image);
}
bool swa_window_is_client_decorated(struct swa_window* win) {
	return win->impl->is_client_decorated(win);
}
uint64_t swa_window_get_vk_surface(struct swa_window* win) {
	return win->impl->get_vk_surface(win);
}
bool swa_window_gl_make_current(struct swa_window* win) {
	return win->impl->gl_make_current(win);
}
bool swa_window_gl_swap_buffers(struct swa_window* win) {
	return win->impl->gl_swap_buffers(win);
}
bool swa_window_gl_set_swap_interval(struct swa_window* win, int interval) {
	return win->impl->gl_set_swap_interval(win, interval);
}
bool swa_window_get_buffer(struct swa_window* win, struct swa_image* img) {
	return win->impl->get_buffer(win, img);
}
void swa_window_apply_buffer(struct swa_window* win) {
	win->impl->apply_buffer(win);
}
const struct swa_window_listener* swa_window_get_listener(struct swa_window* win) {
	return win->listener;
}
void swa_window_set_userdata(struct swa_window* win, void* data) {
	win->userdata = data;
}
void* swa_window_get_userdata(struct swa_window* win) {
	return win->userdata;
}

// data offer api
void swa_data_offer_destroy(struct swa_data_offer* offer) {
	if(offer) {
		offer->impl->destroy(offer);
	}
}
bool swa_data_offer_formats(struct swa_data_offer* offer,
		swa_formats_handler cb) {
	return offer->impl->formats(offer, cb);
}
bool swa_data_offer_data(struct swa_data_offer* offer,
		const char* format, swa_data_handler cb) {
	return offer->impl->data(offer, format, cb);
}
void swa_data_offer_set_preferred(struct swa_data_offer* offer,
		const char* format, enum swa_data_action action) {
	offer->impl->set_preferred(offer, format, action);
}
enum swa_data_action swa_data_offer_action(struct swa_data_offer* offer) {
	return offer->impl->action(offer);
}
enum swa_data_action swa_data_offer_supported_actions(struct swa_data_offer* offer) {
	return offer->impl->supported_actions(offer);
}
void swa_data_offer_set_userdata(struct swa_data_offer* offer, void* data) {
	offer->userdata = data;
}
void* swa_data_offer_get_userdata(struct swa_data_offer* offer) {
	return offer->userdata;
}

// key information
const struct {
	enum swa_key key;
	const char* name;
	bool textual;
} key_infos[] = {
	{swa_key_escape, "escape", false},

	{swa_key_k1, "1", true},
	{swa_key_k2, "2", true},
	{swa_key_k3, "3", true},
	{swa_key_k4, "4", true},
	{swa_key_k5, "5", true},
	{swa_key_k6, "6", true},
	{swa_key_k7, "7", true},
	{swa_key_k8, "8", true},
	{swa_key_k9, "9", true},
	{swa_key_k0, "0", true},
	{swa_key_minus, "minus", true},
	{swa_key_equals, "equals", true},
	{swa_key_backspace, "backspace", false},
	{swa_key_tab, "tab", true},

	{swa_key_q, "q", true},
	{swa_key_w, "w", true},
	{swa_key_e, "e", true},
	{swa_key_r, "r", true},
	{swa_key_t, "t", true},
	{swa_key_y, "y", true},
	{swa_key_u, "u", true},
	{swa_key_i, "i", true},
	{swa_key_o, "o", true},
	{swa_key_p, "p", true},
	{swa_key_leftbrace, "leftbrace", true},
	{swa_key_rightbrace, "rightbrace", true},
	{swa_key_enter, "enter", false},
	{swa_key_leftctrl, "leftctrl", false},

	{swa_key_a, "a", true},
	{swa_key_s, "s", true},
	{swa_key_d, "d", true},
	{swa_key_f, "f", true},
	{swa_key_g, "g", true},
	{swa_key_h, "h", true},
	{swa_key_j, "j", true},
	{swa_key_k, "k", true},
	{swa_key_l, "l", true},
	{swa_key_semicolon, "semicolon", true},
	{swa_key_apostrophe, "apostrophe", true},
	{swa_key_grave, "grave", true},
	{swa_key_leftshift, "leftshift", false},
	{swa_key_backslash, "backslash", true},

	{swa_key_x, "x", true},
	{swa_key_z, "z", true},
	{swa_key_c, "c", true},
	{swa_key_v, "v", true},
	{swa_key_b, "b", true},
	{swa_key_m, "m", true},
	{swa_key_n, "n", true},
	{swa_key_comma, "comma", true},
	{swa_key_period, "period", true},
	{swa_key_slash, "slash", true},
	{swa_key_rightshift, "rightshift", false},
	{swa_key_kpmultiply, "kpmultiply", true},
	{swa_key_leftalt, "leftalt", false},
	{swa_key_space, "space", true},
	{swa_key_capslock, "capslock", false},

	{swa_key_f1, "f1", false},
	{swa_key_f2, "f2", false},
	{swa_key_f3, "f3", false},
	{swa_key_f4, "f4", false},
	{swa_key_f5, "f5", false},
	{swa_key_f6, "f6", false},
	{swa_key_f7, "f7", false},
	{swa_key_f8, "f8", false},
	{swa_key_f9, "f9", false},
	{swa_key_f10, "f10", false},
	{swa_key_numlock, "numlock", false},
	{swa_key_scrollock, "scrolllock", false},

	{swa_key_kp7, "kp7", true},
	{swa_key_kp8, "kp8", true},
	{swa_key_kp9, "kp9", true},
	{swa_key_kpminus, "kpminus", true},
	{swa_key_kp4, "kp4", true},
	{swa_key_kp5, "kp5", true},
	{swa_key_kp6, "kp6", true},
	{swa_key_kpplus, "kpplus", true},
	{swa_key_kp1, "kp1", true},
	{swa_key_kp2, "kp2", true},
	{swa_key_kp3, "kp3", true},
	{swa_key_kp0, "kp0", true},
	{swa_key_kpperiod, "kpperiod", true},

	{swa_key_zenkakuhankaku, "zenkakuhankaku", false},
	{swa_key_102nd, "102nd", false},
	{swa_key_f11, "f11", false},
	{swa_key_f12, "f12", false},

	{swa_key_katakana, "katakana", false},
	{swa_key_hiragana, "hiragana", false},
	{swa_key_henkan, "henkan", false},
	{swa_key_katakanahiragana, "katakanahiragana", false},
	{swa_key_muhenkan, "muhenkan", false},
	{swa_key_kpjpcomma, "kpjpcomma", false},
	{swa_key_kpenter, "kpenter", false},
	{swa_key_rightctrl, "rightctrl", false},
	{swa_key_kpdivide, "kpdivide", false},
	{swa_key_sysrq, "sysrq", false},
	{swa_key_rightalt, "rightalt", false},
	{swa_key_linefeed, "linefeed", false},
	{swa_key_home, "home", false},
	{swa_key_up, "up", false},
	{swa_key_pageup, "pageup", false},
	{swa_key_left, "left", false},
	{swa_key_right, "right", false},
	{swa_key_end, "end", false},
	{swa_key_down, "down", false},
	{swa_key_pagedown, "pagedown", false},
	{swa_key_insert, "insert", false},
	{swa_key_del, "delete", false},
	{swa_key_macro, "macro", false},
	{swa_key_mute, "mute", false},
	{swa_key_volumedown, "volumedown", false},
	{swa_key_volumeup, "volumeup", false},
	{swa_key_power, "power", false},
	{swa_key_kpequals, "kpequals", false},
	{swa_key_kpplusminus, "kpplusminus", false},
	{swa_key_pause, "pause", false},
	{swa_key_scale, "scale", false},

	{swa_key_kpcomma, "kpcomma", true},
	{swa_key_hangeul, "hangeul", false},
	{swa_key_hanguel, "hanguel", false},
	{swa_key_hanja, "hanja", false},
	{swa_key_yen, "yen", false},
	{swa_key_leftmeta, "leftmeta", false},
	{swa_key_rightmeta, "rightmeta", false},
	{swa_key_compose, "compose", false},

	{swa_key_stop, "stop", false},
	{swa_key_again, "again", false},
	{swa_key_props, "props", false},
	{swa_key_undo, "undo", false},
	{swa_key_front, "front", false},
	{swa_key_copy, "copy", false},
	{swa_key_open, "open", false},
	{swa_key_paste, "paste", false},
	{swa_key_find, "find", false},
	{swa_key_cut, "cut", false},
	{swa_key_help, "help", false},
	{swa_key_menu, "menu", false},
	{swa_key_calc, "calc", false},
	{swa_key_setup, "setup", false},
	{swa_key_sleep, "sleep", false},
	{swa_key_wakeup, "wakeup", false},
	{swa_key_file, "file", false},
	{swa_key_sendfile, "sendfile", false},
	{swa_key_deletefile, "sendfildeletefile", false},
	{swa_key_xfer, "xfer", false},
	{swa_key_prog1, "prog1", false},
	{swa_key_prog2, "prog2", false},
	{swa_key_www, "www", false},
	{swa_key_msdos, "msdos", false},
	{swa_key_coffee, "coffee", false},
	{swa_key_screenlock, "screenlock", false},
	{swa_key_rotate_display, "rotateDisplay", false},
	{swa_key_direction, "direction", false},
	{swa_key_cyclewindows, "cyclewindows", false},
	{swa_key_mail, "mail", false},
	{swa_key_bookmarks, "bookmarks", false},
	{swa_key_computer, "computer", false},
	{swa_key_back, "back", false},
	{swa_key_forward, "forward", false},
	{swa_key_closecd, "closecd", false},
	{swa_key_ejectcd, "ejectcd", false},
	{swa_key_ejectclosecd, "ejectclosecd", false},
	{swa_key_nextsong, "nextsong", false},
	{swa_key_playpause, "playpause", false},
	{swa_key_previoussong, "previoussong", false},
	{swa_key_stopcd, "stopcd", false},
	{swa_key_record, "record", false},
	{swa_key_rewind, "rewind", false},
	{swa_key_phone, "rewinphone", false},
	{swa_key_iso, "iso", false},
	{swa_key_config, "config", false},
	{swa_key_homepage, "homepage", false},
	{swa_key_refresh, "refresh", false},
	{swa_key_exit, "exit", false},
	{swa_key_move, "move", false},
	{swa_key_edit, "edit", false},
	{swa_key_scrollup, "scrollup", false},
	{swa_key_scrolldown, "scrolldown", false},
	{swa_key_kpleftparen, "kpleftparen", false},
	{swa_key_kprightparen, "kprightparen", false},
	{swa_key_knew, "knew", false},
	{swa_key_redo, "redo", false},

	{swa_key_f13, "f13", false},
	{swa_key_f14, "f14", false},
	{swa_key_f15, "f15", false},
	{swa_key_f16, "f16", false},
	{swa_key_f17, "f17", false},
	{swa_key_f18, "f18", false},
	{swa_key_f19, "f19", false},
	{swa_key_f20, "f20", false},
	{swa_key_f21, "f21", false},
	{swa_key_f22, "f22", false},
	{swa_key_f23, "f23", false},
	{swa_key_f24, "f24", false},

	{swa_key_playcd, "playcd", false},
	{swa_key_pausecd, "pausecd", false},
	{swa_key_prog3, "prog3", false},
	{swa_key_prog4, "prog4", false},
	{swa_key_dashboard, "dashboard", false},
	{swa_key_suspend, "suspend", false},
	{swa_key_close, "close", false},
	{swa_key_play, "play", false},
	{swa_key_fastforward, "fastforward", false},
	{swa_key_bassboost, "bassboost", false},
	{swa_key_print, "print", false},
	{swa_key_hp, "hp", false},
	{swa_key_camera, "camera", false},
	{swa_key_sound, "sound", false},
	{swa_key_question, "question", false},
	{swa_key_email, "email", false},
	{swa_key_chat, "chat", false},
	{swa_key_search, "search", false},
	{swa_key_connect, "connect", false},
	{swa_key_finance, "finance", false},
	{swa_key_sport, "sport", false},
	{swa_key_shop, "shop", false},
	{swa_key_alterase, "alterase", false},
	{swa_key_cancel, "cancel", false},
	{swa_key_brightnessdown, "brightnessdown", false},
	{swa_key_brightnessup, "brightnessup", false},
	{swa_key_media, "media", false},

	{swa_key_switchvideomode, "switchvideomode", false},
	{swa_key_kbdillumtoggle, "kbdillumtoggle", false},
	{swa_key_kbdillumdown, "kbdillumdown", false},
	{swa_key_kbdillumup, "kbdillumup", false},

	{swa_key_send, "send", false},
	{swa_key_reply, "reply", false},
	{swa_key_forwardmail, "forwardmail", false},
	{swa_key_save, "save", false},
	{swa_key_documents, "documents", false},
	{swa_key_battery, "battery", false},
	{swa_key_bluetooth, "bluetooth", false},
	{swa_key_wlan, "wlan", false},
	{swa_key_uwb, "uwb", false},
	{swa_key_unknown, "unknown", false},
	{swa_key_video_next, "video_ext", false},
	{swa_key_video_prev, "video_prev", false},
	{swa_key_brightness_cycle, "brightness_cycle", false},
	{swa_key_brightness_auto, "brightness_auto", false},
	{swa_key_brightness_zero, "brightness_zero", false},
	{swa_key_display_off, "display_off", false},
	{swa_key_wwan, "wwan", false},
	{swa_key_wimax, "wimax", false},
	{swa_key_rfkill, "rfkill", false},
	{swa_key_micmute, "micmute", false},
};

const char* swa_key_to_name(enum swa_key key) {
	unsigned s = sizeof(key_infos) / sizeof(key_infos[0]);
	for(unsigned i = 0u; i < s; ++i) {
		if(key_infos[i].key == key) {
			return key_infos[i].name;
		}
	}
	return "<invalid>";
}

enum swa_key swa_key_from_name(const char* name) {
	if(!name) {
		return swa_key_none;
	}

	unsigned s = sizeof(key_infos) / sizeof(key_infos[0]);
	for(unsigned i = 0u; i < s; ++i) {
		if(!strcmp(name, key_infos[i].name)) {
			return key_infos[i].key;
		}
	}

	return swa_key_none;
}

bool swa_key_is_textual(enum swa_key key) {
	if(key == swa_key_none) {
		return false;
	}

	unsigned s = sizeof(key_infos) / sizeof(key_infos[0]);
	for(unsigned i = 0u; i < s; ++i) {
		if(key == key_infos[i].key) {
			return key_infos[i].textual;
		}
	}
	return false;
}
