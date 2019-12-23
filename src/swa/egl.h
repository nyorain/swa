#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>

// fwd decls from egl.h
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;

struct swa_gl_surface_settings;

#ifdef __cplusplus
extern "C" {
#endif

struct swa_egl_display {
	struct {
		PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay;
		PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC createPlatformWindowSurface;
	} api;

	EGLDisplay display;
	int major, minor;
};

struct swa_egl_display* swa_egl_display_create(EGLenum platform, void* dpy);
void swa_egl_display_destroy(struct swa_egl_display*);
bool swa_egl_find_ext(const char* exts, const char* ext);

bool swa_egl_init_context(struct swa_egl_display* egl,
	const struct swa_gl_surface_settings* gls, bool transparent,
	EGLConfig* cfg, EGLContext* ctx);
EGLSurface swa_egl_create_surface(struct swa_egl_display* egl,
	void* handle, EGLConfig config, bool srgb);
const char* swa_egl_error_msg(int code);
const char* swa_egl_last_error_msg(void);

#ifdef __cplusplus
}
#endif
