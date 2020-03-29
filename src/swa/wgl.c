#include <swa/private/winapi.h>
#include <Wingdi.h>
#include <stdlib.h>
#include <dlg/dlg.h>

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
	#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
	#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
	#define WGL_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004
	#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
	#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
	#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
	#define WGL_CONTEXT_FLAGS_ARB 0x2094
	#define ERROR_INVALID_VERSION_ARB 0x2095
	#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
	#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
	#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
	#define ERROR_INVALID_PROFILE_ARB 0x2096
	#define WGL_SAMPLE_BUFFERS_ARB 0x2041
	#define WGL_SAMPLES_ARB 0x2042
	#define WGL_NUMBER_PIXEL_FORMATS_ARB 0x2000
	#define WGL_DRAW_TO_WINDOW_ARB 0x2001
	#define WGL_DRAW_TO_BITMAP_ARB 0x2002
	#define WGL_ACCELERATION_ARB 0x2003
	#define WGL_TRANSPARENT_ARB 0x200A
	#define WGL_SHARE_DEPTH_ARB 0x200C
	#define WGL_SHARE_STENCIL_ARB 0x200D
	#define WGL_SHARE_ACCUM_ARB 0x200E
	#define WGL_SUPPORT_GDI_ARB 0x200F
	#define WGL_SUPPORT_OPENGL_ARB 0x2010
	#define WGL_DOUBLE_BUFFER_ARB 0x2011
	#define WGL_STEREO_ARB 0x2012
	#define WGL_PIXEL_TYPE_ARB 0x2013
	#define WGL_COLOR_BITS_ARB 0x2014
	#define WGL_RED_BITS_ARB 0x2015
	#define WGL_RED_SHIFT_ARB 0x2017
	#define WGL_GREEN_BITS_ARB 0x2017
	#define WGL_GREEN_SHIFT_ARB 0x2018
	#define WGL_BLUE_BITS_ARB 0x2019
	#define WGL_BLUE_SHIFT_ARB 0x201A
	#define WGL_ALPHA_BITS_ARB 0x201B
	#define WGL_ALPHA_SHIFT_ARB 0x201C
	#define WGL_DEPTH_BITS_ARB 0x2022
	#define WGL_STENCIL_BITS_ARB 0x2023
	#define WGL_AUX_BUFFERS_ARB 0x2024
	#define WGL_NO_ACCELERATION_ARB 0x2025
	#define WGL_GENERIC_ACCELERATION_ARB 0x2026
	#define WGL_FULL_ACCELERATION_ARB 0x2027
	#define WGL_TYPE_RGBA_ARB 0x202B
#endif

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

typedef HGLRC (*pfn_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
typedef BOOL (*pfn_wglSwapIntervalEXT)(int);
typedef const char* (*pfn_wglGetExtensionStringARB)(HDC);
typedef BOOL(*pfn_wglGetPixelFormatAttribivARB)(HDC, int, int, UINT, const int*, int*);
typedef BOOL (*pfn_wglChoosePixelFormatARB)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

struct wgl_api {
    pfn_wglGetExtensionStringARB wglGetExtensionStringARB;
    pfn_wglChoosePixelFormatARB wglChoosePixelFormatARB;
    pfn_wglGetPixelFormatAttribivARB wglGetPixelFormatAttribivARB;
    pfn_wglCreateContextAttribsARB wglCreateContextAttribsARB;
    pfn_wglSwapIntervalEXT wglSwapIntervalEXT;

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

	size_t len = strlen(ext);
	char after = *(f + len);
	return (f == exts || *(f - 1) == ' ') &&
		(after == '\0' || after == ' ');
}

static bool wgl_init(struct swa_display_win* dpy) {
    HDC dc = GetDC(dpy->dummy_window);

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);
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

    api->wglGetExtensionStringARB = (pfn_wglGetExtensionStringARB)(void(*)(void))(ptr);
    const char* extensions = api->wglGetExtensionStringARB(dc);
    if(!extensions) {
        print_winapi_error("getExtensionStringARB");
        goto error;
    }

    #define LOAD(x) if(!(api->x = (pfn_##x)(void(*)(void)) wglGetProcAddress(#x))) { \
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

bool swa_wgl_init_context(struct swa_display_win* dpy, HDC hdc,
        const struct swa_gl_surface_settings* gls, bool transparent,
        void** out_context) {
    if(!dpy->wgl) {
        if(!wgl_init(dpy)) {
            return false;
        }
    }

    // TODO
    if(false && dpy->wgl->wglChoosePixelFormatARB) {
    } else {
        PIXELFORMATDESCRIPTOR pfd = {0};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = transparent ? 32 : 24;
        pfd.cStencilBits = (BYTE) gls->stencil;
        pfd.cDepthBits = (BYTE) gls->stencil;

        int pf = ChoosePixelFormat(hdc, &pfd);
        SetPixelFormat(hdc, pf, &pfd);
    }

    HGLRC ctx = NULL;
    if(dpy->wgl->wglCreateContextAttribsARB) {
        int attribs[20];
        unsigned c = 0u;
        attribs[c++] = WGL_CONTEXT_MAJOR_VERSION_ARB;
        attribs[c++] = gls->major;
        attribs[c++] = WGL_CONTEXT_MINOR_VERSION_ARB;
        attribs[c++] = gls->minor;

        unsigned flags = 0u;
        if(gls->debug) {
            flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
        }

        if(gls->api == swa_api_gles) {
            if(!dpy->wgl->exts.profile_es) {
                dlg_error("GLES contexts not supported");
                return false;
            }

            if(gls->major < 2) {
                dlg_error("GLES version not supported");
                return false;
            }

            attribs[c++] = WGL_CONTEXT_PROFILE_MASK_ARB;
            attribs[c++] = WGL_CONTEXT_ES2_PROFILE_BIT_EXT;
        } else {
            if(gls->compatibility) {
                flags |= WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
            } else {
                flags |= WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
            }
        }

        attribs[c++] = WGL_CONTEXT_FLAGS_ARB;
        attribs[c++] = flags;
        attribs[c++] = 0;
        attribs[c++] = 0;

        ctx = dpy->wgl->wglCreateContextAttribsARB(hdc, NULL, attribs);
        if(!ctx) {
            print_winapi_error("wglCreateContextAttribsARB");
            // don't return here. We try again below using the legacy api
        }
    }

    if(!ctx) {
        ctx = wglCreateContext(hdc);
        if(!ctx) {
            print_winapi_error("wlgCreateContext");
            return false;
        }
    }

    *out_context = ctx;
    return true;
}