#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <GL/gl.h>

static bool run = true;
static void window_draw(struct swa_window* win) {
	if(!swa_window_gl_make_current(win)) {
		return;
	}

	float alpha = 0.2;
	glClearColor(alpha * 0.9, alpha * 1.0, alpha * 0.85, alpha);
	glClear(GL_COLOR_BUFFER_BIT);
	swa_window_gl_swap_buffers(win);
}

static void window_close(struct swa_window* win) {
	run = false;
}

static void window_mouse_button(struct swa_window* win,
		const struct swa_mouse_button_event* ev) {
	if(ev->pressed && ev->button == swa_mouse_button_left) {
		dlg_debug("begin resize");
		swa_window_begin_resize(win, swa_edge_bottom_right);
	}
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.mouse_button = window_mouse_button,
	.close = window_close,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate();
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	struct swa_cursor cursor;
	cursor.type = swa_cursor_load;

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.app_name = "swa-example";
	settings.title = "swa-example-window";
	settings.surface = swa_surface_gl;
	settings.surface_settings.gl.major = 4;
	settings.surface_settings.gl.minor = 0;
	settings.listener = &window_listener;
	settings.cursor = cursor;
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
