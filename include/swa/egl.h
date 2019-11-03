#pragma once

// fwd decls from egl.h
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;

#ifdef __cplusplus
extern "C" {
#endif

struct swa_egl_context {
	EGLContext context;
};

#ifdef __cplusplus
}
#endif
