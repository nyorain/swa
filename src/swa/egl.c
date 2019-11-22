#include <swa/egl.h>
#include <stdlib.h>
#include <dlg/dlg.h>
#include <string.h>

struct swa_egl_display* swa_egl_display_create(EGLenum platform, void* ndpy) {
	struct swa_egl_display* dpy = calloc(1, sizeof(*dpy));

	// it's important that we do this here since eglGetProcAddress may return
	// a non-null value even if the extension isn't supported and we
	// would crash in that case.
	// EGL_EXT_platform_base is a client extension and we would therefore
	// find it in a call to eglQueryString without display.
	// Calling eglQueryString without display is valid since egl 1.5
	// or with an extension before. But if that is not supported,
	// we will simply generate an error here, and exts will be NULL.
	// The platform base extension will likely not be supported in
	// that case anyways.
	const char* exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if(!exts || !swa_egl_find_ext(exts, "EGL_EXT_platform_base")) {
		dlg_error("EGL_EXT_platform_base not supported");
		goto err;
	}

	if(eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
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
	eglReleaseThread();
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

bool swa_egl_find_ext(const char* exts, const char* ext) {
	const char* f = strstr(exts, ext);
	if(!f) {
		return false;
	}

	unsigned len = strlen(ext);
	char after = *(f + len);
	return (f == exts || *(f - 1) == ' ') &&
		(after == '\0' || after == ' ');
}
