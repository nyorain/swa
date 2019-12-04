#pragma once

#include <swa/swa.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_interface {
	void (*destroy)(struct swa_display*);
	bool (*dispatch)(struct swa_display*, bool block);
	void (*wakeup)(struct swa_display*);
	enum swa_display_cap (*capabilities)(struct swa_display*);
	const char** (*vk_extensions)(struct swa_display*, unsigned* count);
	bool (*key_pressed)(struct swa_display*, enum swa_key);
	const char* (*key_name)(struct swa_display*, enum swa_key);
	enum swa_keyboard_mod (*active_keyboard_mods)(struct swa_display*);
	struct swa_window* (*get_keyboard_focus)(struct swa_display*);
	bool (*mouse_button_pressed)(struct swa_display*, enum swa_mouse_button);
	void (*mouse_position)(struct swa_display*, int* x, int* y);
	struct swa_window* (*get_mouse_over)(struct swa_display*);
	struct swa_data_offer* (*get_clipboard)(struct swa_display*);
	bool (*set_clipboard)(struct swa_display*, struct swa_data_source*);
	bool (*start_dnd)(struct swa_display*, struct swa_data_source*);
	struct swa_window* (*create_window)(struct swa_display*,
		const struct swa_window_settings*);
	swa_gl_proc (*get_gl_proc_addr)(struct swa_display*, const char*);
};

struct swa_window_interface {
	void (*destroy)(struct swa_window*);
	enum swa_window_cap (*get_capabilities)(struct swa_window*);
	void (*set_min_size)(struct swa_window*, unsigned w, unsigned h);
	void (*set_max_size)(struct swa_window*, unsigned w, unsigned h);
	void (*show)(struct swa_window*, bool show);

	void (*set_size)(struct swa_window*, unsigned w, unsigned h);
	void (*set_cursor)(struct swa_window*, struct swa_cursor cursor);
	void (*refresh)(struct swa_window*);
	void (*surface_frame)(struct swa_window*);

	void (*set_state)(struct swa_window*, enum swa_window_state);
	void (*begin_move)(struct swa_window*);
	void (*begin_resize)(struct swa_window*, enum swa_edge edges);
	void (*set_title)(struct swa_window*, const char*);

	void (*set_icon)(struct swa_window*, const struct swa_image* image);
	bool (*is_client_decorated)(struct swa_window*);
	uint64_t (*get_vk_surface)(struct swa_window*);

	bool (*gl_make_current)(struct swa_window*);
	bool (*gl_swap_buffers)(struct swa_window*);
	bool (*gl_set_swap_interval)(struct swa_window*, int interval);

	bool (*get_buffer)(struct swa_window*, struct swa_image*);
	void (*apply_buffer)(struct swa_window*);
};

struct swa_data_offer_interface {
	void (*destroy)(struct swa_data_offer*);
	bool (*formats)(struct swa_data_offer*, swa_formats_handler cb);
	bool (*data)(struct swa_data_offer*, const char* format, swa_data_handler cb);
	void (*set_preferred)(struct swa_data_offer*, const char* format,
		enum swa_data_action action);
	enum swa_data_action (*action)(struct swa_data_offer*);
	enum swa_data_action (*supported_actions)(struct swa_data_offer*);
};

struct swa_display {
	const struct swa_display_interface* impl;
};

struct swa_window {
	const struct swa_window_interface* impl;
	const struct swa_window_listener* listener;
	void* userdata;
};

struct swa_data_offer {
	const struct swa_data_offer_interface* impl;
	void* userdata;
};

#ifdef __cplusplus
}
#endif
