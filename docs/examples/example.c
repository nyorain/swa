#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>

static bool run = true;
static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	unsigned size = img.height * img.stride;
	memset(img.data, 255, size);

	swa_window_apply_buffer(win);
}

static void window_close(struct swa_window* win) {
	run = false;
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate("swa example");
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	settings.transparent = false;
	struct swa_window* win = swa_display_create_window(dpy, &settings);
	if(!win) {
		dlg_fatal("Failed to create window");
		swa_display_destroy(dpy);
		return EXIT_FAILURE;
	}

	swa_window_set_userdata(win, dpy);

	while(run) {
		if(!swa_display_dispatch(dpy, true)) {
			break;
		}
	}

	swa_window_destroy(win);
	swa_display_destroy(dpy);
}
