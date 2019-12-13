#include "vulkan.h"
#include <stdlib.h>
#include <assert.h>
#include <dlg/dlg.h>

#define dlg_error_vk(fmt, res) dlg_error( \
	"vulkan error %s (%d): " fmt, \
	vulkan_strerror(res), res)

const char *vulkan_strerror(VkResult err) {
	#define ERR_STR(r) case VK_ ##r: return #r
	switch (err) {
		ERR_STR(SUCCESS);
		ERR_STR(NOT_READY);
		ERR_STR(TIMEOUT);
		ERR_STR(EVENT_SET);
		ERR_STR(EVENT_RESET);
		ERR_STR(INCOMPLETE);
		ERR_STR(ERROR_OUT_OF_HOST_MEMORY);
		ERR_STR(ERROR_OUT_OF_DEVICE_MEMORY);
		ERR_STR(ERROR_INITIALIZATION_FAILED);
		ERR_STR(ERROR_DEVICE_LOST);
		ERR_STR(ERROR_MEMORY_MAP_FAILED);
		ERR_STR(ERROR_LAYER_NOT_PRESENT);
		ERR_STR(ERROR_EXTENSION_NOT_PRESENT);
		ERR_STR(ERROR_FEATURE_NOT_PRESENT);
		ERR_STR(ERROR_INCOMPATIBLE_DRIVER);
		ERR_STR(ERROR_TOO_MANY_OBJECTS);
		ERR_STR(ERROR_FORMAT_NOT_SUPPORTED);
		ERR_STR(ERROR_SURFACE_LOST_KHR);
		ERR_STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
		ERR_STR(SUBOPTIMAL_KHR);
		ERR_STR(ERROR_OUT_OF_DATE_KHR);
		ERR_STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
		ERR_STR(ERROR_VALIDATION_FAILED_EXT);
		default:
			return "<unknown>";
	}
	#undef ERR_STR
}

// TODO
struct drm_vk_api {
	PFN_vkGetPhysicalDeviceDisplayPropertiesKHR getPhysicalDeviceDisplayPropertiesKHR;
	PFN_vkGetDisplayPlaneSupportedDisplaysKHR getDisplayPlaneSupportedDisplaysKHR;
	PFN_vkGetDisplayModePropertiesKHR getDisplayModePropertiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR getPhysicalDeviceDisplayPlanePropertiesKHR;
	PFN_vkCreateDisplayPlaneSurfaceKHR createDisplayPlaneSurfaceKHR;
	PFN_vkGetDisplayPlaneCapabilitiesKHR getDisplayPlaneCapabilitiesKHR;
};

void drm_vk_surface_destroy(struct drm_vk_surface* surf) {
	if(!surf) {
		return;
	}

	free(surf);
}

struct drm_vk_surface* drm_vk_surface_create(VkInstance instance) {
	VkResult res;

	// TODO: allow application to pass in phdev to use?
	VkPhysicalDevice phdevs[10];
	uint32_t count = 10;
	res = vkEnumeratePhysicalDevices(instance, &count, phdevs);
	if((res != VK_SUCCESS  && res != VK_INCOMPLETE) || count == 0) {
		dlg_error_vk("Could not retrieve physical device", res);
		return NULL;
	}

	VkPhysicalDevice phdev = phdevs[0];
	// TODO: temporary workaround for my amdgpu kernel driver crashing
	// all the time... advanced penguin devices
	if(count > 1) {
		phdev = phdevs[1];
	}

	struct drm_vk_surface* surf = calloc(1, sizeof(*surf));

