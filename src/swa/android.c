#include <swa/android.h>
#include <swa/private/android.h>
#include <dlg/dlg.h>
#include <dlg/output.h>
#include <string.h>
#include <errno.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/keycodes.h>
#include <jni.h>

#if __ANDROID_API__ >= 24
  #include <android/choreographer.h>
#endif

#ifdef SWA_WITH_GL
  #include <swa/private/egl.h>
#endif

#ifdef SWA_WITH_VK
  #include <vulkan/vulkan.h>
  #include <vulkan/vulkan_android.h>
#endif

// NOTE: this is a reference to the application's entrypoint
extern int main(int argc, char** argv);

enum event_type {
	event_type_win_created = 1,
	event_type_win_destroyed,
	event_type_input_queue_created,
	event_type_input_queue_destroyed,
	event_type_focus,
	event_type_draw,
	event_type_resize,
};

struct event {
	enum event_type type;
	uint64_t data;
	bool* cond_wakeup;
};

// File-global storing the entrypoint data
struct activity {
	ANativeActivity* activity;
	ANativeWindow* window;
	AInputQueue* input_queue;

	pthread_t main_thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond; // TODO: move to display?
	bool thread_started;
	bool destroyed;
	struct swa_display_android* dpy;
};

static struct activity* g_current_activity;

static struct {
	int32_t android;
	enum swa_key keycode;
} keycode_map[] = {
	{AKEYCODE_0, swa_key_k0},
	{AKEYCODE_1, swa_key_k1},
	{AKEYCODE_2, swa_key_k2},
	{AKEYCODE_3, swa_key_k3},
	{AKEYCODE_4, swa_key_k4},
	{AKEYCODE_5, swa_key_k5},
	{AKEYCODE_6, swa_key_k6},
	{AKEYCODE_7, swa_key_k7},
	{AKEYCODE_8, swa_key_k8},
	{AKEYCODE_9, swa_key_k9},

	{AKEYCODE_A, swa_key_a},
	{AKEYCODE_B, swa_key_b},
	{AKEYCODE_C, swa_key_c},
	{AKEYCODE_D, swa_key_d},
	{AKEYCODE_E, swa_key_e},
	{AKEYCODE_F, swa_key_f},
	{AKEYCODE_G, swa_key_g},
	{AKEYCODE_H, swa_key_h},
	{AKEYCODE_I, swa_key_i},
	{AKEYCODE_J, swa_key_j},
	{AKEYCODE_K, swa_key_k},
	{AKEYCODE_L, swa_key_l},
	{AKEYCODE_M, swa_key_m},
	{AKEYCODE_N, swa_key_n},
	{AKEYCODE_O, swa_key_o},
	{AKEYCODE_P, swa_key_p},
	{AKEYCODE_Q, swa_key_q},
	{AKEYCODE_R, swa_key_r},
	{AKEYCODE_S, swa_key_s},
	{AKEYCODE_T, swa_key_t},
	{AKEYCODE_U, swa_key_u},
	{AKEYCODE_V, swa_key_v},
	{AKEYCODE_W, swa_key_w},
	{AKEYCODE_X, swa_key_x},
	{AKEYCODE_Y, swa_key_y},
	{AKEYCODE_Z, swa_key_z},

	{AKEYCODE_COMMA, swa_key_comma},
	{AKEYCODE_PERIOD, swa_key_period},
	{AKEYCODE_ALT_LEFT, swa_key_leftalt},
	{AKEYCODE_ALT_RIGHT, swa_key_rightalt},
	{AKEYCODE_SHIFT_RIGHT, swa_key_rightshift},
	{AKEYCODE_SHIFT_LEFT, swa_key_leftshift},
	{AKEYCODE_TAB, swa_key_tab},
	{AKEYCODE_SPACE, swa_key_space},

	// ...

	{AKEYCODE_ENTER, swa_key_enter},
	{AKEYCODE_DEL, swa_key_del},
	{AKEYCODE_GRAVE, swa_key_grave},
	{AKEYCODE_MINUS, swa_key_minus},
	{AKEYCODE_EQUALS, swa_key_equals},
	{AKEYCODE_LEFT_BRACKET, swa_key_leftbrace},
	{AKEYCODE_RIGHT_BRACKET, swa_key_rightbrace},
	{AKEYCODE_BACKSLASH, swa_key_backslash},
	{AKEYCODE_SEMICOLON, swa_key_semicolon},
	{AKEYCODE_APOSTROPHE, swa_key_apostrophe},
	{AKEYCODE_SLASH, swa_key_slash},

	// ...

	{AKEYCODE_ESCAPE, swa_key_escape},
	{AKEYCODE_CTRL_LEFT, swa_key_leftctrl},
	{AKEYCODE_CTRL_RIGHT, swa_key_rightctrl},
	{AKEYCODE_SCROLL_LOCK, swa_key_scrollock},
	{AKEYCODE_META_LEFT, swa_key_leftmeta},
	{AKEYCODE_META_RIGHT, swa_key_rightmeta},

	// ...

	{AKEYCODE_MEDIA_PLAY, swa_key_play},
	{AKEYCODE_MEDIA_PAUSE, swa_key_pause},
	{AKEYCODE_MEDIA_CLOSE, swa_key_close},

	// ...

	{AKEYCODE_F1, swa_key_f1},
	{AKEYCODE_F2, swa_key_f2},
	{AKEYCODE_F3, swa_key_f3},
	{AKEYCODE_F4, swa_key_f4},
	{AKEYCODE_F5, swa_key_f5},
	{AKEYCODE_F6, swa_key_f6},
	{AKEYCODE_F7, swa_key_f7},
	{AKEYCODE_F9, swa_key_f8},
	{AKEYCODE_F10, swa_key_f10},
	{AKEYCODE_F11, swa_key_f11},
	{AKEYCODE_F12, swa_key_f12},

	{AKEYCODE_NUM_LOCK, swa_key_numlock},
	{AKEYCODE_NUMPAD_0, swa_key_kp0},
	{AKEYCODE_NUMPAD_1, swa_key_kp1},
	{AKEYCODE_NUMPAD_2, swa_key_kp2},
	{AKEYCODE_NUMPAD_3, swa_key_kp3},
	{AKEYCODE_NUMPAD_4, swa_key_kp4},
	{AKEYCODE_NUMPAD_5, swa_key_kp5},
	{AKEYCODE_NUMPAD_6, swa_key_kp6},
	{AKEYCODE_NUMPAD_7, swa_key_kp7},
	{AKEYCODE_NUMPAD_8, swa_key_kp8},
	{AKEYCODE_NUMPAD_9, swa_key_kp9},
	{AKEYCODE_NUMPAD_DIVIDE, swa_key_kpdivide},
	{AKEYCODE_NUMPAD_MULTIPLY, swa_key_kpmultiply},
	{AKEYCODE_NUMPAD_SUBTRACT, swa_key_kpminus},
	{AKEYCODE_NUMPAD_ADD, swa_key_kpplus},
	{AKEYCODE_NUMPAD_DOT, swa_key_kpperiod},
	{AKEYCODE_NUMPAD_COMMA, swa_key_kpcomma},
	{AKEYCODE_NUMPAD_ENTER, swa_key_kpenter},
	{AKEYCODE_NUMPAD_EQUALS, swa_key_kpequals},
};

