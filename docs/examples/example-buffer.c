#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>

static bool run = true;
// struct timespec last_redraw;

static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	// struct timespec now;
	// clock_gettime(CLOCK_MONOTONIC, &now);
	// timespec_get(&now, TIME_UTC);
	// float ms = (now.tv_nsec - last_redraw.tv_nsec) / (1000.f * 1000.f);
	// ms += 1000.f * (now.tv_sec - last_redraw.tv_sec);
	// dlg_info("Time between redraws: %f", ms);
	// last_redraw = now;
	dlg_info("drawing window, size: %d %d", img.width, img.height);

	/*
	unsigned fmt_size = swa_image_format_size(img.format);
	for(unsigned y = 0u; y < img.height; ++y) {
		for(unsigned x = 0u; x < img.width; ++x) {
			struct swa_pixel pixel = {
				.r = (uint8_t) (255 * ((float) x) / img.width),
				.g = (uint8_t) (255 * ((float) y) / img.height),
				.b = 128,
				.a = 100,
			};

			unsigned off = (y * img.stride + x * fmt_size);
			swa_write_pixel(&img.data[off], img.format, pixel);
		}
	}
	*/

	unsigned size = img.height * img.stride;
	memset(img.data, 255, size);

	swa_window_apply_buffer(win);
	// swa_window_refresh(win);
}

static void window_close(struct swa_window* win) {
	run = false;
}

static void window_key(struct swa_window* win, const struct swa_key_event* ev) {
	if(ev->pressed && ev->keycode == swa_key_escape) {
		dlg_info("Escape pressed, exiting");
		run = false;
	}
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
	.key = window_key,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate("swa example-buffer");
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	// optional: set cursor
	struct swa_cursor cursor;
	cursor.type = swa_cursor_left_pointer;

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	settings.cursor = cursor;
	settings.transparent = true;
	struct swa_window* win = swa_display_create_window(dpy, &settings);
	if(!win) {
		dlg_fatal("Failed to create window");
		swa_display_destroy(dpy);
		return EXIT_FAILURE;
	}

	swa_window_set_userdata(win, dpy);
	// timespec_get(&last_redraw, TIME_UTC);

	while(run) {
		if(!swa_display_dispatch(dpy, true)) {
			break;
		}
	}

	dlg_trace("Destroying swa window...");
	swa_window_destroy(win);
	dlg_trace("Destroying swa display...");
	swa_display_destroy(dpy);
	dlg_trace("Exiting cleanly");
}
