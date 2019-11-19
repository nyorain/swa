#pragma once
#define UNICODE

#include <swa/swa.h>
#include <swa/impl.h>

#include <windows.h>
#include <winuser.h>
#include <windowsx.h>

// undefine the shittiest macros
// holy fuck microsoft...
#undef near
#undef far
#undef ERROR
#undef MemoryBarrier
#undef UNICODE

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_win {
    struct swa_display base;
    bool error;

    struct swa_window_win* focus;
    struct swa_window_win* mouse_over;
};

struct swa_win_buffer_surface {
    void* data;
    unsigned width;
    unsigned height;
    HBITMAP bitmap;
    bool active;
    HDC wdc; // only set when buffer is active
};

struct swa_window_win {
    struct swa_window base;
    struct swa_display_win* dpy;
    HWND handle;

    unsigned width, height;
    unsigned min_width, min_height;
    unsigned max_width, max_height;

    enum swa_surface_type surface_type;
    union {
        struct swa_win_buffer_surface buffer;
    };

    struct {
        HCURSOR handle;
        bool owned;
    } cursor;
};

struct swa_data_offer_win {
    struct swa_data_offer base;
    struct swa_display_win* dpy;
};

struct swa_display* swa_display_win_create(void);

#ifdef __cplusplus
}
#endif