static struct {
	unsigned int android;
	enum swa_keyboard_mod modifier;
} mod_map[] = {
	{AMETA_ALT_ON, swa_keyboard_mod_alt},
	{AMETA_SHIFT_ON, swa_keyboard_mod_shift},
	{AMETA_CTRL_ON, swa_keyboard_mod_ctrl},
	{AMETA_META_ON, swa_keyboard_mod_super},
	{AMETA_CAPS_LOCK_ON, swa_keyboard_mod_caps_lock},
	{AMETA_NUM_LOCK_ON, swa_keyboard_mod_num_lock}
};

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;

enum swa_key android_to_key(int32_t keycode) {
	for(unsigned int i = 0u; i < sizeof(keycode_map) / sizeof(keycode_map[0]); ++i) {
		if(keycode == keycode_map[i].android) {
			return keycode_map[i].keycode;
		}
	}

	return swa_key_none;
}

int32_t key_to_android(enum swa_key key) {
	for(unsigned int i = 0u; i < sizeof(keycode_map) / sizeof(keycode_map[0]); ++i) {
		if(key == keycode_map[i].keycode) {
			return keycode_map[i].android;
		}
	}

	return -1;
}

enum swa_keyboard_mod android_to_modifiers(int32_t meta_state) {
	enum swa_keyboard_mod mods = swa_keyboard_mod_none;
	for(unsigned i = 0u; i < sizeof(mod_map) / sizeof(mod_map[0]); ++i) {
		if(meta_state & mod_map[i].android) {
			mods |= mod_map[i].modifier;
		}
	}

	return mods;
}

static struct swa_display_android* get_display_android(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_android*) base;
}

static struct swa_window_android* get_window_android(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_android*) base;
}

static void output_handler(const struct dlg_origin* origin, const char* str,
		void* data) {
	enum dlg_output_feature features = dlg_output_file_line;

	size_t size;
	dlg_generic_output_buf(NULL, &size, features, origin,
		str, dlg_default_output_styles);
	++size;
	char* buf = malloc(size);
	dlg_generic_output_buf(buf, &size, features, origin, str,
		dlg_default_output_styles);

	int prio = ANDROID_LOG_DEFAULT;
	switch(origin->level) {
		case dlg_level_trace: // fallthrough
		case dlg_level_debug:
			prio = ANDROID_LOG_DEBUG;
			break;
		case dlg_level_info:
			prio = ANDROID_LOG_INFO;
			break;
		case dlg_level_warn:
			prio = ANDROID_LOG_WARN;
			break;
		case dlg_level_error:
			prio = ANDROID_LOG_ERROR;
			break;
		case dlg_level_fatal:
			prio = ANDROID_LOG_FATAL;
			break;
		default:
			break;
	}

	struct activity* activity = data;
	const char* tag = activity->dpy ? activity->dpy->appname : "swa";
	__android_log_write(prio, tag, buf);
	free(buf);
}

// window api
static bool create_surface(struct swa_window_android* win,
		ANativeWindow* nwin, const struct swa_window_settings* settings) {
	dlg_trace("create_surface");
	struct swa_display_android* dpy = win->dpy;
	(void) dpy;

	if(win->surface_type == swa_surface_buffer) {
		// TODO: handle more data formats. Respect the surface
		// settings and the transparent flag
		int err = ANativeWindow_setBuffersGeometry(nwin,
			0, 0, WINDOW_FORMAT_RGBA_8888);
		if(err != 0) {
			dlg_error("ANativeWindow_setBuffersGeometry: %d", err);
			return false;
		}
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		if(!dpy->egl) {
			dpy->egl = swa_egl_display_create(EGL_PLATFORM_ANDROID_KHR,
				EGL_DEFAULT_DISPLAY);
			if(!dpy->egl) {
				return false;
			}
		}

		const struct swa_gl_surface_settings* gls = &settings->surface_settings.gl;
		bool alpha = settings->transparent;
		EGLConfig config;
		EGLContext* ctx = &win->gl.context;
		if(!swa_egl_init_context(dpy->egl, gls, alpha, &config, ctx)) {
			return false;
		}

		if(!(win->gl.surface = swa_egl_create_surface(dpy->egl,
				nwin, config, gls->srgb))) {
			return false;
		}
#else // SWA_WITH_GL
		dlg_error("swa was compiled without GL support");
		return false;
#endif // SWA_WITH_GL
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		win->vk.instance = settings->surface_settings.vk.instance;
		if(!win->vk.instance) {
			dlg_error("No vulkan instance passed for vulkan window");
			return false;
		}

		VkAndroidSurfaceCreateInfoKHR info = {0};
		info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
		info.window = nwin;

		VkInstance instance = (VkInstance) win->vk.instance;
		VkSurfaceKHR surface;

		PFN_vkCreateAndroidSurfaceKHR fn = (PFN_vkCreateAndroidSurfaceKHR)
			vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
		if(!fn) {
			dlg_error("Failed to load 'vkCreateWaylandSurfaceKHR' function");
			return false;
		}

		VkResult res = fn(instance, &info, NULL, &surface);
		if(res != VK_SUCCESS) {
			dlg_error("Failed to create vulkan surface: %d", res);
			return false;
		}

		win->vk.surface = (uint64_t)surface;
#else // SWA_WITH_VK
		dlg_error("swa was compiled without vulkan support");
		return false;
#endif // SWA_WITH_VK
	}

	win->valid = true;
	return true;
}

