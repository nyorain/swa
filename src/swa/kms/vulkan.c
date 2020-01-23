#include <swa/swa.h>
#include <swa/private/impl.h>
#include <swa/private/kms/vulkan.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <pml.h>
#include <dlg/dlg.h>
#include <sys/poll.h>
#include <sys/eventfd.h> // TODO: linux only

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

// TODO: use this instead of manually loading everything everytime
struct drm_vk_api {
	PFN_vkDestroySurfaceKHR destroySurfaceKHR;
	PFN_vkGetPhysicalDeviceDisplayPropertiesKHR getPhysicalDeviceDisplayPropertiesKHR;
	PFN_vkGetDisplayPlaneSupportedDisplaysKHR getDisplayPlaneSupportedDisplaysKHR;
	PFN_vkGetDisplayModePropertiesKHR getDisplayModePropertiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR getPhysicalDeviceDisplayPlanePropertiesKHR;
	PFN_vkCreateDisplayPlaneSurfaceKHR createDisplayPlaneSurfaceKHR;
	PFN_vkGetDisplayPlaneCapabilitiesKHR getDisplayPlaneCapabilitiesKHR;
	PFN_vkRegisterDisplayEventEXT registerDisplayEventEXT;
};

struct swa_kms_vk_surface {
	VkInstance instance;
	VkPhysicalDevice phdev;
	VkDisplayPropertiesKHR display;
	VkSurfaceKHR surface;
	VkDisplayModeKHR mode;
	unsigned width, height;
	struct swa_window* window;

	VkDevice device;
	bool has_swapchain;
	bool has_display_control;

	struct {
		VkFence fence;
		VkFence next_fence;
		pthread_mutex_t mutex;
		pthread_t thread;
		pthread_cond_t cond;
		bool join;
		int eventfd;
		struct pml_io* eventfd_io;
		bool redraw;
	} frame;
};

static bool has_extension(const VkExtensionProperties *avail,
		uint32_t availc, const char *req) {
	for(size_t j = 0; j < availc; ++j) {
		if(!strcmp(avail[j].extensionName, req)) {
			return true;
		}
	}

	return false;
}

static void finish_device(struct swa_kms_vk_surface* surf) {
	if(surf->device) {
		vkDestroyDevice(surf->device, NULL);
		surf->device = VK_NULL_HANDLE;
	}

	surf->has_swapchain = false;
	surf->has_display_control = false;
}

static void init_device(struct swa_kms_vk_surface* surf) {
	VkResult res;

	// find supported extensions
	VkExtensionProperties* phdev_exts = NULL;
	uint32_t phdev_extc = 0;

	res = vkEnumerateDeviceExtensionProperties(surf->phdev, NULL,
		&phdev_extc, NULL);
	if((res != VK_SUCCESS) || (phdev_extc == 0)) {
		dlg_error_vk("Could not enumerate device extensions (1)", res);
		goto error;
	}

	phdev_exts = malloc(sizeof(*phdev_exts) * phdev_extc);
	res = vkEnumerateDeviceExtensionProperties(surf->phdev, NULL,
		&phdev_extc, phdev_exts);
	if(res != VK_SUCCESS) {
		free(phdev_exts);
		dlg_error_vk("Could not enumerate device extensions (2)", res);
		goto error;
	}

	for(size_t j = 0; j < phdev_extc; ++j) {
		dlg_debug("Vulkan Device extensions %s", phdev_exts[j].extensionName);
	}

	unsigned n_exts = 0;
	const char* exts[2];

	const char* dev_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if(has_extension(phdev_exts, phdev_extc, dev_ext)) {
		exts[n_exts++] = dev_ext;
		surf->has_swapchain = true;
	}

	dev_ext = VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME;
	if(has_extension(phdev_exts, phdev_extc, dev_ext)) {
		exts[n_exts++] = dev_ext;
		surf->has_display_control = true;
	}

	free(phdev_exts);

	// TODO: really find a present queue for the cursor surface
	// find queues
	VkDeviceQueueCreateInfo qinfos[2] = {0};
	unsigned n_queues = 1;
	float prio = 1.f;
	for(unsigned i = 0u; i < n_queues; ++i) {
		qinfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		qinfos[i].queueFamilyIndex = 0;
		qinfos[i].queueCount = 1;
		qinfos[i].pQueuePriorities = &prio;
	}

	VkDeviceCreateInfo dev_info = {0};
	dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dev_info.queueCreateInfoCount = n_queues;
	dev_info.pQueueCreateInfos = qinfos;
	dev_info.enabledExtensionCount = n_exts;
	dev_info.ppEnabledExtensionNames = exts;

	res = vkCreateDevice(surf->phdev, &dev_info, NULL, &surf->device);
	if(res != VK_SUCCESS){
		dlg_error_vk("Failed to create vulkan device", res);
		goto error;
	}

	return;

error:
	finish_device(surf);
	return;
}

