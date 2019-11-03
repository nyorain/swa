#pragma once

// On posix systems (everything except windows), the swa_display implementation
// will be file descriptor powered, one way or another.
// To allow easy intergration of swa with other mainloops, additional
// functionality can therefore be offered on posix platforms.

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display;
bool swa_display_is_posix(struct swa_display*);

void swa_display_posix_prepare(struct swa_display*);
unsigned swa_display_posix_query(struct swa_display*, struct pollfd* fds, int* timeout);
void swa_display_posix_dispatch(struct swa_display*);

#ifdef __cplusplus
}
#endif