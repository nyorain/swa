/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_xcursor_image {
	uint32_t width;		// actual width
	uint32_t height;	// actual height
	uint32_t hotspot_x;	// hot spot x (must be inside image)
	uint32_t hotspot_y;	// hot spot y (must be inside image)
	uint32_t delay;		// animation delay to next frame (ms)
	uint8_t *buffer;
};

struct swa_xcursor {
	unsigned int image_count;
	struct swa_xcursor_image **images;
	char *name;
	uint32_t total_delay; // length of the animation in ms
};

// Container for an Xcursor theme.
struct swa_xcursor_theme {
	unsigned int cursor_count;
	struct swa_xcursor **cursors;
	char *name;
	int size;
};

// Loads the named xcursor theme at the given cursor size (in pixels). This is
// useful if you need cursor images for your compositor to use when a
// client-side cursors is not available or you wish to override client-side
// cursors for a particular UI interaction (such as using a grab cursor when
// moving a window around).
struct swa_xcursor_theme* swa_xcursor_theme_load(const char *name, int size);
void swa_xcursor_theme_destroy(struct swa_xcursor_theme *theme);

// Obtains a swa_xcursor image for the specified cursor name (e.g. "left_ptr")
struct swa_xcursor* swa_xcursor_theme_get_cursor(
	struct swa_xcursor_theme* theme, const char* name);

// Returns the current frame number for an animated cursor give a monotonic time
// reference.
int swa_xcursor_frame(struct swa_xcursor* cursor, uint32_t time);

// Find the frame for a given elapsed time in a cursor animation
// as well as the time left until next cursor change.
int swa_xcursor_frame_and_duration(struct swa_xcursor* cursor, uint32_t time,
	uint32_t* duration);
int swa_xcursor_frame(struct swa_xcursor *_cursor, uint32_t time);

#ifdef __cplusplus
}
#endif
