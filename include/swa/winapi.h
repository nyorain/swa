#pragma once

#include <swa/swa.h>
#include <swa/impl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display_win {
    struct swa_display base;
};

struct swa_window_win {
    struct swa_window base;
    struct swa_display_win* dpy;
};

struct swa_data_offer_win {
    struct swa_data_offer base;
    struct swa_display_win* dpy;
};

struct swa_display* swa_display_win_create(void);

#ifdef __cplusplus
}
#endif