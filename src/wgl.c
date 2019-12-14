#include <swa/winapi.h>
#include <Wingdi.h>
#include <stdlib.h>

// defined as macro so we don't lose file and line information
#define print_winapi_error(func) do { \
	wchar_t* buffer; \
	int code = GetLastError(); \
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | \
		FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, \
		code, 0, (wchar_t*) &buffer, 0, NULL); \
	dlg_errort(("winapi_error"), "%s: %ls", func, buffer); \
	LocalFree(buffer); \
} while(0)

typedef HGLRC (*PfnWglCreateContextAttribsARB)(HDC, HGLRC, const int*);
typedef BOOL (*PfnWglSwapIntervalEXT )(int);
typedef const char* (*PfnWglGetExtensionStringARB )(HDC);
typedef BOOL(*PfnWglGetPixelFormatAttribivARB )(HDC, int, int, UINT, const int*, int*);
typedef BOOL (*PfnWglChoosePixelFormatARB )(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

struct wgl_api {
    PfnWglGetExtensionStringARB wglGetExtensionStringARB;
    PfnWglChoosePixelFormatARB wglChoosePixelFormatARB;
    PfnWglGetPixelFormatAttribivARB wglGetPixelFormatAttribivARB;
    PfnWglCreateContextAttribsARB wglCreateContextAttribsARB;
    PfnWglSwapIntervalEXT wglSwapIntervalEXT;

    struct {
        bool swap_control_tear;
        bool profile;
        bool profile_es;
    } exts;
};

static bool find_ext(const char* exts, const char* ext) {
	const char* f = strstr(exts, ext);
	if(!f) {
		return false;
	}

	unsigned len = strlen(ext);
	char after = *(f + len);
	return (f == exts || *(f - 1) == ' ') &&
		(after == '\0' || after == ' ');
}

bool swa_wgl_init(struct swa_display_win* dpy) {
    HDC dc = GetDC(dpy->dummy_window);

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelformat(dc, pf, &pfd);
    HGLRC ctx = wglCreateContext(dc);
    if(!ctx) {
        print_winapi_error("wglCreateContext dummy");
        return false;
    }

    wglMakeCurrent(dc, ctx);

    struct wgl_api* api = calloc(1, sizeof(*api));
    PROC ptr = wglGetProcAddress("wglGetExtensionsStringARB");
    if(!ptr) {
        dlg_error("could not load wglGetExtensionsString");
        goto error;
    }

    api->wglGetExtensionStringARB = (PfnWglGetExtensionStringARB)(ptr);
    const char* extensions = api->wglGetExtensionStringARB(dc);
    if(!extensions) {
        print_winapi_error("getExtensionStringARB");
        goto error;
    }

    #define LOAD(x) if(!(api->x = wglGetProcAddress(#x))) { \
        dlg_error("Could not load '#x'"); \
        goto error; \
    }

    if(find_ext(extensions, "WGL_ARB_pixel_format")) {
        LOAD(wglChoosePixelFormatARB);
        LOAD(wglGetPixelFormatAttribivARB);
    }

	if(find_ext(extensions, "WGL_ARB_create_context")) {
        LOAD(wglCreateContextAttribsARB);
    }

	if(find_ext(extensions, "WGL_EXT_swap_control")) {
        LOAD(wglSwapIntervalEXT);
    }

	api->exts.swap_control_tear = find_ext(extensions, "WGL_EXT_swap_control_tear");
	api->exts.profile = find_ext(extensions, "WGL_EXT_create_context_profile");
	api->exts.profile_es = find_ext(extensions, "WGL_EXT_create_context_es2_profile");

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(ctx);
    dpy->wgl = api;
    return true;

error:
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(ctx);
    free(api);
    return false;
}

bool swa_wgl_init_context(struct swa_display_win* dpy, HDC dc,
        const struct swa_gl_surface_settings* gls, bool transparent,
        void** context) {
}