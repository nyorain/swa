#pragma once

#include <swa/swa.h>

typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLDisplay;

// If the given display uses an egl display, returns it.
// Otherwise returns NULL. Backends might defer the creation of an EGLDisplay
// until the first gl window is created.
SWA_API EGLDisplay swa_display_get_egl_display(struct swa_display*);

// If the given window has an associated EGLContext, returns it.
// Otherwise returns NULL.
SWA_API EGLContext swa_window_get_egl_context(struct swa_window*);

// If the given window has an associated EGLSurface, returns it.
// Otherwise returns NULL.
SWA_API EGLSurface swa_window_get_egl_surface(struct swa_window*);

