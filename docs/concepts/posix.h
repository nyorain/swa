#pragma once

// On posix systems (everything except windows), the swa_display implementation
// will be file descriptor powered, one way or another.
// To allow easy intergration of swa with other mainloops, additional
// functionality can therefore be offered on posix platforms.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pollfd;
struct swa_display;
struct mainloop;

// Returns whether the given display has a posix implementation.
bool swa_display_is_posix(struct swa_display*);

// The following api can be used to integrate swa into another mainloop
// instead of using `swa_display_{poll,wait}_events`.
// ```
// // we already made sure `swa_display_is_posix(dpy)` is true
// while(run) {
//  int timeout;
// 	swa_display_posix_prepare(dpy);
//  unsigned count = swa_display_posix_query(dpy, NULL, 0, &timeout);
//  struct pollfd* fds = calloc(count, sizeof(*fds));
//  swa_display_posix_query(dpy, fds, count, &timeout);
//  // poll fds with timeout by yourself or leave that to another
//  // mainloop api such as glib, qt, libuv or whatever
//  swa_display_posix_dispatch(dpy, fds, count);
//  free(fds);
// }
// ```

// Prepares the displays timeout and file descriptors.
// Must be called in every mainloop iteration before calling
// `swa_display_posix_query` or polling the returned file descriptors.
// Calling this on displays that don't return true for `swa_display_is_posix`
// is an error.
// As `swa_display_{poll, wait}_events`, this returns false if there is a critical
// error that means the display should no longer be used.
bool swa_display_posix_prepare(struct swa_display*);

// Queries the displays timeout and file descriptors.
// Can be called multiple times between `swa_display_posix_prepare`,
// the custom polling and `swa_display_posix_dispatch` but must
// be called every mainloop iteration since fds and prepared timeout can
// change at any time.  The buffer for the pollfd values is provided
// by the caller.
// - fds: an array with at least size n_fds into which the file descriptors
//   and events to be polled will be written.
//   Can be NULL if n_fds is 0 (useful to just query the number of
//   available fds before allocating).
// - n_fds: number of items in the `fds` buffer
// - timeout: will be set to the prepared timeout.
//   -1 means that there is no active timer event and polling should happen
//   without timeout. 0 means that polling shouldn't happen at all.
//   In this case, one can skip the polling step directly and
//   call `swa_display_posix_dispatch`.
// Will always return the number of internally avilable fds. If n_fds
// is smaller than this number, will only write the first n_fds values of fds.
// Otherwise, if n_fds is greater, will not modify the remaining values in fds.
// This means, calling this function with fds = NULL and n_fds = 0 to
// just query the timeout or number of available fds is valid.
// Calling this on displays that don't return true for `swa_display_is_posix`
// is an error.
unsigned swa_display_posix_query(struct swa_display*,
	struct pollfd* fds, unsigned n_fds, int* timeout);

// Dispatches all ready events. Must be called after the custom
// polling. If the timeout returned from swa_display_posix_query,
// the fds can be NULL and n_fds 0.
// - fds: the pollfd values from `swa_display_posix_query`, now filled with the
//   revents from poll.
// - n_fds: number of elements in the 'fds' array.
// The data in fds and n_fds (except revents) should match what was
// returned by `swa_display_posix_query`.
// After this call, one iteration of the mainloop is complete and
// the next iteration can be started using 'swa_display_posix_prepare'.
// Calling this on displays that don't return true for `swa_display_is_posix`
// is an error.
void swa_display_posix_dispatch(struct swa_display*, struct pollfd* fds,
	unsigned n_fds);

// Many posix display implementations use the posix_mainloop
// (see github.com/nyorain/posix_mainloop) library as internal mainloop.
// Will return this object, which can be used to easily add timers,
// i/o and defer callbacks to the already existent mainloop.
// Since not all posix display implementations need this, some might
// return NULL.
struct mainloop* swa_display_posix_get_mainloop(struct swa_display*);

#ifdef __cplusplus
}
#endif