static void destroy_surface(struct swa_window_android* win) {
	dlg_trace("destroy_surface");
	win->valid = false;

	if(win->surface_type == swa_surface_buffer) {
		dlg_assertm(!win->buffer.active, "Drawing buffer still pending "
			"while surface was destroyed");
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		if(win->vk.surface) {
			dlg_assert(win->vk.instance);

			VkInstance instance = (VkInstance) win->vk.instance;
			VkSurfaceKHR surface = (VkSurfaceKHR) win->vk.surface;
			PFN_vkDestroySurfaceKHR fn = (PFN_vkDestroySurfaceKHR)
				vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
			if(fn) {
				fn(instance, surface, NULL);
			} else {
				dlg_error("Failed to load 'vkDestroySurfaceKHR' function");
			}
		}

		memset(&win->vk, 0x0, sizeof(win->vk));
#else // SWA_WITH_VK
		dlg_error("swa was compiled without vk support; invalid surface");
#endif // SWA_WITH_VK
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		if(win->gl.context) {
			eglDestroyContext(win->dpy->egl->display, win->gl.context);
		}
		if(win->gl.surface) {
			eglDestroySurface(win->dpy->egl->display, win->gl.surface);
		}

		memset(&win->gl, 0x0, sizeof(win->gl));
#else // SWA_WITH_GL
		dlg_error("swa was compiled without gl support; invalid surface");
#endif // SWA_WITH_GL
	}
}

static void win_destroy(struct swa_window* base) {
	struct swa_window_android* win = get_window_android(base);
	destroy_surface(win);
	if(win->dpy) {
		win->dpy->window = NULL;
	}
	free(win);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
	(void) base;
	return swa_window_cap_none;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	(void) base; (void) w; (void) h;
	dlg_error("win_set_min_size not supported");
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	(void) base; (void) w; (void) h;
	dlg_error("win_set_max_size not supported");
}

static void win_show(struct swa_window* base, bool show) {
	(void) base; (void) show;
	dlg_error("win_show not supported");
}

static void win_set_size(struct swa_window* base, unsigned w, unsigned h) {
	(void) base; (void) w; (void) h;
	dlg_error("win_set_size not supported");
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	(void) base; (void) cursor;
	dlg_error("win_set_cursor not supported");
}

static void frame_callback64(int64_t frame_time_nanos, void* data) {
	(void) frame_time_nanos;
	struct swa_window_android* win = data;
	if(win->redraw) {
		win->redraw = false;
		if(win->valid && win->base.listener->draw) {
			win->base.listener->draw(&win->base);
		}
	}
}

static void frame_callback(long frame_time_nanos, void* data) {
	frame_callback64(frame_time_nanos, data);
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_android* win = get_window_android(base);
	win->redraw = true;
}

static void win_surface_frame(struct swa_window* base) {
	struct swa_window_android* win = get_window_android(base);
	if(!win->valid) {
		dlg_warn("invalid (destroyed) native window");
		return;
	}

	struct swa_display_android* dpy = win->dpy;

	if(dpy->choreographer) {
#if __ANDROID_API__ >= 29
		AChoreographer_postFrameCallback64(dpy->choreographer,
			frame_callback64, win);
#elif __ANDROID_API__ >= 24
		AChoreographer_postFrameCallback(dpy->choreographer,
			frame_callback, win);
#else
		// When the choreographer is available, the api has to be
		// at least 24, otherwise we can't have gotten one
		dlg_error("Unreachable");
#endif // __ANDROID_API__
	} else {
		// TODO: somehow handle case when choreographer isn't available.
		// Do refreshing via timer or something
		dlg_warn("Unimplemented: no choreographer available. "
			"Refreshing the surface probably won't work");
	}
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	(void) base; (void) state;
	dlg_error("win_set_state not supported");
}

static void win_begin_move(struct swa_window* base) {
	(void) base;
	dlg_error("win_begin_move not supported");
}

static void win_begin_resize(struct swa_window* base, enum swa_edge edges) {
	(void) base; (void) edges;
	dlg_error("win_begin_resize not supported");
}

static void win_set_title(struct swa_window* base, const char* title) {
	(void) base; (void) title;
	dlg_error("win_set_title not supported");
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
	(void) base; (void) img;
	dlg_error("win_set_icon not supported");
}

static bool win_is_client_decorated(struct swa_window* base) {
	(void) base;
	// it's neither client nor server decoration i guess
	return false;
}

static uint64_t win_get_vk_surface(struct swa_window* base) {
#ifdef SWA_WITH_VK
	struct swa_window_android* win = get_window_android(base);
	if(win->surface_type != swa_surface_vk) {
		dlg_warn("can't get vulkan surface from non-vulkan window");
		return 0;
	}

	if(!win->valid) {
		dlg_warn("Native window is invalid (destroyed");
		return 0;
	}

	return win->vk.surface;
#else
	dlg_warn("swa was compiled without vulkan suport");
	return 0;
#endif
}

