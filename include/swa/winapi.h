#pragma once

#include <swa/swa.h>

#ifdef __cplusplus
extern "C" {
#endif

// Creates a winapi display implementation.
SWA_API struct swa_display* swa_display_winapi_create(const char* appname);
SWA_API bool swa_display_is_winapi(struct swa_display* dpy);

#ifdef __cplusplus
}
#endif