	// scan displays
	uint32_t display_count;
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(phdev, &display_count, NULL);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkGetPhysicalDeviceDisplayPropertiesKHR (1)", res);
		goto error;
	}

	dlg_info("VkDisplayKHR count: %d", display_count);
	if(display_count == 0) {
		dlg_error("Can't find any vulkan display");
		goto error;
	}

	VkDisplayPropertiesKHR* display_props = calloc(display_count, sizeof(*display_props));
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(phdev, &display_count, display_props);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkGetPhysicalDeviceDisplayPropertiesKHR (2)", res);
		goto error;
	}

	// TODO: we currently just choose the first display
	// we want to support multiple displays/windows
	surf->display = display_props[0];
	VkDisplayKHR display = surf->display.display;
	dlg_info("Using display '%s'", surf->display.displayName);

	// scan modes
	uint32_t modes_count;
	res = vkGetDisplayModePropertiesKHR(phdev, display, &modes_count, NULL);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkGetDisplayModePropertiesKHR (1)", res);
		goto error;
	}

	assert(modes_count > 0); // guaranteed by standard
	VkDisplayModePropertiesKHR* modes = calloc(modes_count, sizeof(*modes));
	res = vkGetDisplayModePropertiesKHR(phdev, display, &modes_count, modes);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkGetDisplayModePropertiesKHR (2)", res);
		goto error;
	}

	dlg_info("Detected modes:");
	for(unsigned m = 0; m < modes_count; ++m) {
		VkDisplayModePropertiesKHR* mode = &modes[m];
		dlg_info("  %dx%d, %d mHz",
			mode->parameters.visibleRegion.width,
			mode->parameters.visibleRegion.height,
			mode->parameters.refreshRate);
	}

	// TODO: don't just choose the first mode!
	VkDisplayModePropertiesKHR* mode_props = &modes[0];
	VkDisplayModeKHR mode = mode_props->displayMode;

	// scan planes
	uint32_t plane_count;
	res = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(phdev, &plane_count, NULL);
	if (res != VK_SUCCESS) {
		dlg_error_vk("vkGetPhysicalDeviceDisplayPlanePropertiesKHR (1)", res);
		goto error;
	}

    if(plane_count == 0) {
		dlg_error("Could not find any planes");
		goto error;
    }

	VkDisplayPlanePropertiesKHR* plane_props = calloc(plane_count, sizeof(*plane_props));
	res = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(phdev, &plane_count, plane_props);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkGetPhysicalDeviceDisplayPlanePropertiesKHR (2)", res);
		goto error;
	}

	// get a supported plane
	VkDisplayPlanePropertiesKHR* found_plane = NULL;
	unsigned plane_index;
    for(plane_index = 0; plane_index < plane_count; ++plane_index) {
		VkDisplayPlanePropertiesKHR* plane = &plane_props[plane_index];
		if(plane->currentDisplay && plane->currentDisplay != surf->display.display) {
			continue;
		}

		uint32_t supported_count;
		res = vkGetDisplayPlaneSupportedDisplaysKHR(phdev, plane_index, &supported_count, NULL);
		if(res != VK_SUCCESS) {
			dlg_error_vk("vkGetDisplayPlaneSupportedDisplaysKHR (1)", res);
			continue;
		}

        if(supported_count == 0) {
            continue;
        }

        VkDisplayKHR* supported_displays = calloc(supported_count, sizeof(*supported_displays));
        res = vkGetDisplayPlaneSupportedDisplaysKHR(phdev, plane_index,
			&supported_count, supported_displays);
		if(res != VK_SUCCESS) {
			dlg_error_vk("vkGetDisplayPlaneSupportedDisplaysKHR (2)", res);
			free(supported_displays);
			continue;
		}

		bool supported = false;
		for(unsigned i = 0u; i < supported_count; i++) {
            if(supported_displays[i] == display) {
                supported = true;
                break;
            }
        }

		free(supported_displays);
		if(supported) {
			found_plane = plane;
			break;
		}
	}

	if(!found_plane) {
		dlg_error("Could not find primary plane for output mode");
		goto error;
	}


	// TODO: check capabilities. Some way to find a cursor plane?
	// check if it matches requirements
	VkDisplayPlaneCapabilitiesKHR caps;
	res = vkGetDisplayPlaneCapabilitiesKHR(phdev, mode, plane_index, &caps);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkGetDisplayPlaneCapabilitiesKHR", res);
		goto error;
	}

	VkDisplaySurfaceCreateInfoKHR surf_info = {0};
	surf_info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
	surf_info.displayMode = mode;
	surf_info.planeIndex = plane_index;
	surf_info.planeStackIndex = found_plane->currentStackIndex;
	surf_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; // TODO: impl transform
	surf_info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR; // TODO: check support
	surf_info.imageExtent.width = mode_props->parameters.visibleRegion.width;
	surf_info.imageExtent.height = mode_props->parameters.visibleRegion.height;

	res = vkCreateDisplayPlaneSurfaceKHR(instance, &surf_info, NULL, &surf->surface);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkCreateDisplayPlaneSurfaceKHR", res);
		goto error;
	}

	// TODO: cleanup of props

	surf->mode = mode; // TODO: not really needed, right?
	surf->instance = instance;
	return surf;

error:
	// TODO: cleanup
	return NULL;
}
