#pragma once

#include <swa/swa.h>
#include <swa/impl.h>

#define UNICODE
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
};

struct swa_window_win {
    struct swa_window base;
    struct swa_display_win* dpy;
    HWND handle;
};

struct swa_data_offer_win {
    struct swa_data_offer base;
    struct swa_display_win* dpy;
};

struct swa_display* swa_display_win_create(void);

#ifdef __cplusplus
}
#endif