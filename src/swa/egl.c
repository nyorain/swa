#include <swa/egl.h>
#include <stdlib.h>
#include <dlg/dlg.h>

struct swa_egl_display* swa_egl_display_create(EGLenum platform, void* ndpy) {
	struct swa_egl_display* dpy = calloc(1, sizeof(*dpy));
	if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
		dlg_error("Failed to bind OpenGL EGL api");
		goto err;
	}

	dpy->api.getPlatformDisplay =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy->api.createPlatformWindowSurface =
		(PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)
		eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");

	if(!dpy->api.getPlatformDisplay || !dpy->api.createPlatformWindowSurface) {
		dlg_error("Couldn't load egl platform api");
		goto err;
	}

	dpy->display = dpy->api.getPlatformDisplay(platform, ndpy, NULL);
	if(!dpy->display) {
		dlg_error("Failed to get egl display");
		goto err;
	}

	EGLint major, minor;
	if(eglInitialize(dpy->display, &major, &minor) == EGL_FALSE) {
		dlg_error("Failed to initialize egl");
		goto err;
	}

	return dpy;

err:
	swa_egl_display_destroy(dpy);
	return NULL;
}

void swa_egl_display_destroy(struct swa_egl_display* dpy) {
	if(!dpy) {
		return;
	}

	if(dpy->display) {
		eglTerminate(dpy->display);
		eglReleaseThread();
	}
	free(dpy);
}
