#pragma once

#include <vulkan/vulkan.h>

struct pml;
struct swa_window;

#ifdef __cplusplus
extern "C" {
#endif

struct drm_vk_surface* drm_vk_surface_create(struct pml*, VkInstance,
	struct swa_window* window);
void drm_vk_surface_destroy(struct drm_vk_surface* surf);

void drm_vk_surface_refresh(struct drm_vk_surface* surf);
void drm_vk_surface_frame(struct drm_vk_surface* surf);
VkSurfaceKHR drm_vk_surface_get(struct drm_vk_surface* surf);

#ifdef __cplusplus
}
#endif
