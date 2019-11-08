#include <swa/impl.h>
#include <swa/wayland.h>
#include <dlg/dlg.h>
#include <stdlib.h>
#include <string.h>

struct swa_display* swa_display_autocreate(void) {
	return swa_display_wl_create();
}

void swa_window_settings_default(struct swa_window_settings* settings) {
	memset(settings, 0, sizeof(*settings));
	settings->cursor.type = swa_cursor_default;
	settings->app_name = "swapplication";
	settings->title = "swa_window";
	settings->state = swa_window_state_normal;
	settings->x = settings->y = SWA_DEFAULT_POSITION;
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

struct pixel {
	uint8_t r, g, b, a;
};

static void write_pixel(uint8_t* data, enum swa_image_format fmt,
		struct pixel pixel) {
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

static struct pixel read_pixel(const uint8_t* data, enum swa_image_format fmt) {
	switch(fmt) {
		case swa_image_format_rgba32:
			return (struct pixel){data[0], data[1], data[2], data[3]};
		case swa_image_format_rgb24:
			return (struct pixel){data[0], data[1], data[2], 255};
		case swa_image_format_bgr24:
			return (struct pixel){data[2], data[1], data[0], 255};
		case swa_image_format_xrgb32:
			return (struct pixel){data[1], data[2], data[3], 255};
		case swa_image_format_argb32:
			return (struct pixel){data[1], data[2], data[3], data[0]};
		case swa_image_format_abgr32:
			return (struct pixel){data[3], data[2], data[1], data[0]};
		case swa_image_format_bgra32:
			return (struct pixel){data[2], data[1], data[0], data[3]};
		case swa_image_format_bgrx32:
			return (struct pixel){data[2], data[1], data[0], 255};
		case swa_image_format_a8:
			return (struct pixel){data[0], data[0], data[0], data[0]};
		case swa_image_format_none:
			return (struct pixel){0, 0, 0, 0};
	}

	// unreachable for valid formats
	// dont' put it in default so we get warnings about unhandled enum values
	dlg_error("Invalid image format %d", fmt);
}

void swa_convert_image(const struct swa_image* src, const struct swa_image* dst) {
	dlg_assert(dst->width == src->width);
	dlg_assert(dst->height == src->height);

	unsigned src_size = swa_image_format_size(src->format);
	unsigned dst_size = swa_image_format_size(dst->format);

	const uint8_t* src_data = src->data;
	uint8_t* dst_data = dst->data;
	for(unsigned y = 0u; y < src->height; ++y) {
		for(unsigned x = 0u; x < src->width; ++x) {
			struct pixel pixel = read_pixel(src_data, src->format);
			write_pixel(dst_data, src->format, pixel);
			src_data += src_size;
			dst_data += dst_size;
		}
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
bool swa_display_poll_events(struct swa_display* dpy) {
	return dpy->impl->poll_events(dpy);
}
bool swa_display_wait_events(struct swa_display* dpy) {
	return dpy->impl->wait_events(dpy);
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
		struct swa_data_source* source,
		void* trigger_event_data) {
	return dpy->impl->set_clipboard(dpy, source, trigger_event_data);
}
bool swa_display_start_dnd(struct swa_display* dpy,
		struct swa_data_source* source,
		void* trigger_event_data) {
	return dpy->impl->start_dnd(dpy, source, trigger_event_data);
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
void swa_window_set_position(struct swa_window* win, int x, int y) {
	win->impl->set_position(win, x, y);
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
void swa_window_begin_move(struct swa_window* win, void* trigger) {
	win->impl->begin_move(win, trigger);
}
void swa_window_begin_resize(struct swa_window* win, enum swa_edge edges,
		void* trigger) {
	win->impl->begin_resize(win, edges, trigger);
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
bool swa_window_get_vk_surface(struct swa_window* win, void* vkSurfaceKHR) {
	return win->impl->get_vk_surface(win, vkSurfaceKHR);
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
	return win->impl->apply_buffer(win);
}
void swa_window_set_listener(struct swa_window* win,
		const struct swa_window_listener* listener) {
	win->listener = listener;
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