static bool win_gl_make_current(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_android* win = get_window_android(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	if(!win->valid) {
		dlg_warn("Native window is invalid (destroyed)");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);
	return eglMakeCurrent(win->dpy->egl->display, win->gl.surface,
		win->gl.surface, win->gl.context);
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_swap_buffers(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_android* win = get_window_android(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	if(!win->valid) {
		dlg_warn("Native window is invalid (destroyed)");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);

	// eglSwapBuffers must commit to the surface in one way or another
	win_surface_frame(&win->base);

	if(!eglSwapBuffers(win->dpy->egl->display, win->gl.surface)) {
		dlg_error("eglSwapBuffers: %s", swa_egl_last_error_msg());
		return false;
	}

	return true;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
#ifdef SWA_WITH_GL
	// struct swa_window_wl* win = get_window_wl(base);
	dlg_error("Unimplemented");
	return false;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

// TODO: support more image formats

// android image formats use byte order, just like swa does.
// their rgba32 format therefore is our rgba32 format.
static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	struct swa_window_android* win = get_window_android(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return false;
	}

	if(win->buffer.active) {
		dlg_error("There is already an active buffer");
		return false;
	}

	if(!win->valid) {
		dlg_warn("Native window is invalid (destroyed)");
		return false;
	}

	ANativeWindow* awin = win->dpy->activity->window;
	dlg_assert(awin);

	ARect dirty = {0};
	ANativeWindow_Buffer buffer;
	int res = ANativeWindow_lock(awin, &buffer, &dirty);
	dlg_assertm(!res, "ANativeWindow_lock: %d", res);

	img->data = buffer.bits;
	img->width = buffer.width;
	img->height = buffer.height;
	img->stride = 4 * buffer.stride;
	img->format = swa_image_format_rgba32;
	win->buffer.active = true;
	return true;
}

static void win_apply_buffer(struct swa_window* base) {
	struct swa_window_android* win = get_window_android(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return;
	}

	if(!win->buffer.active) {
		dlg_error("There is already an active buffer");
		return;
	}

	// this shouldn't change in between acquiring a buffer and applying it.
	// we check here anyways
	if(!win->valid) {
		dlg_warn("Native window is invalid (destroyed)");
		return;
	}

	ANativeWindow* awin = win->dpy->activity->window;
	dlg_assert(awin);

	win_surface_frame(base);
	int res = ANativeWindow_unlockAndPost(awin);
	dlg_assertm(!res, "ANativeWindow_unlockAndPost: %d", res);
	win->buffer.active = false;
}

static const struct swa_window_interface window_impl = {
	.destroy = win_destroy,
	.get_capabilities = win_get_capabilities,
	.set_min_size = win_set_min_size,
	.set_max_size = win_set_max_size,
	.show = win_show,
	.set_size = win_set_size,
	.refresh = win_refresh,
	.surface_frame = win_surface_frame,
	.set_state = win_set_state,
	.set_cursor = win_set_cursor,
	.begin_move = win_begin_move,
	.begin_resize = win_begin_resize,
	.set_title = win_set_title,
	.set_icon = win_set_icon,
	.is_client_decorated = win_is_client_decorated,
	.get_vk_surface = win_get_vk_surface,
	.gl_make_current = win_gl_make_current,
	.gl_swap_buffers = win_gl_swap_buffers,
	.gl_set_swap_interval = win_gl_set_swap_interval,
	.get_buffer = win_get_buffer,
	.apply_buffer = win_apply_buffer
};

// display api
static void display_destroy(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	dlg_assertm(!dpy->window, "Window wasn't destroyed before display");

#ifdef SWA_WITH_GL
	if(dpy->egl) {
		swa_egl_display_destroy(dpy->egl);
	}
#endif

	free(dpy->events);
	free((char*) dpy->appname);
	dpy->activity->dpy = NULL;
	free(dpy);
}

static bool is_destroyed(struct activity* activity) {
	pthread_mutex_lock(&activity->mutex);
	bool res = activity->destroyed;
	pthread_mutex_unlock(&activity->mutex);
	return res;
}

// https://encoding.spec.whatwg.org/#utf-8-encoder
static void encode_utf8(char* out, uint32_t unicode) {
	if(unicode <= 0x7F) { // 1-byte (ascii)
		out[0] = (char) unicode;
		out[1] = '\0';
	} else if(unicode <= 0x07FF) { // 2-byte
		out[0] = (char) (((unicode >> 6) & 0x1F) | 0xC0);
		out[1] = (char) (((unicode >> 0) & 0x3F) | 0x80);
		out[2] = '\0';
	} else if(unicode <= 0xFFFF) { // 3-byte
		out[0] = (char) (((unicode >> 12) & 0x0F) | 0xE0);
		out[1] = (char) (((unicode >>  6) & 0x3F) | 0x80);
		out[2] = (char) (((unicode >>  0) & 0x3F) | 0x80);
		out[3] = '\0';
	} else if(unicode <= 0x10FFFF) { // 4-byte
		out[0] = (char) (((unicode >> 18) & 0x07) | 0xF0);
		out[1] = (char) (((unicode >> 12) & 0x3F) | 0x80);
		out[2] = (char) (((unicode >>  6) & 0x3F) | 0x80);
		out[3] = (char) (((unicode >>  0) & 0x3F) | 0x80);
		out[4] = '\0';
	} else {
		dlg_error("Invalid unicode char: %d", unicode);
		out[0] = '\0';
	}
}

// TODO(optimization): don't attach thread and query function *every time*
// do it just once.
static void get_utf8(struct activity* activity, char* out,
		unsigned akeycode, unsigned ameta) {
    JavaVMAttachArgs attach_args;
    attach_args.version = JNI_VERSION_1_6;
    attach_args.name = "NativeThread";
    attach_args.group = NULL;

	JNIEnv* jni_env;
	JavaVM* vm = activity->activity->vm;
    jint res = (*vm)->AttachCurrentThread(vm, &jni_env, &attach_args);
    if(res == JNI_ERR) {
		dlg_error("AttachCurrentThread failed");
		out[0] = '\0';
		return;
    }

	jclass jni_key_event = (*jni_env)->FindClass(jni_env, "android/view/KeyEvent");

	jmethodID jni_key_event_ctor;
	jmethodID jni_get_unicode_char;
	if(ameta) {
		jni_get_unicode_char = (*jni_env)->GetMethodID(jni_env,
			jni_key_event, "getUnicodeChar", "(I)I");
	} else {
 		jni_get_unicode_char = (*jni_env)->GetMethodID(jni_env,
			jni_key_event, "getUnicodeChar", "()I");
	}
	jni_key_event_ctor = (*jni_env)->GetMethodID(jni_env,
		jni_key_event, "<init>", "(II)V");

	if(!jni_key_event || !jni_get_unicode_char || !jni_key_event_ctor) {
    	(*vm)->DetachCurrentThread(vm);
		dlg_warn("could not load all jni symbols");
		out[0] = '\0';
		return;
	}

	jobject eventObj = (*jni_env)->NewObject(jni_env, jni_key_event,
		jni_key_event_ctor, AKEY_EVENT_ACTION_DOWN, akeycode);
	uint32_t unicode = (*jni_env)->CallIntMethod(jni_env, eventObj,
		jni_get_unicode_char, ameta);
	(*vm)->DetachCurrentThread(vm);
	encode_utf8(out, unicode);
}

static bool handle_key_event(struct swa_display_android* dpy, AInputEvent* ev) {
	dlg_assert(AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY);
	dlg_trace("key event");

	int action = AKeyEvent_getAction(ev);
	bool pressed = false;
	if(action == AKEY_EVENT_ACTION_DOWN) {
		pressed = true;
	} else if(action == AKEY_EVENT_ACTION_MULTIPLE) {
		// There is no way to retrieve any meaningful information in
		// this case. Sadly, this will be triggered for all non-standard
		// characters, i.e. non-english ones.
		// See https://code.google.com/p/android/issues/detail?id=33998
		dlg_warn("Can't process ACTION_MULTIPLE key event; ndk issue");
		return false;
	} else if(action != AKEY_EVENT_ACTION_UP) {
		dlg_debug("Unknown key action %d", action);
		return false;
	}

	int32_t akeycode = AKeyEvent_getKeyCode(ev);

	// we skip this keycode so that is handled by android
	// the application can't handle it anyways
	if(akeycode == AKEYCODE_BACK ||
			akeycode == AKEYCODE_VOLUME_UP ||
			akeycode == AKEYCODE_VOLUME_DOWN) {
		dlg_debug("android: skip back key");
		return false;
	}

	int32_t ameta = AKeyEvent_getMetaState(ev);
	enum swa_keyboard_mod mods = android_to_modifiers(ameta);
	dpy->keyboard_mods = mods;

	// set the key state
	enum swa_key key = android_to_key(akeycode);
	unsigned idx = key / 64;
	unsigned bit = key % 64;
	if(pressed) {
		dpy->key_states[idx] |= (uint64_t)(1 << bit);
	} else {
		dpy->key_states[idx] &= ~(uint64_t)(1 << bit);
	}

	if(!dpy->window || !dpy->window->base.listener->key) {
		return false;
	}

	char utf8[5];
	get_utf8(dpy->activity, utf8, akeycode, ameta);
	struct swa_key_event kev = {
		.utf8 = utf8,
		.keycode = key,
		.modifiers = mods,
		.pressed = pressed,
		.repeated = AKeyEvent_getRepeatCount(ev) != 0,
	};

	dpy->window->base.listener->key(&dpy->window->base, &kev);
	return true;
}

static bool handle_motion_event(struct swa_display_android* dpy, AInputEvent* ev) {
	dlg_assert(AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION);
	int32_t action = AMotionEvent_getAction(ev);

	size_t count = AMotionEvent_getPointerCount(ev);
	switch(action & AMOTION_EVENT_ACTION_MASK) {
	case AMOTION_EVENT_ACTION_DOWN: {
		if(!dpy->window || !dpy->window->base.listener->touch_begin) {
			return false;
		}

		for(size_t i = 0u; i < count; ++i) {
			struct swa_touch_event tev = {
				.id = AMotionEvent_getPointerId(ev, i),
				.x = AMotionEvent_getX(ev, i),
				.y = AMotionEvent_getY(ev, i),
			};
			dpy->window->base.listener->touch_begin(&dpy->window->base, &tev);
		}
		break;
	} case AMOTION_EVENT_ACTION_UP: {
		if(!dpy->window || !dpy->window->base.listener->touch_end) {
			return false;
		}

		for(size_t i = 0u; i < count; ++i) {
			unsigned id = AMotionEvent_getPointerId(ev, i);
			dpy->window->base.listener->touch_end(&dpy->window->base, id);
		}
		break;
	} case AMOTION_EVENT_ACTION_MOVE: {
		if(!dpy->window || !dpy->window->base.listener->touch_update) {
			return false;
		}

		for(size_t i = 0u; i < count; ++i) {
			struct swa_touch_event tev = {
				.id = AMotionEvent_getPointerId(ev, i),
				.x = AMotionEvent_getX(ev, i),
				.y = AMotionEvent_getY(ev, i),
			};

			dpy->window->base.listener->touch_update(&dpy->window->base, &tev);
		}
		break;
	} case AMOTION_EVENT_ACTION_CANCEL: {
		if(!dpy->window || !dpy->window->base.listener->touch_cancel) {
			return false;
		}
		dpy->window->base.listener->touch_cancel(&dpy->window->base);
		break;

	// Those events are generated for secondary touch points up/down
	} case AMOTION_EVENT_ACTION_POINTER_DOWN: {
		if(!dpy->window || !dpy->window->base.listener->touch_begin) {
			return false;
		}

		int32_t idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
			>> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		unsigned id = AMotionEvent_getPointerId(ev, idx);
		struct swa_touch_event tev = {
			.id = id,
			.x = AMotionEvent_getX(ev, idx),
			.y = AMotionEvent_getY(ev, idx),
		};

		dpy->window->base.listener->touch_begin(&dpy->window->base, &tev);
		break;
	} case AMOTION_EVENT_ACTION_POINTER_UP: {
		if(!dpy->window || !dpy->window->base.listener->touch_end) {
			return false;
		}

		int32_t idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
			>> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		unsigned id = AMotionEvent_getPointerId(ev, idx);
		dpy->window->base.listener->touch_end(&dpy->window->base, id);
		break;
	/*
	} case AMOTION_EVENT_ACTION_HOVER_MOVE: {
		MouseMoveEvent mme;
		mme.position = pos;
		mme.eventData = &eventData;
		wc.listener().mouseMove(mme);
		break;
	} case AMOTION_EVENT_ACTION_SCROLL: {
		MouseWheelEvent mwe;
		mwe.eventData = &eventData;
		mwe.value[0] = AMotionEvent_getAxisValue(&event,
			AMOTION_EVENT_AXIS_VSCROLL, 0);
		mwe.value[1] = AMotionEvent_getAxisValue(&event,
			AMOTION_EVENT_AXIS_HSCROLL, 0);
		wc.listener().mouseWheel(mwe);
		break;
	} case AMOTION_EVENT_ACTION_HOVER_ENTER: {
		MouseCrossEvent mce;
		mce.entered = true;
		wc.listener().mouseCross(mce);
		break;
	} case AMOTION_EVENT_ACTION_HOVER_EXIT: {
		MouseCrossEvent mce;
		mce.entered = true;
		wc.listener().mouseCross(mce);
		break;
	*/
	// TODO: AMOTION_EVENT_ACTION_BUTTON_PRESS etc
	} default:
		return false;
	}

	return true;
}

// Returns whether the event was handled or not
static bool handle_input_event(struct swa_display_android* dpy, AInputEvent* ev) {
	int32_t type = AInputEvent_getType(ev);
	if(type == AINPUT_EVENT_TYPE_KEY) {
		return handle_key_event(dpy, ev);
	} else if(type == AINPUT_EVENT_TYPE_MOTION) {
		return handle_motion_event(dpy, ev);
	} else {
		dlg_debug("Unknown event type %d", type);
	}

	return false;
}

static int input_queue_readable(int fd, int events, void* data) {
	(void) fd;
	(void) events;

	struct swa_display_android* dpy = data;
	AInputQueue* q = dpy->activity->input_queue;
	dlg_assert(q);

	int ret = 0;
	while((ret = AInputQueue_hasEvents(q)) > 0) {
		AInputEvent* event = NULL;
		ret = AInputQueue_getEvent(q, &event);
		if(ret < 0) {
			dlg_warn("getEvent returned error code %d", ret);
			continue;
		}

		ret = AInputQueue_preDispatchEvent(q, event);
		if(ret != 0) {
			continue;
		}

		bool handled = handle_input_event(dpy, event);
		AInputQueue_finishEvent(q, event, handled);
	}

	if(ret < 0) {
		dlg_warn("input queue returned error code %d", ret);
		return 1;
	}

	return 1;
}

static void init_input_queue(struct swa_display_android* dpy,
		AInputQueue* queue) {
	dlg_assertm(queue == dpy->activity->input_queue, "%p vs %p",
		(void*) queue, (void*) dpy->activity->input_queue);
	AInputQueue_attachLooper(queue, dpy->looper, 1u,
		input_queue_readable, dpy);
}

static void handle_event(struct swa_display_android* dpy, const struct event* ev) {
	switch(ev->type) {
		case event_type_draw:
			if(dpy->window && dpy->window->base.listener->draw) {
				dpy->window->base.listener->draw(&dpy->window->base);
			}
			break;
		case event_type_focus:
			memset(dpy->key_states, 0, sizeof(dpy->key_states));
			if(dpy->window && dpy->window->base.listener->focus) {
				struct swa_window* win = &dpy->window->base;
				win->listener->focus(win, ev->data);
			}
			break;
		case event_type_win_created:
			if(dpy->window && !dpy->window->valid) {
				if(!create_surface(dpy->window, dpy->activity->window,
						&dpy->window->settings)) {
					break;
				}

				if(dpy->window->base.listener->surface_created) {
					dpy->window->base.listener->surface_created(&dpy->window->base);
				}
			}
			break;
		case event_type_win_destroyed:
			memset(dpy->key_states, 0, sizeof(dpy->key_states));
			if(dpy->window && dpy->window->valid) {
				destroy_surface(dpy->window);
				if(dpy->window->base.listener->surface_destroyed) {
					dpy->window->base.listener->surface_destroyed(&dpy->window->base);
				}
			}
			break;
		case event_type_resize:
			if(dpy->window && dpy->window->base.listener->resize) {
				uint32_t w = ev->data >> 32;
				uint32_t h = ev->data & 0xFFFFFFFFu;
				dpy->window->base.listener->resize(&dpy->window->base, w, h);
			}
			break;
		case event_type_input_queue_created: {
			AInputQueue* queue = (AInputQueue*)(uintptr_t) ev->data;
			init_input_queue(dpy, queue);
			break;
		} case event_type_input_queue_destroyed: {
			AInputQueue* queue = (AInputQueue*)(uintptr_t) ev->data;
			AInputQueue_detachLooper(queue);
			break;
		} default:
			dlg_error("Invalid event type: %d", ev->type);
			break;
	}
}

static bool display_dispatch(struct swa_display* base, bool block) {
	struct swa_display_android* dpy = get_display_android(base);

	// process events from activity thread
	pthread_mutex_lock(&dpy->activity->mutex);
	if(dpy->activity->destroyed) {
		pthread_mutex_unlock(&dpy->activity->mutex);
		return false;
	}

	// process pending window events
	pthread_mutex_unlock(&dpy->activity->mutex);
	if(dpy->window && dpy->activity->window && dpy->window->initial_events) {
		dpy->window->initial_events = false;
		if(dpy->window->base.listener->resize) {
			unsigned width = ANativeWindow_getWidth(dpy->activity->window);
			unsigned height = ANativeWindow_getHeight(dpy->activity->window);
			dpy->window->base.listener->resize(&dpy->window->base, width, height);
		}
	}

	pthread_mutex_lock(&dpy->activity->mutex);
	while(dpy->n_events) {
		struct event ev = dpy->events[0];
		--dpy->n_events;
		memmove(dpy->events, dpy->events + 1, sizeof(*dpy->events) * dpy->n_events);
		pthread_mutex_unlock(&dpy->activity->mutex);
		handle_event(dpy, &ev);

		pthread_mutex_lock(&dpy->activity->mutex);

		if(ev.cond_wakeup) {
			*ev.cond_wakeup = true;
			pthread_cond_signal(&dpy->activity->cond);
		}
	}

	pthread_mutex_unlock(&dpy->activity->mutex);

	// process looper events
	int outFd, outEvents;
	void* outData;
	int timeout = block ? -1 : 0;
	int err = ALooper_pollAll(timeout, &outFd, &outEvents, &outData);
	if(err == ALOOPER_POLL_ERROR) {
		dlg_error("ALooper_pollAll error");
	}

	return !is_destroyed(dpy->activity);

}

static void display_wakeup(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	ALooper_wake(dpy->looper);
}

static enum swa_display_cap display_capabilities(struct swa_display* base) {
	(void) base;
	enum swa_display_cap caps =
		// NOTE: reasonable assumptions but may be wrong.
		// android devices work without touch and could e.g. have a
		// mouse connected instead. No way to query this though i guess
		swa_display_cap_touch |
		swa_display_cap_keyboard |
#ifdef SWA_WITH_GL
		swa_display_cap_gl |
#endif
#ifdef SWA_WITH_VK
		swa_display_cap_vk |
#endif
		swa_display_cap_buffer_surface;

	return caps;
}

static const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
	(void) base;
#ifdef SWA_WITH_VK
	static const char* names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
	};
	*count = sizeof(names) / sizeof(names[0]);
	return names;
#else
	dlg_warn("swa was compiled without vulkan suport");
	*count = 0;
	return NULL;
#endif
}

static swa_gl_proc display_get_gl_proc_addr(struct swa_display* base,
		const char* name) {
#ifdef SWA_WITH_GL
	return (swa_gl_proc) eglGetProcAddress(name);
#else
	dlg_error("swa was built without gl support");
	return NULL;
#endif
}

static bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	struct swa_display_android* dpy = get_display_android(base);
	const unsigned n_bits = 8 * sizeof(dpy->key_states);
	if(key >= n_bits) {
		dlg_warn("keycode not tracked (too high)");
		return false;
	}

	unsigned idx = key / 64;
	unsigned bit = key % 64;
	return (dpy->key_states[idx] & (1 << bit));
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_android* dpy = get_display_android(base);
	int32_t keycode = key_to_android(key);
	if(keycode == -1) {
		dlg_warn("Key %d has no android equivalent", key);
		return "";
	}

	char* name = malloc(5);
	get_utf8(dpy->activity, name, keycode, 0);
	return name;
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	return dpy->keyboard_mods;
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	return dpy->window && dpy->window->focus ? &dpy->window->base : NULL;
}

