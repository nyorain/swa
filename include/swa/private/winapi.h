#pragma once
#define UNICODE

#include <swa/swa.h>
#include <swa/private/impl.h>

#include <windows.h>
#include <winuser.h>
#include <windowsx.h>
#include <Dwmapi.h>

// undefine the shittiest macros
// holy fuck microsoft...
#undef near
#undef far
#undef ERROR
#undef MemoryBarrier
#undef UNICODE

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_win {
	struct swa_display base;
	HWND dummy_window;

	// The thread this display was created in.
	// Since many winapi functions are thread-dependent, we
	// require that all functions (except wakup_wait) have
	// to be called from this thread.
	DWORD main_thread_id;
	bool error;

	struct swa_window_win* focus;
	struct swa_window_win* mouse_over;
	struct wgl_api* wgl;

	int mx, my; // last mouse coords
};

struct swa_win_buffer_surface {
	void* data;
	unsigned width;
	unsigned height;
	HBITMAP bitmap;
	bool active;
	HDC wdc; // only set when buffer is active
};

struct swa_win_vk_surface {
	uintptr_t instance;
	uint64_t surface;
};

struct swa_window_win {
	struct swa_window base;
	struct swa_display_win* dpy;
	HWND handle;

	unsigned width, height;
	unsigned min_width, min_height;
	unsigned max_width, max_height;

	enum swa_surface_type surface_type;
	union {
		struct swa_win_buffer_surface buffer;
		struct swa_win_vk_surface vk;
		void* gl_context;
	};

	struct {
		HCURSOR handle;
		bool owned;
		bool set;
	} cursor;
};

struct swa_data_offer_win {
	struct swa_data_offer base;
	struct swa_display_win* dpy;
};

struct swa_display* swa_display_win_create(const char* appname);

// wgl utility
bool swa_wgl_init_context(struct swa_display_win* dpy, HDC hdc,
	const struct swa_gl_surface_settings* gls, bool transparent,
	void** out_context);

#ifdef __cplusplus
}
#endif
