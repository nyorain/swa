#include <swa/swa.h>
#include <dlg/dlg.h>
#include <string.h>

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

static void window_key(struct swa_window* win, const struct swa_key_event* ev) {
	dlg_info("key: %d %s -> %s", ev->keycode,
		ev->pressed ? (ev->repeated ? "repeated" : "pressed") : "release",
		ev->utf8 ? ev->utf8 : "<no text>");
}

const static struct swa_window_listener window_listener = {
	.draw = window_draw,
	.key = window_key,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate();

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.app_name = "swa-example";
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	swa_display_create_window(dpy, &settings);

	while(true) {
		if(!swa_display_wait_events(dpy)) {
			break;
		}
	}
}