// TODO: mouse support
static bool display_mouse_button_pressed(struct swa_display* base,
		enum swa_mouse_button button) {
	// struct swa_display_android* dpy = get_display_android(base);
	return false;
}
static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	// struct swa_display_android* dpy = get_display_android(base);
	*x = *y = 0;
}
static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	return dpy->window && dpy->window->focus ? &dpy->window->base : NULL;
}
static struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	dlg_error("clipboard not supported");
	return NULL;
}
static bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source) {
	dlg_error("clipboard not supported");
	return false;
}
static bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	dlg_error("dnd not supported");
	return false;
}

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_android* dpy = get_display_android(base);
	struct swa_window_android* win = calloc(1, sizeof(*win));

	win->base.impl = &window_impl;
	win->base.listener = settings->listener;

	win->dpy = dpy;
	win->surface_type = settings->surface;
	win->initial_events = true;
	win->settings = *settings;

	if(dpy->activity->window) {
		if(!create_surface(win, dpy->activity->window, settings)) {
			goto error;
		}
	} else {
		// TODO: send (deferred) initial surface destroyed event
	}

	dpy->window = win;
	return &win->base;

error:
	win_destroy(&win->base);
	return NULL;
}

static const struct swa_display_interface display_impl = {
	.destroy = display_destroy,
	.dispatch = display_dispatch,
	.wakeup = display_wakeup,
	.capabilities = display_capabilities,
	.vk_extensions = display_vk_extensions,
	.key_pressed = display_key_pressed,
	.key_name = display_key_name,
	.active_keyboard_mods = display_active_keyboard_mods,
	.get_keyboard_focus = display_get_keyboard_focus,
	.mouse_button_pressed = display_mouse_button_pressed,
	.mouse_position = display_mouse_position,
	.get_mouse_over = display_get_mouse_over,
	.get_clipboard = display_get_clipboard,
	.set_clipboard = display_set_clipboard,
	.start_dnd = display_start_dnd,
	.get_gl_proc_addr = display_get_gl_proc_addr,
	.create_window = display_create_window,
};

