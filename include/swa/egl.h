#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>

// fwd decls from egl.h
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;

#ifdef __cplusplus
extern "C" {
#endif

struct swa_egl_display {
	struct {
		PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay;
		PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC createPlatformWindowSurface;
	} api;

	EGLDisplay display;
};

struct swa_egl_context {
	EGLConfig config;
	EGLContext context;
};

struct swa_egl_display* swa_egl_display_create(EGLenum platform, void* dpy);
void swa_egl_display_destroy(struct swa_egl_display*);
bool swa_egl_find_ext(const char* exts, const char* ext);

#ifdef __cplusplus
}
#endif
