#include <swa/impl.h>
#include <dlg/dlg.h>
#include <stdlib.h>
#include <string.h>

struct swa_display* swa_display_autocreate(void) {
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

void swa_display_destroy(struct swa_display* dpy) {
	if(!dpy) {
		return;
	}

	dpy->impl->destroy(dpy);
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
struct swa_window* swa_display_create_window(struct swa_display*,
	struct swa_window_settings);

// window api
void swa_window_destroy(struct swa_window*);
enum swa_window_cap swa_window_get_capabilities(struct swa_window*);
void swa_window_set_min_size(struct swa_window*, unsigned w, unsigned h);
void swa_window_set_max_size(struct swa_window*, unsigned w, unsigned h);
void swa_window_show(struct swa_window*, bool show);

// Calling this function will not emit a size event.
void swa_window_set_size(struct swa_window*, unsigned w, unsigned h);
void swa_window_set_position(struct swa_window*, int x, int y);
void swa_window_set_cursor(struct swa_window*, struct swa_cursor cursor);
void swa_window_refresh(struct swa_window*);
void swa_window_surface_frame(struct swa_window*);

// Calling this function will not emit a state event.
void swa_window_set_state(struct swa_window*, enum swa_window_state);
void swa_window_begin_move(struct swa_window*, void* trigger_event_data);
void swa_window_begin_resize(struct swa_window*, enum swa_edge edges, void* trigger_event_data);
void swa_window_set_title(struct swa_window*, const char*);

// The passed image must not remain valid after this call.
// Can pass NULL to unset the icon.
void swa_window_set_icon(struct swa_window*, const struct swa_image* image);

// Returns whether the window should be decorated by the user.
// This can either be the case because the backend uses client-side decorations
// by default or because the window was explicitly created with client decorations
// (and those are supported by the backend as well).
bool swa_window_is_client_decorated(struct swa_window*);

// The objects must remain valid until it is changed or the window is destroyed.
void swa_window_set_listener(struct swa_window*, const struct swa_window_listener*);
void swa_window_set_userdata(struct swa_window*, void* data);
void* swa_window_get_userdata(struct swa_window*);

// Only valid if the window was created with surface set to vk.
// The surface will automatically be destroyed when the window is destroyed.
// Note that the surface might get lost on some platforms, signaled by the
// surface_destroyed event.
bool swa_window_get_vulkan_surface(struct swa_window*, void* vkSurfaceKHR);

// Only valid if the window was created with surface set to gl.
bool swa_window_gl_make_current(struct swa_window*);
bool swa_window_gl_swap_buffers(struct swa_window*);
bool swa_window_gl_set_swap_interval(struct swa_window*);

// Only valid if the window was created with surface set to buffer.
// Implementations might use multiple buffers to avoid flickering, i.e.
// the caller should not expect two calls to `get_buffer` ever to
// return the same image.
bool swa_window_get_buffer(struct swa_window*, struct swa_image*);

// Sets the window contents to the image data stored in the image
// returned by the last call to `get_buffer`.
// For each call of `apply_buffer`, there must have been a previous
// call to `get_buffer`.
void swa_window_apply_buffer(struct swa_window*);

bool swa_data_offer_types(struct swa_data_offer*, swa_formats_handler cb, void* data);
bool swa_data_offer_data(struct swa_data_offer*, const char* format, swa_data_handler cb, void* data);
void swa_data_offer_set_preferred(struct swa_data_offer*, const char* foramt, enum swa_data_action action);
enum swa_data_action swa_data_offer_action(struct swa_data_offer*);
enum swa_data_action swa_data_offer_supported_actions(struct swa_data_offer*);