// android-specific
bool swa_display_is_android(struct swa_display* dpy) {
	return dpy->impl == &display_impl;
}

ANativeWindow* swa_display_android_get_native_window(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	return dpy->activity->window;
}

ANativeActivity* swa_display_android_get_native_activity(struct swa_display* base) {
	struct swa_display_android* dpy = get_display_android(base);
	return dpy->activity->activity;
}

struct swa_display* swa_display_android_create(const char* appname) {
	(void) appname;
	dlg_assert(g_current_activity);

	struct swa_display_android* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;
	dpy->looper = ALooper_prepare(0);
	dpy->activity = g_current_activity;
	dpy->appname = strdup(appname ? appname : "swa");

#if __ANDROID_API__ >= 24
	dpy->choreographer = AChoreographer_getInstance();
#endif // __ANDROID_API__ >= 24

	dpy->activity->dpy = dpy;
	if(dpy->activity->input_queue) {
		init_input_queue(dpy, dpy->activity->input_queue);
	}

	return &dpy->base;
}

// activity api
static void* main_thread(void* data) {
	struct activity* activity = data;
	dlg_assert(activity);

	int ret = main(0, NULL);
	dlg_info("main thread has exited with return code %d", ret);

	ANativeActivity_finish(activity->activity);
	return NULL;
}

