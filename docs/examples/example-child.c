#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>

struct state {
	bool run;
	bool show_child;
	struct swa_display* dpy;
	struct swa_window* parent;
	struct swa_window* child;
	unsigned width, height;
};

static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	struct state* state = swa_window_get_userdata(win);

	dlg_info("drawing window %p, size: %d %d", (void*) win, img.width, img.height);

	unsigned size = img.height * img.stride;
	if(win == state->parent) {
		unsigned fmt_size = swa_image_format_size(img.format);
		for(unsigned y = 0u; y < img.height; ++y) {
			for(unsigned x = 0u; x < img.width; ++x) {
				struct swa_pixel pixel = {
					.r = (uint8_t) (255 * ((float) x) / img.width),
					.g = (uint8_t) (255 * ((float) y) / img.height),
					.b = 128,
					.a = 210,
				};

				unsigned off = (y * img.stride + x * fmt_size);
				swa_write_pixel(&img.data[off], img.format, pixel);
			}
		}
	} else {
		memset(img.data, 0x88, size);
	}

	swa_window_apply_buffer(win);
}

static void window_close(struct swa_window* win) {
	struct state* state = swa_window_get_userdata(win);
	if(win == state->parent) {
		state->run = false;
	}
}

static void window_key(struct swa_window* win, const struct swa_key_event* ev) {
	struct state* state = swa_window_get_userdata(win);
	if(ev->pressed && win == state->parent) {
		if(ev->keycode == swa_key_escape) {
			dlg_info("Escape pressed, exiting");
			state->run = false;
		} else if(ev->keycode == swa_key_c) {
			state->show_child = !state->show_child;
			dlg_info("show child: %d", state->show_child);
			swa_window_show(state->child, state->show_child);
			if(state->show_child) {
				swa_window_set_size(state->child, state->width / 2, state->height);
			}
			swa_window_refresh(state->parent);
		}
	}

	dlg_info("key %d on %s: %d (%s)", ev->keycode,
		win == state->parent ? "parent" : "child", ev->pressed,
		ev->utf8 ? ev->utf8 : "<empty>");
}

static void window_resize(struct swa_window* win, uint32_t w, uint32_t h) {
	struct state* state = swa_window_get_userdata(win);
	if(win == state->child) {
		dlg_info("child resized: %u %u", w, h);
		return;
	}

	// make client window match size.
	if(state->show_child) {
		swa_window_set_size(state->child, w / 2, h);
	}

	state->width = w;
	state->height = h;
}

static void mouse_button(struct swa_window* win, const struct swa_mouse_button_event* ev) {
	struct state* state = swa_window_get_userdata(win);
	dlg_info("button %d on %s:%d:%d: %d", ev->button,
		win == state->parent ? "parent" : "child", ev->x, ev->y, ev->pressed);
}

static void mouse_cross(struct swa_window* win, const struct swa_mouse_cross_event* ev) {
	struct state* state = swa_window_get_userdata(win);
	dlg_info("mouse %s on %s", ev->entered ? "enter" : "leave",
		win == state->parent ? "parent" : "child");
}

static void window_focus(struct swa_window* win, bool gain) {
	struct state* state = swa_window_get_userdata(win);
	dlg_info("focus %s on %s", gain ? "enter" : "leave",
		win == state->parent ? "parent" : "child");
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
	.key = window_key,
	.resize = window_resize,
	.mouse_button = mouse_button,
	.focus = window_focus,
	.mouse_cross = mouse_cross,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate("swa example-buffer");
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	if(!(swa_display_capabilities(dpy) & swa_display_cap_child_windows)) {
		dlg_fatal("Display does not support child windows");
		return EXIT_FAILURE;
	}

	// Create parent window
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

	// create child window
	const bool initial_show = false;
	void* parent_handle = swa_window_native_handle(win);

	settings.parent = parent_handle;
	settings.width = 100;
	settings.height = 100;
	settings.hide = !initial_show;
	settings.surface = swa_surface_none;
	settings.input_only = true;
	struct swa_window* child = swa_display_create_window(dpy, &settings);
	if(!child) {
		dlg_fatal("Failed to create child");
		swa_display_destroy(dpy);
		return EXIT_FAILURE;
	}

	struct state state = {true, initial_show, dpy, win, child, 0, 0};
	swa_window_set_userdata(win, &state);
	swa_window_set_userdata(child, &state);

	while(state.run) {
		if(!swa_display_dispatch(dpy, true)) {
			break;
		}
	}

	dlg_trace("Destroying child window...");
	swa_window_destroy(child);
	dlg_trace("Destroying swa window...");
	swa_window_destroy(win);
	dlg_trace("Destroying swa display...");
	swa_display_destroy(dpy);
	dlg_trace("Exiting cleanly");
}

