#include <swa/egl.h>
#include <swa/swa.h>
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

	if(eglInitialize(dpy->display, &dpy->major, &dpy->minor) == EGL_FALSE) {
		dlg_error("Failed to initialize egl");
		goto err;
	}

	exts = eglQueryString(dpy->display, EGL_EXTENSIONS);
	dlg_assert(exts);
	if(!swa_egl_find_ext(exts, "EGL_KHR_get_all_proc_addresses")) {
		// This is critical since then calls to eglGetProcAddress cannot
		// be used to query core functionality. Most displays simply
		// forward calls to display_get_gl_proc_addr to eglGetProcAddress.
		dlg_warn("EGL_KHR_get_all_proc_addresses not available");
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

bool swa_egl_init_context(struct swa_egl_display* egl,
		const struct swa_gl_surface_settings* gls, bool transparent,
		EGLConfig* cfg, EGLContext* ctx) {
	EGLint api_bit;
	EGLint api;
	if(gls->api == swa_api_gl) {
		if(egl->major <= 1 && egl->minor < 4) {
			dlg_error("Creating an OpenGL EGL context requires EGL 1.4");
			return false;
		}

		api = EGL_OPENGL_API;
		api_bit = EGL_OPENGL_BIT;
	} else if(gls->api == swa_api_gles) {
		api = EGL_OPENGL_ES_API;
		api_bit = gls->major > 1 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES_BIT;
	} else {
		dlg_error("Invalid gl surface settings api: %d", gls->api);
		return false;
	}

	if(eglBindAPI(api) == EGL_FALSE) {
		dlg_error("Failed to bind EGL client api");
		return false;
	}

	// NOTE: can be improved, see
	// directx.com/2014/06/egl-understanding-eglchooseconfig-then-ignoring-it/
	// Need to evaulate.
	EGLDisplay edpy = egl->display;
	EGLint ca[16] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_ALPHA_SIZE, transparent ? 1 : 0,
		EGL_DEPTH_SIZE, gls->depth,
		EGL_STENCIL_SIZE, gls->stencil,
		EGL_SAMPLES, gls->samples,
	};

	unsigned i = 9;
	if(egl->major > 1 || egl->minor >= 2) {
		ca[++i] = EGL_RENDERABLE_TYPE;
		ca[++i] = api_bit;
	}

	if(egl->major > 1 || egl->minor >= 3) {
		ca[++i] = EGL_CONFORMANT;
		ca[++i] = api_bit;
	}

	ca[++i] = EGL_NONE;

	EGLint count;
	EGLBoolean ret = eglChooseConfig(edpy, ca, cfg, 1, &count);
	if(ret == EGL_FALSE || count == 0) {
		dlg_error("eglChooseConfig returned false");
		return false;
	}

	// create context
	const char* exts = eglQueryString(edpy, EGL_EXTENSIONS);
	EGLint ctxa[32];
	if(swa_egl_find_ext(exts, "EGL_KHR_create_context")) {
		ctxa[0] = EGL_CONTEXT_MAJOR_VERSION;
		ctxa[1] = gls->major;
		ctxa[2] = EGL_CONTEXT_MINOR_VERSION;
		ctxa[3] = gls->minor;
		ctxa[4] = EGL_CONTEXT_OPENGL_DEBUG;
		ctxa[5] = gls->debug;
		ctxa[6] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
		ctxa[7] = gls->forward_compatible;
		ctxa[8] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
		ctxa[9] = gls->compatibility ?
			EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT :
			EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
		ctxa[10] = EGL_NONE;
	} else {
		dlg_warn("EGL_KHR_create_context not available, "
			"can't respect gl context creation settings");
		ctxa[0] = EGL_CONTEXT_CLIENT_VERSION;
		ctxa[1] = gls->major;
		ctxa[2] = EGL_NONE;
	}

	*ctx = eglCreateContext(edpy, *cfg, NULL, ctxa);
	if(!*ctx) {
		dlg_error("eglCreateContext failed");
		return false;
	}

	return true;
}

EGLSurface swa_egl_create_surface(struct swa_egl_display* egl,
		void* handle, EGLConfig config, bool srgb) {
	dlg_assert(egl->api.createPlatformWindowSurface);

	EGLint sa[3] = {EGL_NONE};
	const char* exts = eglQueryString(egl->display, EGL_EXTENSIONS);
	if(exts && swa_egl_find_ext(exts, "EGL_KHR_gl_colorspace")) {
		sa[0] = EGL_GL_COLORSPACE_KHR;
		sa[1] = srgb ?  EGL_GL_COLORSPACE_SRGB_KHR : EGL_GL_COLORSPACE_LINEAR_KHR;
		sa[2] = EGL_NONE;
	} else if(srgb) {
		// must be support otherwise
		dlg_assert(egl->major < 1 || egl->minor < 5);
		dlg_error("EGL_KHR_gl_colorspace not supported, can't use srgb");
		return NULL;
	}

	EGLSurface surface = egl->api.createPlatformWindowSurface(
		egl->display, config, handle, sa);
	if(!surface) {
		dlg_error("eglCreatePlatformWindowSurface failed");
		return NULL;
	}

	return surface;
}