static void push_event_locked(struct activity* activity, struct event ev) {
	struct swa_display_android* dpy = activity->dpy;
	if(!dpy) {
		return;
	}

	if(++dpy->n_events > dpy->cap_events) {
		dpy->cap_events = 2 * dpy->n_events;
		unsigned size = sizeof(*dpy->events) * dpy->cap_events;
		dpy->events = realloc(dpy->events, size);
	}

	dpy->events[dpy->n_events - 1] = ev;
	ALooper_wake(activity->dpy->looper);
}

static inline void push_event(struct activity* activity, struct event ev) {
	pthread_mutex_lock(&activity->mutex);
	push_event_locked(activity, ev);
	pthread_mutex_unlock(&activity->mutex);
}

static void push_event_wait_locked(struct activity* activity, struct event ev) {
	struct swa_display_android* dpy = activity->dpy;
	if(!dpy) {
		return;
	}

	if(++dpy->n_events > dpy->cap_events) {
		dpy->cap_events = 2 * dpy->n_events;
		unsigned size = sizeof(*dpy->events) * dpy->cap_events;
		dpy->events = realloc(dpy->events, size);
	}

	bool done = false;
	ev.cond_wakeup = &done;
	dpy->events[dpy->n_events - 1] = ev;

	ALooper_wake(activity->dpy->looper);
	while(!done) {
		pthread_cond_wait(&activity->cond, &activity->mutex);
	}
}

// static void push_event_wait(struct activity* activity, struct event ev) {
// 	pthread_mutex_lock(&activity->mutex);
// 	push_event_wait_locked(activity, ev);
// 	pthread_mutex_unlock(&activity->mutex);
// }

static void activity_onStart(ANativeActivity* activity) {
	(void) activity;
	// no-op
}

static void activity_onResume(ANativeActivity* activity) {
	(void) activity;
	// no-op
}

static void* activity_onSaveInstanceState(ANativeActivity* activity, size_t* outSize) {
	(void) activity;
	*outSize = 0;
	return NULL;
}

static void activity_onPause(ANativeActivity* activity) {
	(void) activity;
	// no-op
}

static void activity_onStop(ANativeActivity* activity) {
	(void) activity;
	// no-op
}

