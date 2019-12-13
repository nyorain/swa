#pragma once

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drm_vk_surface {
	VkInstance instance;
	VkDisplayPropertiesKHR display;
	VkSurfaceKHR surface;
	VkDisplayModeKHR mode;
};

struct drm_vk_surface* drm_vk_surface_create(VkInstance instance);
void drm_vk_surface_destroy(struct drm_vk_surface* surf);

#ifdef __cplusplus
}
#endif
