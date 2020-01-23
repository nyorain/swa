#pragma once

#include <swa/config.h>
#include <swa/private/impl.h>
#include <pthread.h>

#include <android/native_activity.h>
#include <android/native_window.h>

typedef struct AChoreographer AChoreographer;

typedef void* EGLSurface;
typedef void* EGLContext;

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_android {
	struct swa_display base;
	struct activity* activity;

	const char* appname;
	ALooper* looper;
	AChoreographer* choreographer;

	struct swa_window_android* window;
	uint64_t key_states[16]; // bitset

	// NOTE: could use a ringbuffer here
	// probably not worth it
	unsigned n_events;
	unsigned cap_events;
	struct event* events;
	enum swa_keyboard_mod keyboard_mods;

	struct swa_egl_display* egl;
};

struct swa_android_vk_surface {
	uintptr_t instance;
	uint64_t surface;
};

struct swa_android_gl_surface {
	EGLSurface surface;
	EGLContext context;
};

struct swa_android_buffer_surface {
	bool active;
};

struct swa_window_android {
	struct swa_window base;
	struct swa_display_android* dpy;
	bool valid;
	bool redraw;
	bool focus;
	bool initial_events;

	// TODO: needed for surface re-creation
	// should probably be removed by only the information we actually need
	struct swa_window_settings settings;
	enum swa_surface_type surface_type;
	union {
		struct swa_android_buffer_surface buffer;
		struct swa_android_vk_surface vk;
		struct swa_android_gl_surface gl;
	};
};

#ifdef __cplusplus
}
#endif
