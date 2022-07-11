#pragma once

#include <swa/swa.h>

#ifdef __cplusplus
extern "C" {
#endif

// Creates a winapi display implementation.
SWA_API struct swa_display* swa_display_winapi_create(const char* appname);
SWA_API bool swa_display_is_winapi(struct swa_display* dpy);

SWA_API enum swa_key swa_winapi_to_key(unsigned vkcode);
SWA_API unsigned swa_key_to_winapi(enum swa_key key);

#ifdef __cplusplus
}
#endif