static void activity_onDestroy(ANativeActivity* aactivity) {
	dlg_trace("onDestroy");
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	pthread_mutex_lock(&activity->mutex);
	activity->destroyed = true;
	pthread_mutex_unlock(&activity->mutex);

	if(activity->dpy) {
		ALooper_wake(activity->dpy->looper);
	}

	if(activity->thread_started) {
		dlg_trace("waiting for main thread do finish...");
		pthread_join(activity->main_thread, NULL);
		dlg_trace("done waiting for main thread");
	}

	dlg_assertm(activity->dpy == NULL, "Display wasn't destroyed");

	free(activity);
	aactivity->instance = NULL;
	if(g_current_activity == activity) {
		g_current_activity = NULL;
		dlg_set_handler(dlg_default_output, stdout);
	}

	dlg_trace("onDestroy done");
}

static void activity_onWindowFocusChanged(ANativeActivity* aactivity,
		int has_focus) {
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	struct event ev = {
		.type = event_type_focus,
		.data = has_focus,
	};

	push_event(activity, ev);
}

static void activity_onNativeWindowCreated(ANativeActivity* aactivity,
		ANativeWindow* window) {

	dlg_trace("onNativeWindowCreated");
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	struct event ev = {
		.type = event_type_win_created,
		.data = (uint64_t)(uintptr_t) window,
	};

	pthread_mutex_lock(&activity->mutex);
	activity->window = window;
	if(activity->dpy) {
		push_event_locked(activity, ev);
	} else if(!activity->thread_started) {
		// initialization complete, start the main thread
		int err = pthread_create(&activity->main_thread, NULL,
			main_thread, activity);
		dlg_assertm(!err, "pthread_create: %s", strerror(errno));
		activity->thread_started = true;
	}

	pthread_mutex_unlock(&activity->mutex);
	dlg_trace("onNativeWindowCreated done");
}

static void activity_onNativeWindowResized(ANativeActivity* aactivity,
		ANativeWindow* window) {
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	int64_t width = ANativeWindow_getWidth(window);
	int32_t height = ANativeWindow_getHeight(window);
	struct event ev = {
		.type = event_type_resize,
		.data = (width << 32 | height),
	};

	push_event(activity, ev);
}

static void activity_onNativeWindowRedrawNeeded(ANativeActivity* aactivity,
		ANativeWindow* window) {
	dlg_trace("onNativeWindowRedraw");
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	int64_t width = ANativeWindow_getWidth(window);
	int32_t height = ANativeWindow_getHeight(window);
	struct event ev = {
		.type = event_type_draw,
		.data = (width << 32 | height),
	};

	push_event(activity, ev);
	dlg_trace("onNativeWindowRedraw done");
}

static void activity_onNativeWindowDestroyed(ANativeActivity* aactivity,
		ANativeWindow* window) {
	dlg_trace("onNativeWindowDestroyed");
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	struct event ev = {
		.type = event_type_win_destroyed,
	};

	pthread_mutex_lock(&activity->mutex);
	push_event_wait_locked(activity, ev);
	activity->window = NULL;
	pthread_mutex_unlock(&activity->mutex);
	dlg_trace("onNativeWindowDestroyed done");
}

static void activity_onInputQueueCreated(ANativeActivity* aactivity,
		AInputQueue* queue) {
	dlg_trace("onInputQueueCreated %p", (void*) queue);
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	struct event ev = {
		.type = event_type_input_queue_created,
		.data = (uint64_t)(uintptr_t) queue,
	};

	pthread_mutex_lock(&activity->mutex);
	activity->input_queue = queue;
	// push_event_locked(activity, ev);
	push_event_wait_locked(activity, ev);
	pthread_mutex_unlock(&activity->mutex);
	dlg_trace("onInputQueueCreated done");
}

static void activity_onInputQueueDestroyed(ANativeActivity* aactivity,
		AInputQueue* queue) {
	dlg_trace("onInputQueueDestroyed");
	struct activity* activity = aactivity->instance;
	dlg_assert(activity);

	dlg_assert(activity->input_queue = queue);
	struct event ev = {
		.type = event_type_input_queue_destroyed,
		.data = (uint64_t)(uintptr_t) queue,
	};

	pthread_mutex_lock(&activity->mutex);
	activity->input_queue = NULL;
	push_event_wait_locked(activity, ev);
	pthread_mutex_unlock(&activity->mutex);
	dlg_trace("onInputQueueDestroyed done");
}

static void activity_onContentRectChanged(ANativeActivity* activity,
		const ARect* rect) {
	(void) activity;
	(void) rect;
	// no-op
}

static void activity_onConfigurationChanged(ANativeActivity* activity) {
	(void) activity;
	// no-op
}

static void activity_onLowMemory(ANativeActivity* activity) {
	(void) activity;
	// no-op
}

// App entrypoint
void ANativeActivity_onCreate(ANativeActivity* aactivity,
		void* savedState, size_t savedStateSize) {
	(void) savedState;
	(void) savedStateSize;

	// set native activity callbacks
	*aactivity->callbacks = (struct ANativeActivityCallbacks) {
		.onStart = activity_onStart,
		.onResume = activity_onResume,
		.onSaveInstanceState = activity_onSaveInstanceState,
		.onPause = activity_onPause,
		.onStop = activity_onStop,
		.onDestroy = activity_onDestroy,
		.onWindowFocusChanged = activity_onWindowFocusChanged,
		.onNativeWindowCreated = activity_onNativeWindowCreated,
		.onNativeWindowResized = activity_onNativeWindowResized,
		.onNativeWindowRedrawNeeded = activity_onNativeWindowRedrawNeeded,
		.onNativeWindowDestroyed = activity_onNativeWindowDestroyed,
		.onInputQueueCreated = activity_onInputQueueCreated,
		.onInputQueueDestroyed = activity_onInputQueueDestroyed,
		.onContentRectChanged = activity_onContentRectChanged,
		.onConfigurationChanged = activity_onConfigurationChanged,
		.onLowMemory = activity_onLowMemory,
	};

	// init static data
	int err;
	struct activity* activity = calloc(1, sizeof(*activity));
	activity->activity = aactivity;
	activity->thread_started = false;
	activity->destroyed = false;

	err = pthread_mutex_init(&activity->mutex, NULL);
	dlg_assertm(!err, "pthread_mutex_init: %s", strerror(errno));

	err = pthread_cond_init(&activity->cond, NULL);
	dlg_assertm(!err, "pthread_cond_init: %s", strerror(errno));

	aactivity->instance = activity;
	g_current_activity = activity;

	// set dlg log handler
	dlg_set_handler(output_handler, activity);
	dlg_trace("onCreate done");
}

