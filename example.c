#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>

static bool run = true;
static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	dlg_info("drawing window, size: %d %d", img.width, img.height);
	unsigned size = img.height * img.stride;
	memset(img.data, 255, size);
	swa_window_apply_buffer(win);
}

static const char* mime_utf8 = "text/plain;charset=utf-8";
static void clipboard_text(struct swa_data_offer* offer, const char* format,
		struct swa_exchange_data data) {
	dlg_assert(!strcmp(format, mime_utf8));
	char buf[256];
	unsigned size = data.size < 256 ? data.size : 256;
	memcpy(buf, data.data, size);
	dlg_info("clipboard:\n==== begin ====\n%s\n==== end ====", buf);

	// TODO: fix this situation
	// swa_data_offer_destroy(offer);
}

static void clipboard_formats(struct swa_data_offer* offer, const char** formats,
		unsigned n_formats) {
	for(unsigned i = 0u; i < n_formats; ++i) {
		dlg_debug("format: %s", formats[i]);
		if(strcmp(formats[i], mime_utf8) == 0) {
			swa_data_offer_data(offer, formats[i], clipboard_text);
			break;
		}
	}
}

static void window_key(struct swa_window* win, const struct swa_key_event* ev) {
	dlg_info("key: %d %s -> %s", ev->keycode,
		ev->pressed ? (ev->repeated ? "repeated" : "pressed") : "release",
		ev->utf8 ? ev->utf8 : "<no text>");
	bool ctrl = ev->modifiers & swa_keyboard_mod_ctrl;
	if(ev->pressed && ev->keycode == swa_key_v && ctrl) {
		dlg_debug("ctrl+v");
		struct swa_display* dpy = swa_window_get_userdata(win);
		struct swa_data_offer* offer = swa_display_get_clipboard(dpy);
		if(offer) {
			dlg_debug("requesting data offer formats");
			swa_data_offer_formats(offer, clipboard_formats);
		} else {
			dlg_debug("no clipboard data available");
		}
	}
}

static void window_close(struct swa_window* win) {
	run = false;
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.key = window_key,
	.close = window_close,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate();

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.app_name = "swa-example";
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	struct swa_window* win = swa_display_create_window(dpy, &settings);
	swa_window_set_userdata(win, dpy);

	while(run) {
		if(!swa_display_wait_events(dpy)) {
			break;
		}
	}

	swa_window_destroy(win);
	swa_display_destroy(dpy);
}
