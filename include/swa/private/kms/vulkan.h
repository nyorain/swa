#pragma once

#include <vulkan/vulkan.h>
#include <stdbool.h>

struct pml;
struct swa_window;

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: you have to disable validation layers, they seem to have
// bugs in the VkDisplayKHR api (as of december 2019) that lead to
// crashes (they don't recognize the fence returned by vkCreateDisplayEventEXT).
// And crashes usually mean you need a computer restart if you test this
// in a real drm environment

struct swa_kms_vk_surface* swa_kms_vk_surface_create(struct pml*, VkInstance,
	struct swa_window* window);
void swa_kms_vk_surface_destroy(struct swa_kms_vk_surface* surf);

// Returns false if refresh should happen via deferred event.
bool swa_kms_vk_surface_refresh(struct swa_kms_vk_surface* surf);
void swa_kms_vk_surface_frame(struct swa_kms_vk_surface* surf);
VkSurfaceKHR swa_kms_vk_surface_get_surface(struct swa_kms_vk_surface* surf);
void swa_kms_vk_surface_get_size(struct swa_kms_vk_surface* surf,
	unsigned* width, unsigned* height);

#ifdef __cplusplus
}
#endif