static void* frame_thread(void* data) {
	struct swa_kms_vk_surface* surf = data;
	while(true) {
		pthread_mutex_lock(&surf->frame.mutex);
		if(surf->frame.join) {
			pthread_mutex_unlock(&surf->frame.mutex);
			break;
		}

		// TODO: if there is a new fence, abandon the old one and
		// use that instead, i guess. We don't wanna wait for
		// old fences
		if(!surf->frame.fence) {
			while(!surf->frame.next_fence && !surf->frame.join) {
				dlg_trace("wait c");
				pthread_cond_wait(&surf->frame.cond, &surf->frame.mutex);
			}

			if(surf->frame.join) {
				pthread_mutex_unlock(&surf->frame.mutex);
				break;
			}

			surf->frame.fence = surf->frame.next_fence;
			surf->frame.next_fence = VK_NULL_HANDLE;
		}

		pthread_mutex_unlock(&surf->frame.mutex);

		// 0.1 sec timeout; mainly relevant for destruction since we
		// can't wake the thread during waitForFences.
		static const uint64_t timeout = 100 * 1000 * 1000;
		VkResult res = vkWaitForFences(surf->device, 1,
			&surf->frame.fence, true, timeout);
		if(res == VK_SUCCESS) {
			dlg_trace("display fence completed");
			vkDestroyFence(surf->device, surf->frame.fence, NULL);
			surf->frame.fence = VK_NULL_HANDLE;

			int64_t v = 1;
			write(surf->frame.eventfd, &v, 8);
		} else if(res != VK_TIMEOUT) {
			dlg_error_vk("vkWaitForFences", res);
		}
	}

	if(surf->frame.fence) {
		vkDestroyFence(surf->device, surf->frame.fence, NULL);
		surf->frame.fence = VK_NULL_HANDLE;
	}

	return NULL;
}

static void eventfd_readable(struct pml_io* io, unsigned revents) {
	struct swa_kms_vk_surface* surf = pml_io_get_data(io);
	dlg_assert(pml_io_get_fd(io) == surf->frame.eventfd);
	dlg_assert(surf->window->listener->draw);
	dlg_trace("eventfd readable");

	// reset eventfd
	int64_t v = 0;
	read(surf->frame.eventfd, &v, 8);

	// send draw event
	dlg_trace("eventfd reset complete");
	if(surf->frame.redraw) {
		dlg_trace("sending draw event due to eventfd");
		surf->frame.redraw = false;
		surf->window->listener->draw(surf->window);
	}
}

static void init_frame(struct pml* pml, struct swa_kms_vk_surface* surf) {
	surf->frame.eventfd = eventfd(0, EFD_CLOEXEC);
	if(surf->frame.eventfd < 0) {
		dlg_error("eventfd failed: %s", strerror(errno));
		goto error;
	}

	surf->frame.eventfd_io = pml_io_new(pml, surf->frame.eventfd,
		POLLIN, eventfd_readable);
	if(!surf->frame.eventfd_io) {
		goto error;
	}

	pml_io_set_data(surf->frame.eventfd_io, surf);

	pthread_mutex_init(&surf->frame.mutex, NULL);
	pthread_cond_init(&surf->frame.cond, NULL);
	pthread_create(&surf->frame.thread, NULL, frame_thread, surf);

	return;

error:
	return;
}

void swa_kms_vk_surface_destroy(struct swa_kms_vk_surface* surf) {
	if(!surf) {
		return;
	}

	if(surf->surface) {
		vkDestroySurfaceKHR(surf->instance, surf->surface, NULL);
	}
	if(surf->frame.thread) {
		pthread_mutex_lock(&surf->frame.mutex);
		surf->frame.join = true;
		pthread_cond_signal(&surf->frame.cond);
		pthread_mutex_unlock(&surf->frame.mutex);
		pthread_join(surf->frame.thread, NULL);

		pthread_mutex_destroy(&surf->frame.mutex);
		pthread_cond_destroy(&surf->frame.cond);
	}

	// TODO: cleanup fences
	finish_device(surf);
	free(surf);
}

