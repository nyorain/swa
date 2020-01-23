#pragma once

#include <swa/swa.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeActivity ANativeActivity;

// Creates an android display implementation.
SWA_API struct swa_display* swa_display_android_create(const char* appname);

// Returns whether the given display is implemented by the android backend.
// If this function returns false, using android-specific functions is an error.
SWA_API bool swa_display_is_android(struct swa_display*);

SWA_API ANativeWindow* swa_display_android_get_native_window(struct swa_display*);
SWA_API ANativeActivity* swa_display_android_get_native_activity(struct swa_display*);
