#include <swa/private/kms/props.h>
#include <dlg/dlg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct prop_info {
	const char *name;
	size_t index;
};

static const struct prop_info connector_info[] = {
#define INDEX(name) (offsetof(union drm_connector_props, name) / sizeof(uint32_t))
	{ "CRTC_ID", INDEX(crtc_id) },
	{ "DPMS", INDEX(dpms) },
	{ "EDID", INDEX(edid) },
	{ "PATH", INDEX(path) },
	{ "link-status", INDEX(link_status) },
#undef INDEX
};

static const struct prop_info crtc_info[] = {
#define INDEX(name) (offsetof(union drm_crtc_props, name) / sizeof(uint32_t))
	{ "ACTIVE", INDEX(active) },
	{ "GAMMA_LUT", INDEX(gamma_lut) },
	{ "GAMMA_LUT_SIZE", INDEX(gamma_lut_size) },
	{ "MODE_ID", INDEX(mode_id) },
	{ "rotation", INDEX(rotation) },
	{ "scaling mode", INDEX(scaling_mode) },
#undef INDEX
};

static const struct prop_info plane_info[] = {
#define INDEX(name) (offsetof(union drm_plane_props, name) / sizeof(uint32_t))
	{ "CRTC_H", INDEX(crtc_h) },
	{ "CRTC_ID", INDEX(crtc_id) },
	{ "CRTC_W", INDEX(crtc_w) },
	{ "CRTC_X", INDEX(crtc_x) },
	{ "CRTC_Y", INDEX(crtc_y) },
	{ "FB_ID", INDEX(fb_id) },
	{ "IN_FORMATS", INDEX(in_formats) },
	{ "SRC_H", INDEX(src_h) },
	{ "SRC_W", INDEX(src_w) },
	{ "SRC_X", INDEX(src_x) },
	{ "SRC_Y", INDEX(src_y) },
	{ "type", INDEX(type) },
#undef INDEX
};

static int cmp_prop_info(const void *arg1, const void *arg2) {
	const char *key = arg1;
	const struct prop_info *elem = arg2;

	return strcmp(key, elem->name);
}

static bool scan_properties(int fd, uint32_t id, uint32_t type, uint32_t *result,
		const struct prop_info *info, size_t info_len) {
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, id, type);
	if (!props) {
		dlg_error("Failed to get DRM object properties: %s", strerror(errno));
		return false;
	}

	for (uint32_t i = 0; i < props->count_props; ++i) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			dlg_error("Failed to get DRM object property: %s", strerror(errno));
			continue;
		}

		const struct prop_info *p =
			bsearch(prop->name, info, info_len, sizeof(info[0]), cmp_prop_info);
		if (p) {
			result[p->index] = prop->prop_id;
		}

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
	return true;
}

bool get_drm_connector_props(int fd, uint32_t id, union drm_connector_props *out) {
	return scan_properties(fd, id, DRM_MODE_OBJECT_CONNECTOR, out->props,
		connector_info, sizeof(connector_info) / sizeof(connector_info[0]));
}

bool get_drm_crtc_props(int fd, uint32_t id, union drm_crtc_props *out) {
	return scan_properties(fd, id, DRM_MODE_OBJECT_CRTC, out->props,
		crtc_info, sizeof(crtc_info) / sizeof(crtc_info[0]));
}

bool get_drm_plane_props(int fd, uint32_t id, union drm_plane_props *out) {
	return scan_properties(fd, id, DRM_MODE_OBJECT_PLANE, out->props,
		plane_info, sizeof(plane_info) / sizeof(plane_info[0]));
}

bool get_drm_prop(int fd, uint32_t obj, uint32_t prop, uint64_t *ret) {
	drmModeObjectProperties *props =
		drmModeObjectGetProperties(fd, obj, DRM_MODE_OBJECT_ANY);
	if (!props) {
		return false;
	}

	bool found = false;

	for (uint32_t i = 0; i < props->count_props; ++i) {
		if (props->props[i] == prop) {
			*ret = props->prop_values[i];
			found = true;
			break;
		}
	}

	drmModeFreeObjectProperties(props);
	return found;
}