struct swa_kms_vk_surface* swa_kms_vk_surface_create(struct pml* pml,
		VkInstance instance, struct swa_window* window) {
	VkResult res;

	// TODO: allow application to pass in phdev to use?
	// or at least allow it to query the used device? but i guess
	// only the physical device that we choose here will be able
	// to present on the returned surface, so applications can
	// query it implicitly. But it's a bad solution, make this
	// explicit (or document it!).
	// But then we should enumerate all phdevs; important for
	// supporting multiple monitors, that may be plugged into
	// different phdevs.
	// maybe allow to configure this via env vars?
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

	struct swa_kms_vk_surface* surf = calloc(1, sizeof(*surf));
	surf->instance = instance;
	surf->phdev = phdev;
	surf->window = window;

	VkDisplayPropertiesKHR* display_props = NULL;
	VkDisplayPlanePropertiesKHR* plane_props = NULL;
	VkDisplayModePropertiesKHR* modes = NULL;

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

	display_props = calloc(display_count, sizeof(*display_props));
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
	modes = calloc(modes_count, sizeof(*modes));
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
	// we could try to match the requested window size. We could even
	// create a new display mode for it.
	// This is probably a bad idea in many cases though.
	// We could also implement 'resize' using custom modes.
	// maybe allow to configure this via env vars?
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

	plane_props = calloc(plane_count, sizeof(*plane_props));
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

	surf->width = mode_props->parameters.visibleRegion.width;
	surf->height = mode_props->parameters.visibleRegion.height;
	surf->mode = mode; // TODO: not really needed later on, right?

	free(display_props);
	free(plane_props);
	free(modes);

	init_device(surf);

	// If the window listener doesn't want draw events, we don't need
	// this whole frame thing
	if(surf->has_display_control) {
		if(window->listener->draw) {
			init_frame(pml, surf);
		}
	} else {
		dlg_warn("No support for display control extensions");
	}

	return surf;

error:
	free(display_props);
	free(plane_props);
	free(modes);
	swa_kms_vk_surface_destroy(surf);
	return NULL;
}

// TODO: when this is called *before* vkQueuePresentKHR was called
// the first time (and surface_frame should always be called before
// queuePresent), the registerDisplayEventEXT call may fail.
void swa_kms_vk_surface_frame(struct swa_kms_vk_surface* surf) {
	if(!surf->has_display_control) {
		return;
	}

	dlg_assert(surf->device && surf->frame.thread);
	VkDevice device = surf->device;

	PFN_vkRegisterDisplayEventEXT registerDisplayEventEXT =
		(PFN_vkRegisterDisplayEventEXT)
		vkGetDeviceProcAddr(device, "vkRegisterDisplayEventEXT");
	dlg_assert(registerDisplayEventEXT);

	VkFence fence;
	VkDisplayEventInfoEXT event_info = {0};
	event_info.sType = VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT;
	event_info.displayEvent = VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT;
	VkResult res = registerDisplayEventEXT(device,
		surf->display.display, &event_info, NULL, &fence);
	if(res != VK_SUCCESS) {
		dlg_error_vk("vkRegisterDisplayEventEXT", res);
		return;
	}

	pthread_mutex_lock(&surf->frame.mutex);
	if(surf->frame.next_fence || surf->frame.fence) {
		dlg_info("Detected mixing surface_frame with manual drawing");
	}
	if(surf->frame.next_fence) {
		vkDestroyFence(device, surf->frame.next_fence, NULL);
	}

	surf->frame.next_fence = fence;
	pthread_cond_signal(&surf->frame.cond);
	pthread_mutex_unlock(&surf->frame.mutex);
}

bool swa_kms_vk_surface_refresh(struct swa_kms_vk_surface* surf) {
	bool pending;
	pthread_mutex_lock(&surf->frame.mutex);
	pending = (bool)surf->frame.fence || (bool)surf->frame.next_fence;
	pthread_mutex_unlock(&surf->frame.mutex);

	if(pending) {
		dlg_trace("refresh: pending, delayed redraw");
		surf->frame.redraw = true;
		return true;
	}

	// immediate (i.e. via defer event) redraw.
	return false;
}

VkSurfaceKHR swa_kms_vk_surface_get_surface(struct swa_kms_vk_surface* surf) {
	return surf->surface;
}

void swa_kms_vk_surface_get_size(struct swa_kms_vk_surface* surf,
		unsigned* width, unsigned* height) {
	*width = surf->width;
	*height = surf->height;
}
