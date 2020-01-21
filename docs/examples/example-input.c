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

static void window_focus(struct swa_window* win, bool gained) {
	dlg_info("focus %s", gained ? "gained" : "lost");
}

static void key(struct swa_window* win, const struct swa_key_event* ev) {
	dlg_info("key %d %s %s: utf8 %s", ev->keycode,
		ev->pressed ? "pressed" : "released",
		ev->repeated ? "(repeated)" : "",
		ev->utf8 ? ev->utf8 : "<none>");

	if(ev->pressed && ev->keycode == swa_key_escape) {
		dlg_info("Escape pressed, exiting");
		run = false;
	}
}

static void mouse_move(struct swa_window* win,
		const struct swa_mouse_move_event* ev) {
	dlg_info("mouse moved to (%d, %d)", ev->x, ev->y);
}

static void mouse_cross(struct swa_window* win,
		const struct swa_mouse_cross_event* ev) {
	dlg_info("mouse %s at (%d, %d)", ev->entered ? "entered" : "left",
		ev->x, ev->y);
}

static void mouse_button(struct swa_window* win,
		const struct swa_mouse_button_event* ev) {
	dlg_info("mouse button: button = %d %s, pos = (%d, %d)",
		ev->button, ev->pressed ? "pressed" : "released",
		ev->x, ev->y);
}

static void mouse_wheel(struct swa_window* win,
		float dx, float dy) {
	dlg_info("mouse wheel: (%f, %f)", dx, dy);
}

static void touch_begin(struct swa_window* win,
		const struct swa_touch_event* ev) {
	dlg_info("touch begin: id = %d, pos = (%d, %d)",
		ev->id, ev->x, ev->y);
}

static void touch_update(struct swa_window* win,
		const struct swa_touch_event* ev) {
	dlg_info("touch update: id = %d, pos = (%d, %d)",
		ev->id, ev->x, ev->y);
}

static void touch_end(struct swa_window* win, unsigned id) {
	dlg_info("touch end: id = %d", id);
}

static void touch_cancel(struct swa_window* win) {
	dlg_info("touch cancel");
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
	.key = key,
	.mouse_move = mouse_move,
	.mouse_cross = mouse_cross,
	.mouse_button = mouse_button,
	.mouse_wheel = mouse_wheel,
	.touch_begin = touch_begin,
	.touch_update = touch_update,
	.touch_end = touch_end,
	.touch_cancel = touch_cancel,
	.focus = window_focus,
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

