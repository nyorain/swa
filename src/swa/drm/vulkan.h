#pragma once

#include <vulkan/vulkan.h>
#include <stdbool.h>

struct pml;
struct swa_window;

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: you have to disable validation layers, they seem to have
// bugs in the VkDisplayKHR api that lead to crashes (they don't
// recognize the fence returned by vkCreateDisplayEventEXT).
// And crashes usually mean you need a computer restart if you test this
// in a real drm environment. Which sucks.

struct drm_vk_surface* drm_vk_surface_create(struct pml*, VkInstance,
	struct swa_window* window);
void drm_vk_surface_destroy(struct drm_vk_surface* surf);

// Returns false if refresh should happen via deferred event.
bool drm_vk_surface_refresh(struct drm_vk_surface* surf);
void drm_vk_surface_frame(struct drm_vk_surface* surf);
VkSurfaceKHR drm_vk_surface_get(struct drm_vk_surface* surf);

#ifdef __cplusplus
}
#endif
