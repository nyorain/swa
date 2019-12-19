#pragma once

#include <swa/impl.h>
#include <pthread.h>

#include <android/native_activity.h>
#include <android/native_window.h>

struct AChoreographer;
typedef struct AChoreographer AChoreographer;

typedef void* EGLSurface;
typedef void* EGLContext;

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_android {
	struct swa_display base;

	ALooper* looper;
	AInputQueue* input_queue;
	AChoreographer* choreographer;

	struct swa_window_android* window;

	// TODO: could use a ringbuffer here
	unsigned n_events;
	unsigned cap_events;
	struct event* events;

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

	enum swa_surface_type surface_type;
	union {
		struct swa_android_buffer_surface buffer;
		struct swa_android_vk_surface vk;
		struct swa_android_gl_surface gl;
	};
};

struct swa_display* swa_display_android_create(void);

#ifdef __cplusplus
}
#endif
