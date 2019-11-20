#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>

static bool run = true;
struct timespec last_redraw;

static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	struct timespec now;
	// clock_gettime(CLOCK_MONOTONIC, &now);
	timespec_get(&now, TIME_UTC);
	float ms = (now.tv_nsec - last_redraw.tv_nsec) / (1000.f * 1000.f);
	ms += 1000.f * (now.tv_sec - last_redraw.tv_sec);
	dlg_info("Time between redraws: %f", ms);
	last_redraw = now;

	dlg_info("drawing window, size: %d %d", img.width, img.height);

	for(unsigned y = 0u; y < img.height; ++y) {
		for(unsigned x = 0u; x < img.width; ++x) {
			unsigned off = (y * img.stride + 4 * x);
			img.data[off + 0] = 128; // b
			img.data[off + 1] = (uint8_t) (255 * ((float) y) / img.height); // g
			img.data[off + 2] = (uint8_t) (255 * ((float) x) / img.width); // r
			img.data[off + 3] = 100; // a
		}
	}

	swa_window_apply_buffer(win);
	swa_window_refresh(win);
}

static void window_close(struct swa_window* win) {
	run = false;
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate();
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	struct swa_cursor cursor;
	cursor.type = swa_cursor_right_pointer;

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.app_name = "swa-example";
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	settings.cursor = cursor;
	struct swa_window* win = swa_display_create_window(dpy, &settings);
	if(!win) {
		dlg_fatal("Failed to create window");
		swa_display_destroy(dpy);
		return EXIT_FAILURE;
	}

	swa_window_set_userdata(win, dpy);
	timespec_get(&last_redraw, TIME_UTC);

	while(run) {
		if(!swa_display_dispatch(dpy, true)) {
			break;
		}
	}

	swa_window_destroy(win);
	swa_display_destroy(dpy);
}
