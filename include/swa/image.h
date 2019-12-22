#pragma once

#include <swa/config.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Describes the layout of pixel color values in the data
// buffer of a swa_image. The format always describes the ordering
// of bytes in memory, not in logical words.
// Example: rgba32 means that a pixel uses 32 bits (4 bytes),
// where the first byte in memory contain the 'red' value.
// This means the representation is independent from the endianess
// of the system, but extracting the components from a pixel word using
// logical C operations requires knowledge about the endianess.
// This definition is consistent with OpenGL & Vulkan (when not
// using packed formats) and android but different to SDL (uses native type),
// cairo (uses native type) and drm/wayland (uses little endian,
// i.e. reversed byte order) format definitions.
// You can use swa_image_format_toggle_byte_word
// to get the equivalent format for another semantic.
// https://github.com/afrantzis/pixel-format-guide is a useful tool
// to understand pixel formats, swa is comparable to the non-packed
// vulkan formats.
enum swa_image_format {
	swa_image_format_none = 0,
	swa_image_format_a8,
	swa_image_format_rgba32,
	swa_image_format_argb32,
	swa_image_format_xrgb32,
	swa_image_format_rgb24,

	swa_image_format_abgr32,
	swa_image_format_bgra32,
	swa_image_format_bgrx32,
	swa_image_format_bgr24,
};

// Describes a 2 dimensional image.
struct swa_image {
	unsigned width, height;
	unsigned stride; // in bytes
	enum swa_image_format format;
	uint8_t* data;
};

struct swa_pixel {
	uint8_t r, g, b, a;
};


// Returns the size of one pixel in the given formats in bytes.
SWA_API unsigned swa_image_format_size(enum swa_image_format);

// Reads one pixel from the given image data with the given format.
SWA_API struct swa_pixel swa_read_pixel(const uint8_t* data, enum swa_image_format);

// Writes ones pixel into the given image data with the given format.
SWA_API void swa_write_pixel(uint8_t* data, enum swa_image_format, struct swa_pixel);

// Converts an image into a new image that already has data.
// Expects `src` to be a valid image.
// Expects `dst` to already have enough data allocated and width, height,
// stride and format set which will be used for conversion.
// Width and height of the both images must match. If you want to e.g.
// copy an image into a larger image you can achieve this by offsetting
// data and modifying the stride.
SWA_API void swa_convert_image(const struct swa_image* src,
	const struct swa_image* dst);

// Converts the format of the given image.
// Can also be used to create an image with a different stride.
// If `new_stride` is zero, will tightly pack the image.
// The data in the returned image must be freed.
SWA_API struct swa_image swa_convert_image_new(const struct swa_image* src,
	enum swa_image_format format, unsigned new_stride);

// Returns the corresponding image format with reversed component order.
// Example: argb32 will be mapped to bgra32.
SWA_API enum swa_image_format swa_image_format_reversed(enum swa_image_format);

// Computes the corresponding image format to the given one
// with toggled byte/word order semantics.
// In practice this simply means:
// On big endian systems it just returns the given format while
// on little endian systems it returns the reversed format.
// This is useful when translating between swa_image_format and another
// pixel format definition that is given in word order instead of
// swa_image_format's byte order.
// Note how only one function is needed since translating from byte
// to word and from word to byte order is the same operation.
SWA_API enum swa_image_format swa_image_format_toggle_byte_word(enum swa_image_format);


#ifdef __cplusplus
}
#endif
