#pragma once

#include <swa/swa.h>
#include <swa/impl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_x11 {
    struct swa_display base;
    bool error;
};

struct swa_window_x11 {
    struct swa_window base;
    struct swa_display_x11* dpy;
};

struct swa_display* swa_display_x11_create(void);

#ifdef __cplusplus
}
#endif
