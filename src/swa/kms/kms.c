#define _POSIX_C_SOURCE 200809L

#include <swa/private/kms/kms.h>
#include <swa/private/kms/props.h>
#include <swa/private/kms/xcursor.h>
#include <swa/private/xkb.h>
#include <dlg/dlg.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/signalfd.h>
#include <sys/poll.h>
#include <sys/mman.h>

#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>

#include <libudev.h>
#include <libinput.h>

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <linux/input-event-codes.h>

#ifdef SWA_WITH_VK
  #include <swa/private/kms/vulkan.h>
#endif

#ifdef SWA_WITH_GL
  #include <swa/private/egl.h>
  #include <gbm.h>
#endif

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;

// from xcursor.c
const char* const* swa_get_xcursor_names(enum swa_cursor_type type);

static struct swa_display_kms* get_display_kms(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_kms*) base;
}

static struct swa_window_kms* get_window_kms(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_kms*) base;
}

static bool add_fd_flags(int fd, int add_flags) {
	long flags = fcntl(fd, F_GETFD);
	if(flags == -1) {
		dlg_error("fcntl (get): %s (%d)", strerror(errno), errno);
		return false;
	}

	if(fcntl(fd, F_SETFD, flags | add_flags) == -1) {
		dlg_error("fcntl (set): %s (%d)", strerror(errno), errno);
		return false;
	}

	return true;
}

static bool swa_pipe(int fds[static 2]) {
	// NOTE: on linux we could use pipe2 here, not as racy
	int err = pipe(fds);
	if(err < 0) {
		dlg_error("pipe: %s (%d)", strerror(errno), errno);
		return false;
	}

	int flags = O_NONBLOCK | FD_CLOEXEC;
	if(!add_fd_flags(fds[0], flags) || !add_fd_flags(fds[0], flags)) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}

	return true;
}

static void finish_dumb_buffer(struct swa_display_kms* dpy,
		struct swa_kms_dumb_buffer* buf) {
	if(buf->fb_id) {
		drmModeRmFB(dpy->drm.fd, buf->fb_id);
	}
	if(buf->data) {
		munmap(buf->data, buf->size);
	}
	if(buf->gem_handle) {
		struct drm_mode_destroy_dumb destroy = { .handle = buf->gem_handle };
		drmIoctl(dpy->drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	}

	memset(buf, 0x0, sizeof(*buf));
}

static bool init_dumb_buffer(struct swa_display_kms* dpy,
		unsigned width, unsigned height, unsigned format,
		struct swa_kms_dumb_buffer* buf) {
	// The create ioctl uses the combination of depth and bpp to infer
	// a format; 24/32 refers to DRM_FORMAT_XRGB8888 as defined in
	// the drm_fourcc.h header. These arguments are the same as given
	// to drmModeAddFB, which has since been superseded by
	// drmModeAddFB2 as the latter takes an explicit format token.
	//
	// We only specify these arguments; the driver calculates the
	// pitch (also known as stride or row length) and total buffer size
	// for us, also returning us the GEM handle.
	//
	// For more information on pixel formats, a very useful reference
	// is the Pixel Format Guide to the Galaxy, which covers most of the
	// pixel formats used across the low-level graphics stack:
	// https://afrantzis.com/pixel-format-guide/
	struct drm_mode_create_dumb create = {
		.width = width,
		.height = height,
		.bpp = 32,
	};
	int err = drmIoctl(dpy->drm.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	if(err != 0) {
		dlg_error("Failed to create %u x %u dumb buffer: %s",
			create.width, create.height, strerror(errno));
		goto err;
	}

	assert(create.handle > 0);
	assert(create.pitch >= create.width * (create.bpp / 8));
	assert(create.size >= create.pitch * create.height);

	buf->gem_handle = create.handle;
	buf->stride = create.pitch;

	// In order to map the buffer, we call an ioctl specific to the buffer
	// type, which returns us a fake offset to use with the mmap syscall.
	// mmap itself then works as you expect.
	//
	// Note this means it is not possible to map arbitrary offsets of
	// buffers without specifically requesting it from the kernel.
	struct drm_mode_map_dumb map = {
		.handle = buf->gem_handle,
	};
	err = drmIoctl(dpy->drm.fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if(err != 0) {
		dlg_error("failed to get %u x %u mmap offset: %s",
			create.width, create.height, strerror(errno));
		goto err;
	}

	buf->data = mmap(NULL, create.size, PROT_WRITE, MAP_SHARED,
		dpy->drm.fd, map.offset);
	if(buf->data == MAP_FAILED) {
		dlg_error("failed to mmap %u x %u dumb buffer: %s",
			create.width, create.height, strerror(errno));
		goto err;
	}

	buf->size = create.size;

	// create framebuffer
	// TODO: use modifier api
	uint32_t pitches[4] = {buf->stride, 0, 0, 0};
	uint32_t gem_handles[4] = {buf->gem_handle, 0, 0, 0};
	uint32_t offsets[4] = {0, 0, 0, 0};
	err = drmModeAddFB2(dpy->drm.fd, width, height,
		format, gem_handles, pitches, offsets, &buf->fb_id, 0);

	if(err != 0 || buf->fb_id == 0) {
		dlg_error("AddFB2 failed: %s", strerror(errno));
		goto err;
	}

	dlg_trace("created dumb buffer %d: width %d, height %d, stride %d, size %ld",
		buf->gem_handle, width, height, buf->stride, buf->size);
	return true;

err:
	finish_dumb_buffer(dpy, buf);
	return false;
}

struct atomic {
	drmModeAtomicReq *req;
	bool failed;
};

static void atomic_add(struct atomic* atom, uint32_t id, uint32_t prop, uint64_t val) {
	if(!atom->failed && drmModeAtomicAddProperty(atom->req, id, prop, val) < 0) {
		dlg_error("Failed to add atomic DRM property: %s", strerror(errno));
		atom->failed = true;
	}
}


// window
static void win_destroy(struct swa_window* base) {
	struct swa_window_kms* win = get_window_kms(base);
	if(win->output) win->output->window = NULL;
	if(win->dpy->input.pointer.over == win) {
		win->dpy->input.pointer.over = NULL;
	}
	if(win->dpy->input.keyboard.focus == win) {
		win->dpy->input.keyboard.focus = NULL;
	}

	// TODO: full cleanup
	free(win);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
	(void) base;
	return swa_window_cap_none;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	(void) base; (void) w; (void) h;
	dlg_error("win_set_min_size not supported");
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	(void) base; (void) w; (void) h;
	dlg_error("win_set_max_size not supported");
}

static void win_show(struct swa_window* base, bool show) {
	(void) base; (void) show;
	dlg_error("win_show not supported");
}

static void win_set_size(struct swa_window* base, unsigned w, unsigned h) {
	(void) base; (void) w; (void) h;
	dlg_error("win_set_size not supported");
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_kms* win = get_window_kms(base);

	// TODO: For vulkan this will be somewhat complicated. We probably
	// have to create our own, internal vulkan device i guess?
	// Combining vkdisplay with drm calls isn't something we want to start.
	// But we would also have to create pipelines to render the cursor
	// into the surface (created from the cursor plane)...
	// NOTE: when already using a gl render surface, we could use
	// gbm_bo's. Shouldn't really improve anything but may be useful
	// when the drm driver doesn't have the dumb buffer capability i guess?
	// are there drivers that implement gbm (with mapping, we don't want to
	// create a pipeline/surface/fbo/whatever just for that) but not dumb
	// buffers though? rather unlikely i guess
	if(win->surface_type != swa_surface_buffer &&
			win->surface_type != swa_surface_gl) {
		dlg_error("TODO: not implemented");
		return;
	}

	/*
	if(!win->output->cursor_plane.id) {
		dlg_error("No cursor plane");
		return;
	}
	*/

	enum swa_cursor_type type = cursor.type;
	if(type == swa_cursor_default) {
		type = swa_cursor_left_pointer;
	}

	struct swa_image cursor_image = {0};
	bool valid = false;
	if(type == swa_cursor_image) {
		cursor_image = cursor.image;
		valid = cursor_image.width > 0 && cursor_image.height > 0;

		win->cursor.buffer.hx = cursor.hx;
		win->cursor.buffer.hy = cursor.hy;
	} else if(type != swa_cursor_none) {
		if(!win->dpy->cursor_theme) {
			const char* theme = getenv("XCURSOR_THEME");
			const char* size_str = getenv("XCURSOR_SIZE");
			unsigned size = 32u;
			if(size_str) {
				long s = strtol(size_str, NULL, 10);
				if(s <= 0) {
					dlg_warn("Invalid XCURSOR_SIZE: %s", size_str);
				} else {
					size = s;
				}
			}

			// if XCURSOR_THEME is not set, we pass in null, which will result
			// in the default cursor theme being used.
			win->dpy->cursor_theme = swa_xcursor_theme_load(theme, size);
			if(!win->dpy->cursor_theme) {
				dlg_error("Could not load cursor theme");
				return;
			}
		}

		// TODO: support animated cursor. See wayland backend
		const char* const* names = swa_get_xcursor_names(type);
		if(!names) {
			dlg_warn("failed to convert cursor type %d to xcursor", type);
			return;
		}

		struct swa_xcursor* cursor = NULL;
		for(; *names; ++names) {
			cursor = swa_xcursor_theme_get_cursor(win->dpy->cursor_theme, *names);
			if(cursor) {
				break;
			} else {
				dlg_debug("failed to retrieve cursor %s", *names);
			}
		}

		if(!cursor) {
			dlg_warn("failed to get any cursor for cursor type %d", type);
			return;
		}

		struct swa_xcursor_image* img = cursor->images[0];
		cursor_image.width = img->width;
		cursor_image.height = img->height;
		cursor_image.stride = 4 * img->width;
		cursor_image.format = swa_image_format_bgra32;
		cursor_image.data = img->buffer;
		valid = true;

		win->cursor.buffer.hx = img->hotspot_x;
		win->cursor.buffer.hy = img->hotspot_y;
	}

	// create buffer if needed
	if(!valid) {
		finish_dumb_buffer(win->dpy, &win->cursor.buffer.buffer);
	} else if(!win->cursor.buffer.buffer.data) {
		int err;
		uint64_t w, h;
		err = drmGetCap(win->dpy->drm.fd, DRM_CAP_CURSOR_WIDTH, &w);
		dlg_assertlm(dlg_level_warn, !err, "%d (%s)", err, strerror(errno));
		w = err ? 64 : w;
		err = drmGetCap(win->dpy->drm.fd, DRM_CAP_CURSOR_HEIGHT, &h);
		dlg_assertlm(dlg_level_warn, !err, "%d (%s)", err, strerror(errno));
		h = err ? 64 : h;

		// TODO: we don't really need a drm framebuffer for this
		// buffer. Maybe add an additional function that doesn't
		// create one?
		if(!init_dumb_buffer(win->dpy, w, h, DRM_FORMAT_ARGB8888,
				&win->cursor.buffer.buffer)) {
			dlg_warn("failed to create cursor dumb buffer");
			return;
		}

		win->cursor.buffer.width = w;
		win->cursor.buffer.height = h;
	}

	if(cursor_image.width > win->cursor.buffer.width ||
			cursor_image.height > win->cursor.buffer.height) {
		dlg_error("cursor image too large");
		return;
	}

	// clear first (important for overflow)
	if(valid) {
		memset(win->cursor.buffer.buffer.data, 0x0, win->cursor.buffer.buffer.size);
		if(valid) {
			struct swa_image dst = {
				.width = cursor_image.width,
				.height = cursor_image.height,
				.stride = win->cursor.buffer.buffer.stride,
				.format = swa_image_format_bgra32,
				.data = win->cursor.buffer.buffer.data,
			};
			swa_convert_image(&cursor_image, &dst);
		}

		int err = drmModeSetCursor(win->dpy->drm.fd, win->output->crtc.id,
			win->cursor.buffer.buffer.gem_handle,
			win->cursor.buffer.width, win->cursor.buffer.height);
		dlg_assertm(!err, "drmModeSetCursor: %s", strerror(errno));
	}
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type == swa_surface_buffer) {
		if(!win->buffer.pending && !win->buffer.active) {
			if(win->base.listener->draw) {
				win->defer_events |=  swa_kms_defer_draw;
				pml_defer_enable(win->defer, true);
			}
		} else {
			win->redraw = true;
		}
	} else if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		if(!swa_kms_vk_surface_refresh(win->vk)) {
			win->defer_events |=  swa_kms_defer_draw;
			pml_defer_enable(win->defer, true);
		}
#else
		dlg_error("window has vk surface but swa was built without vulkan");
#endif
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		if(!win->gl.pending) {
			if(win->base.listener->draw) {
				win->defer_events |=  swa_kms_defer_draw;
				pml_defer_enable(win->defer, true);
			}
		} else {
			win->redraw = true;
		}
#else
		dlg_error("window has gl surface but swa was built without gl");
#endif
	} else {
		dlg_warn("can't refresh window without surface");
	}
}

static void win_surface_frame(struct swa_window* base) {
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		swa_kms_vk_surface_frame(win->vk);
#else
		dlg_error("window has vk surface but swa was built without vulkan");
#endif
	}

	// no-op otherwise
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	(void) base; (void) state;
	dlg_error("win_set_state not supported");
}

static void win_begin_move(struct swa_window* base) {
	(void) base;
	dlg_error("win_begin_move not supported");
}

static void win_begin_resize(struct swa_window* base, enum swa_edge edges) {
	(void) base; (void) edges;
	dlg_error("win_begin_resize not supported");
}

static void win_set_title(struct swa_window* base, const char* title) {
	(void) base; (void) title;
	dlg_error("win_set_title not supported");
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
	(void) base; (void) img;
	dlg_error("win_set_icon not supported");
}

static bool win_is_client_decorated(struct swa_window* base) {
	// it's neither client nor server decoration i guess
	return false;
}

static uint64_t win_get_vk_surface(struct swa_window* base) {
#ifdef SWA_WITH_VK
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type != swa_surface_vk) {
		dlg_warn("can't get vulkan surface from non-vulkan window");
		return 0;
	}

	return (uint64_t) swa_kms_vk_surface_get_surface(win->vk);
#else
	dlg_warn("swa was compiled without vulkan suport");
	return 0;
#endif
}

static bool win_gl_make_current(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);
	return eglMakeCurrent(win->dpy->egl->display, win->gl.surface,
		win->gl.surface, win->gl.context);
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

#ifdef SWA_WITH_GL
static void free_fb(struct gbm_bo *bo, void *data) {
	uint32_t id = (uintptr_t)data;
	if(id) {
		struct gbm_device *gbm = gbm_bo_get_device(bo);
		drmModeRmFB(gbm_device_get_fd(gbm), id);
	}
}

static uint32_t fb_for_bo(struct gbm_bo* bo, uint32_t drm_format) {
	uint32_t id = (uintptr_t) gbm_bo_get_user_data(bo);
	if(id) {
		return id;
	}

	struct gbm_device* gbm = gbm_bo_get_device(bo);
	int fd = gbm_device_get_fd(gbm);
	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);

	uint32_t handles[4] = {0};
	uint32_t strides[4] = {0};
	uint32_t offsets[4] = {0};
	for(int i = 0; i < gbm_bo_get_plane_count(bo); i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
	}

	int err = drmModeAddFB2(fd, width, height, drm_format, handles, strides,
		offsets, &id, 0);
	if(err) {
		dlg_error("drmModeAddFB2: %s", strerror(errno));
		return 0;
	}

	gbm_bo_set_user_data(bo, (void*)(uintptr_t)id, free_fb);
	return id;
}
#endif // SWA_WITH_GL

static bool pageflip(struct swa_window_kms* win, uint32_t fb_id,
		uint64_t width, uint64_t height) {
	drmModeAtomicReq* req = drmModeAtomicAlloc();
	struct atomic atom = {req, false};

	uint32_t plane_id = win->output->primary_plane.id;
	union drm_plane_props* pprops = &win->output->primary_plane.props;
	atomic_add(&atom, plane_id, pprops->crtc_id, win->output->crtc.id);
	atomic_add(&atom, plane_id, pprops->fb_id, fb_id);
	atomic_add(&atom, plane_id, pprops->src_x, 0);
	atomic_add(&atom, plane_id, pprops->src_y, 0);
	atomic_add(&atom, plane_id, pprops->src_w, width << 16);
	atomic_add(&atom, plane_id, pprops->src_h, height << 16);

	atomic_add(&atom, plane_id, pprops->crtc_x, 0);
	atomic_add(&atom, plane_id, pprops->crtc_y, 0);
	atomic_add(&atom, plane_id, pprops->crtc_w, width);
	atomic_add(&atom, plane_id, pprops->crtc_h, height);

	union drm_connector_props* conn_props = &win->output->connector.props;
	uint32_t conn_id = win->output->connector.id;
	atomic_add(&atom, conn_id, conn_props->crtc_id, win->output->crtc.id);

	union drm_crtc_props* crtc_props = &win->output->crtc.props;
	uint32_t crtc_id = win->output->crtc.id;
	atomic_add(&atom, crtc_id, crtc_props->mode_id, win->output->mode_id);
	atomic_add(&atom, crtc_id, crtc_props->active, 1);

	uint32_t flags = (DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT);
	if(win->output->needs_modeset) {
		win->output->needs_modeset = false;
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	if(atom.failed) {
		return false;
	}

	int err = drmModeAtomicCommit(win->dpy->drm.fd, req, flags, win->dpy);
	if(err != 0) {
		dlg_error("drmModeAtomicCommit: %s", strerror(errno));
	}

	drmModeAtomicFree(req);
	return err == 0;
}

static bool win_gl_swap_buffers(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Window doesn't have gl surface");
		return false;
	}

	dlg_assert(win->dpy->egl && win->dpy->egl->display);
	dlg_assert(win->gl.context && win->gl.surface);

	// This happens when swap_buffers is called before the previous
	// page flipping completes.
	// TODO: we probably want to allow this. Not sure if supported by
	// gbm_surface though. But this requires some
	// modifications, we e.g. have to track a list/array of pending
	// buffers.
	if(win->gl.pending) {
		dlg_error("Can't swap buffers before buffers were flipped");
		return false;
	}

	if(!eglSwapBuffers(win->dpy->egl->display, win->gl.surface)) {
		dlg_error("eglSwapBuffers: %d", eglGetError());
		return false;
	}

	win->gl.pending = gbm_surface_lock_front_buffer(win->gl.gbm_surface);

	uint32_t fb_id = fb_for_bo(win->gl.pending, DRM_FORMAT_ARGB8888);
	uint64_t width = win->output->mode.hdisplay;
	uint64_t height = win->output->mode.vdisplay;
	return pageflip(win, fb_id, width, height);

#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
	dlg_error("TODO: not implemented");
	return false;
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Cannot get buffer for non-buffer-surface window");
		return false;
	}

	if(win->buffer.active) {
		dlg_error("Cannot get buffer while there is still an active one");
		return false;
	}

	// TODO: we could fix this by using a dynamically growing set of
	// buffers instead of a fixed number of 3. This case happens
	// when a new drawing is started (i.e. this function called)
	// before the presentation of the old one is completed.
	if(win->buffer.pending) {
		dlg_error("Constant (non-timed) redrawing not supported, "
			"applying the previous buffer hasn't completed");
		return false;
	}

	for(unsigned i = 0u; i < 3u; ++i) {
		if(!win->buffer.buffers[i].in_use) {
			win->buffer.active = &win->buffer.buffers[i];
			break;
		}
	}

	if(!win->buffer.active) {
		dlg_error("Couldn't find unused buffer");
		return false;
	}

	img->width = win->output->mode.hdisplay;
	img->height = win->output->mode.vdisplay;
	// DRM_FORMAT_XRGB8888 but drm formats are little endian and
	// we want byte order.
	img->format = swa_image_format_bgrx32;
	img->stride = win->buffer.active->stride;
	img->data = win->buffer.active->data;

	return true;
}

static void win_apply_buffer(struct swa_window* base) {
	struct swa_window_kms* win = get_window_kms(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Cannot apply buffer for non-buffer-surface window");
		return;
	}

	if(!win->buffer.active) {
		dlg_error("Cannot apply buffer when there is still no active one");
		return;
	}

	uint64_t width = win->output->mode.hdisplay;
	uint64_t height = win->output->mode.vdisplay;
	if(pageflip(win, win->buffer.active->fb_id, width, height)) {
		dlg_assert(!win->buffer.pending);
		win->buffer.active->in_use = true;
		win->buffer.pending = win->buffer.active;
	}
	win->buffer.active = NULL;
}

static const struct swa_window_interface window_impl = {
	.destroy = win_destroy,
	.get_capabilities = win_get_capabilities,
	.set_min_size = win_set_min_size,
	.set_max_size = win_set_max_size,
	.show = win_show,
	.set_size = win_set_size,
	.refresh = win_refresh,
	.surface_frame = win_surface_frame,
	.set_state = win_set_state,
	.set_cursor = win_set_cursor,
	.begin_move = win_begin_move,
	.begin_resize = win_begin_resize,
	.set_title = win_set_title,
	.set_icon = win_set_icon,
	.is_client_decorated = win_is_client_decorated,
	.get_vk_surface = win_get_vk_surface,
	.gl_make_current = win_gl_make_current,
	.gl_swap_buffers = win_gl_swap_buffers,
	.gl_set_swap_interval = win_gl_set_swap_interval,
	.get_buffer = win_get_buffer,
	.apply_buffer = win_apply_buffer
};

// display
static void drm_finish(struct swa_display_kms* dpy) {
	// TODO: cleanup output data
	free(dpy->drm.outputs);
	for(unsigned i = 0u; i < dpy->drm.n_planes; ++i) {
		drmModeFreePlane(dpy->drm.planes[i]);
	}
	free(dpy->drm.planes);

	if(dpy->drm.res) drmModeFreeResources(dpy->drm.res);
	if(dpy->drm.io) pml_io_destroy(dpy->drm.io);
	if(dpy->drm.fd) close(dpy->drm.fd);
	memset(&dpy->drm, 0x0, sizeof(dpy->drm));
}

static void display_destroy(struct swa_display* base) {
	struct swa_display_kms* dpy = get_display_kms(base);

	if(dpy->session.tty_fd) {
		struct vt_mode mode = {
			.mode = VT_AUTO,
		};

		ioctl(dpy->session.tty_fd, KDSKBMODE, dpy->session.saved_kb_mode);
		ioctl(dpy->session.tty_fd, KDSETMODE, KD_TEXT);
		ioctl(dpy->session.tty_fd, VT_SETMODE, &mode);
	}


	if(dpy->wakeup_pipe_r) close(dpy->wakeup_pipe_r);
	if(dpy->wakeup_pipe_w) close(dpy->wakeup_pipe_w);
	if(dpy->wakeup_io) pml_io_destroy(dpy->wakeup_io);

	// TODO: cleanup libinput, udev stuff
	drm_finish(dpy);
	free(dpy);
}

static bool display_dispatch(struct swa_display* base, bool block) {
	struct swa_display_kms* dpy = get_display_kms(base);
	pml_iterate(dpy->pml, block);
	return !dpy->quit;
}

static void display_wakeup(struct swa_display* base) {
	struct swa_display_kms* dpy = get_display_kms(base);
	int err = write(dpy->wakeup_pipe_w, " ", 1);

	// if the pipe is full, the waiting thread will wake up and clear
	// it and it doesn't matter that our write call failed
	if(err < 0 && errno != EAGAIN) {
		dlg_warn("Writing to wakeup pipe failed: %s", strerror(errno));
	}
}

static enum swa_display_cap display_capabilities(struct swa_display* base) {
	struct swa_display_kms* dpy = get_display_kms(base);
	enum swa_display_cap caps =
#ifdef SWA_WITH_GL
		swa_display_cap_gl |
#endif
#ifdef SWA_WITH_VK
		swa_display_cap_vk |
#endif
		swa_display_cap_buffer_surface;

	if(dpy->input.keyboard.present) caps |= swa_display_cap_keyboard;
	if(dpy->input.pointer.present) caps |= swa_display_cap_mouse;
	if(dpy->input.touch.present) caps |= swa_display_cap_touch;
	return caps;
}

static const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
	(void) base;
#ifdef SWA_WITH_VK
	static const char* names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_DISPLAY_EXTENSION_NAME,
		// We only require that because ext_display_control depends on it
		VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME,
	};
	*count = sizeof(names) / sizeof(names[0]);
	return names;
#else
	dlg_warn("swa was compiled without vulkan suport");
	*count = 0;
	return NULL;
#endif
}

static bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	struct swa_display_kms* dpy = get_display_kms(base);
	if(!dpy->input.keyboard.present) {
		dlg_warn("display has no keyboard");
		return false;
	}

	const unsigned n_bits = 8 * sizeof(dpy->input.keyboard.key_states);
	if(key >= n_bits) {
		dlg_warn("keycode not tracked (too high)");
		return false;
	}

	unsigned idx = key / 64;
	unsigned bit = key % 64;
	return (dpy->input.keyboard.key_states[idx] & (1 << bit));
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_kms* dpy = get_display_kms(base);
	if(!dpy->input.keyboard.keymap) {
		dlg_warn("display has no keyboard");
		return NULL;
	}

	return swa_xkb_key_name_keymap(dpy->input.keyboard.keymap, key);
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_kms* dpy = get_display_kms(base);
	if(!dpy->input.keyboard.state) {
		dlg_warn("display has no keyboard");
		return swa_keyboard_mod_none;
	}

	return swa_xkb_modifiers_state(dpy->input.keyboard.state);
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_kms* dpy = get_display_kms(base);
	if(!dpy->input.keyboard.present) {
		dlg_error("no keyboard present");
		return NULL;
	}

	return &dpy->input.keyboard.focus->base;
}

static bool display_mouse_button_pressed(struct swa_display* base,
		enum swa_mouse_button button) {
	struct swa_display_kms* dpy = get_display_kms(base);
	if(!dpy->input.pointer.present) {
		dlg_warn("display has no mouse");
		return NULL;
	}

	if(button >= 64) {
		dlg_warn("mouse button code not tracked (too high)");
		return false;
	}

	return (dpy->input.pointer.button_states & (1 << button));
}

static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_kms* dpy = get_display_kms(base);
	dlg_assert(x && y);
	if(!dpy->input.pointer.present) {
		dlg_error("no pointer present");
		return;
	}

	*x = (int) dpy->input.pointer.x;
	*y = (int) dpy->input.pointer.y;
}

static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_kms* dpy = get_display_kms(base);
	if(!dpy->input.pointer.present) {
		dlg_error("no pointer present");
		return NULL;
	}

	return &dpy->input.pointer.over->base;
}
static struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	dlg_error("clipboard not supported");
	return NULL;
}
static bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source) {
	dlg_error("clipboard not supported");
	return false;
}
static bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	dlg_error("dnd not supported");
	return false;
}

static void win_get_size(struct swa_window_kms* win, unsigned* width,
		unsigned* height) {
	if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		swa_kms_vk_surface_get_size(win->vk, width, height);
#else // SWA_WITH_VK
		dlg_error("window has vulkan surface but swa was built without vulkan");
		return;
#endif // SWA_WITH_VK
	} else if(win->output) {
		*width = win->output->mode.hdisplay;
		*height = win->output->mode.vdisplay;
	} else {
		dlg_error("Invalid window: neither vulkan window nor bound to drm output");
		return;
	}
}

static void win_handle_deferred(struct pml_defer* defer) {
	struct swa_window_kms* win = pml_defer_get_data(defer);
	pml_defer_enable(defer, false);

	if(win->defer_events & swa_kms_defer_size) {
		win->defer_events &= ~swa_kms_defer_size;
		if(win->base.listener->resize) {
			unsigned width, height;
			win_get_size(win, &width, &height);
			win->base.listener->resize(&win->base, width, height);
		}
	}

	if(win->defer_events & swa_kms_defer_draw) {
		win->defer_events &= ~swa_kms_defer_draw;
		if(win->base.listener->draw) {
			win->base.listener->draw(&win->base);
		}
	}
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, unsigned crtc_id, void *data) {
	struct swa_display_kms* dpy = data;
	dlg_assert(dpy);
	dlg_assert(dpy->drm.fd);

	struct swa_kms_output* output = NULL;
	for(unsigned i = 0u; i < dpy->drm.n_outputs; ++i) {
		if(dpy->drm.outputs[i].crtc.id == crtc_id) {
			output = &dpy->drm.outputs[i];
			break;
		}
	}

	if(!output) {
		dlg_debug("[CRTC:%u] atomic completion for unknown CRTC", crtc_id);
		return;
	}

	// This might happen if the window is destroyed in between i guess
	if(!output->window) {
		dlg_debug("[CRTC:%u] atomic completion for windowless output", crtc_id);
		return;
	}

	// manage buffers
	struct swa_window_kms* win = output->window;
	if(win->surface_type == swa_surface_buffer) {
		dlg_assert(win->buffer.pending);
		dlg_assert(win->buffer.pending->in_use);
		if(win->buffer.last) {
			dlg_assert(win->buffer.last->in_use);
			win->buffer.last->in_use = false;
		}
		win->buffer.last = win->buffer.pending;
		win->buffer.pending = NULL;
	} else if(win->surface_type == swa_surface_gl) {
		dlg_assert(win->gl.pending);
#ifdef SWA_WITH_GL
		if(win->gl.front) {
			gbm_surface_release_buffer(win->gl.gbm_surface, win->gl.front);
		}
#endif // SWA_WITH_GL

		output->window->gl.front = output->window->gl.pending;
		output->window->gl.pending = NULL;
	}

	// redraw, if requested
	if(output->window->redraw) {
		output->window->redraw = false;
		struct swa_window* base = &output->window->base;
		if(base->listener->draw) {
			base->listener->draw(base);
		}
	}
}

static void drm_io(struct pml_io* io, unsigned revents) {
	struct swa_display_kms* dpy = pml_io_get_data(io);
	drmEventContext event = {
		.version = 3,
		.page_flip_handler2 = page_flip_handler,
	};

	errno = 0;
	int err = drmHandleEvent(dpy->drm.fd, &event);
	if(err != 0) {
		dlg_error("drmHandleEvent: %d (%s)", err, strerror(errno));
	}
}

static bool output_init(struct swa_display_kms* dpy,
		struct swa_kms_output* output, drmModeConnectorPtr connector) {
	bool success = false;

	// Find the encoder (a deprecated KMS object) for this connector
	if(connector->encoder_id == 0) {
		dlg_debug("[CONN:%" PRIu32 "]: no encoder", connector->connector_id);
		return NULL;
	}

	drmModeEncoderPtr encoder = NULL;
	for(int e = 0; e < dpy->drm.res->count_encoders; e++) {
		if(dpy->drm.res->encoders[e] == connector->encoder_id) {
			encoder = drmModeGetEncoder(dpy->drm.fd, dpy->drm.res->encoders[e]);
			break;
		}
	}

	assert(encoder);

	// TODO: use more sophisticated matching/crtc reassigning, see wlroots
	if(encoder->crtc_id == 0) {
		dlg_debug("[CONN:%" PRIu32 "]: no CRTC", connector->connector_id);
		goto out_encoder;
	}

	drmModeCrtcPtr crtc = NULL;
	for(int c = 0; c < dpy->drm.res->count_crtcs; c++) {
		if(dpy->drm.res->crtcs[c] == encoder->crtc_id) {
			crtc = drmModeGetCrtc(dpy->drm.fd, dpy->drm.res->crtcs[c]);
			break;
		}
	}
	assert(crtc);

	// Ensure the CRTC is active.
	if(crtc->buffer_id == 0) {
		dlg_debug("[CONN:%" PRIu32 "]: not active", connector->connector_id);
		goto out_crtc;
	}

	// The kernel doesn't directly tell us what it considers to be the
	// single primary plane for this CRTC (i.e. what would be updated
	// by drmModeSetCrtc), but if it's already active then we can cheat
	// by looking for something displaying the same framebuffer ID,
	// since that information is duplicated.
	for(unsigned p = 0; p < dpy->drm.n_planes; p++) {
		union drm_plane_props props = {0};
		uint32_t plane_id = dpy->drm.planes[p]->plane_id;
		if(!get_drm_plane_props(dpy->drm.fd, plane_id, &props)) {
			continue;
		}

		uint64_t type;
		if(!get_drm_prop(dpy->drm.fd, plane_id, props.type, &type)) {
			dlg_error("Couldn't get type of plane");
			continue;
		}

		dlg_debug("[PLANE: %" PRIu32 "] CRTC ID %" PRIu32 ", FB %" PRIu32 ", type %" PRIu64,
			dpy->drm.planes[p]->plane_id,
			dpy->drm.planes[p]->crtc_id,
			dpy->drm.planes[p]->fb_id,
			type);
		/*
		if(type == DRM_PLANE_TYPE_CURSOR && !output->cursor_plane.id) {
				dlg_debug("  used as cursor plane");
				output->cursor_plane.id = dpy->drm.planes[p]->plane_id;
				output->cursor_plane.props = props;
		} else */ if(type == DRM_PLANE_TYPE_PRIMARY && !output->primary_plane.id) {
			if(dpy->drm.planes[p]->crtc_id == crtc->crtc_id &&
					dpy->drm.planes[p]->fb_id == crtc->buffer_id) {
				dlg_debug("  used as primary plane");
				output->primary_plane.id = dpy->drm.planes[p]->plane_id;
				output->primary_plane.props = props;
			}
		}
	}

	if(!output->primary_plane.id) {
		dlg_error("Couldn't find a primary plane");
		goto out_crtc;
	}

	// DRM is supposed to provide a refresh interval, but often doesn't;
	// calculate our own in milliHz for higher precision anyway.
	uint64_t refresh = ((crtc->mode.clock * 1000000LL / crtc->mode.htotal) +
		   (crtc->mode.vtotal / 2)) / crtc->mode.vtotal;
	dlg_debug("[CRTC:%" PRIu32 ", CONN %" PRIu32 ", PLANE %" PRIu32 "]: "
		"active at %u x %u, %" PRIu64 " mHz",
		crtc->crtc_id, connector->connector_id, output->primary_plane.id,
	    crtc->width, crtc->height, refresh);

	output->crtc.id = crtc->crtc_id;
	output->connector.id = connector->connector_id;
	output->mode = crtc->mode;

	int ret = drmModeCreatePropertyBlob(dpy->drm.fd, &output->mode,
		sizeof(output->mode), &output->mode_id);
	if(ret != 0) {
		dlg_error("Unable to create property blob: %s", strerror(errno));
		goto out_crtc;
	}

	// Just reuse the CRTC's existing mode: requires it to already be
	// active. In order to use a different mode, we could look at the
	// mode list exposed in the connector, or construct a new DRM mode
	// from EDID.
	// output->mode = crtc->mode;
	// output->refresh_interval_nsec = millihz_to_nsec(refresh);
	// output->mode_blob_id = mode_blob_create(device, &output->mode);
	if(!get_drm_connector_props(dpy->drm.fd, output->connector.id,
				&output->connector.props)) {
		goto out_crtc;
	}
	if(!get_drm_crtc_props(dpy->drm.fd, output->crtc.id,
				&output->crtc.props)) {
		goto out_crtc;
	}

	success = true;

out_crtc:
	drmModeFreeCrtc(crtc);
out_encoder:
	drmModeFreeEncoder(encoder);
	return success;
}

static bool init_drm_dev(struct swa_display_kms* dpy, const char* filename) {
	dpy->drm.fd = open(filename, O_RDWR | O_CLOEXEC, 0);
	if(dpy->drm.fd < 0) {
		dlg_error("couldn't open %s: %s", filename, strerror(errno));
		goto error;
	}

	dpy->drm.io = pml_io_new(dpy->pml, dpy->drm.fd, POLLIN, drm_io);
	if(!dpy->drm.io) {
		goto error;
	}

	pml_io_set_data(dpy->drm.io, dpy);

	// In order to drive KMS, we need to be 'master'. This should already
	// have happened for us thanks to being root and the first client.
	// There can only be one master at a time, so this will fail if
	// (e.g.) trying to run this test whilst a graphical session is
	// already active on the current VT.
	drm_magic_t magic;
	if(drmGetMagic(dpy->drm.fd, &magic) != 0 ||
			drmAuthMagic(dpy->drm.fd, magic) != 0) {
		dlg_error("KMS device %s is not master", filename);
		goto error;
	}

	int err;
	err = drmSetClientCap(dpy->drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	err |= drmSetClientCap(dpy->drm.fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if(err != 0) {
		dlg_error("No support for universal planes or atomic");
		goto error;
	}

	// uint64_t cap;
	// err = drmGetCap(dpy->drm_fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
	// dpy->has_fb_mods = (err == 0 && cap != 0);
	// dlg_debug("device %s framebuffer modifiers",
	//     (dpy->has_fb_mods) ? "supports" : "does not support");

	// The two 'resource' properties describe the KMS capabilities for
	// this device.
	dpy->drm.res = drmModeGetResources(dpy->drm.fd);
	if(!dpy->drm.res) {
		dlg_error("couldn't get card resources for %s", filename);
		goto error;
	}

	drmModePlaneResPtr plane_res = drmModeGetPlaneResources(dpy->drm.fd);
	if(!plane_res) {
		dlg_error("device %s has no planes", filename);
		goto error;
	}

	if(dpy->drm.res->count_crtcs <= 0 || dpy->drm.res->count_connectors <= 0 ||
	    	dpy->drm.res->count_encoders <= 0 || plane_res->count_planes <= 0) {
		dlg_error("device %s is not a KMS device", filename);
		goto error;
	}

	dpy->drm.planes = calloc(plane_res->count_planes, sizeof(*dpy->drm.planes));
	dpy->drm.n_planes = plane_res->count_planes;
	for(unsigned int i = 0; i < plane_res->count_planes; i++) {
		dpy->drm.planes[i] = drmModeGetPlane(dpy->drm.fd, plane_res->planes[i]);
		dlg_assert(dpy->drm.planes[i]);
	}

	free(plane_res);
	dpy->drm.outputs = calloc(dpy->drm.res->count_connectors, sizeof(*dpy->drm.outputs));

	// Go through our connectors one by one and try to find a usable
	// output chain. The comments in output_create() describe how we
	// determine how to set up the output, and why we work backwards
	// from a connector.
	for(int i = 0; i < dpy->drm.res->count_connectors; i++) {
		drmModeConnectorPtr connector =
			drmModeGetConnector(dpy->drm.fd, dpy->drm.res->connectors[i]);
		struct swa_kms_output* output = &dpy->drm.outputs[dpy->drm.n_outputs];
		if(!output_init(dpy, output, connector)) {
			memset(output, 0x0, sizeof(*output));
			continue;
		}

		++dpy->drm.n_outputs;
	}

	if(dpy->drm.n_outputs == 0) {
		dlg_error("device %s has no active outputs", filename);
		goto error;
	}

	dlg_info("using device %s with %d outputs", filename, dpy->drm.n_outputs);
	return true;

error:
	drm_finish(dpy);
	return false;
}

static bool init_drm(struct swa_display_kms* dpy) {
	int n_devs = drmGetDevices2(0, NULL, 0);
	if(n_devs == 0) {
		dlg_error("no DRM devices available");
		return false;
	}

	drmDevicePtr* devs = calloc(n_devs, sizeof(*devs));
	n_devs = drmGetDevices2(0, devs, n_devs);
	dlg_info("%d DRM devices available", n_devs);

	for(int i = 0; i < n_devs; i++) {
		drmDevicePtr candidate = devs[i];

		if(!(candidate->available_nodes & (1 << DRM_NODE_PRIMARY))) {
			continue;
		}

		if(init_drm_dev(dpy, candidate->nodes[DRM_NODE_PRIMARY])) {
			break;
		}
	}

	drmFreeDevices(devs, n_devs);
	if(!dpy->drm.fd) {
		dlg_error("Couldn't find any suitable KMS device");
		return false;
	}

	return true;
}

// TODO: handle drmSetMaster equivalent for vulkan as well
// see vk_display wlroots branch. We have to re-set the saved
// mode.
// should we destroy the surface and use the
// surface_created and surface_destroyed events?
//
// TODO: send focus and mouse cross events
// TODO: restart drawing for vulkan as well
// TODO: when not active we should probably fail buffer_apply
//   and gl swap calls right? but we can't really do anything
//   about vulkan. Maybe use surface created/destroyed to make
//   sure it's not used? Or is it even a problem if used?
static void sigusr_handler(struct pml_io* io, unsigned revents) {
	struct swa_display_kms* dpy = pml_io_get_data(io);
	dlg_assert(pml_io_get_fd(io) == dpy->session.sigusrfd);
	dlg_debug("Received SIGUSR1");

	// clear input
	struct signalfd_siginfo signal_info;
	int len;
	errno = 0;
	len = read(dpy->session.sigusrfd, &signal_info, sizeof(signal_info));
	if(!(len == -1 && errno == EAGAIN) && len != sizeof(signal_info)) {
		dlg_warn("Reading from signalfd failed: %s (length %d)",
			strerror(errno), len);
	}

	if(dpy->session.active) {
		dlg_trace("releasing vt");
		dpy->session.active = false;

		if(dpy->drm.fd) {
			for(unsigned i = 0u; i < dpy->drm.n_outputs; ++i) {
				if(!dpy->drm.outputs[i].window) {
					continue;
				}

				struct swa_window* base = &dpy->drm.outputs[i].window->base;
				if(base->listener->focus) {
					base->listener->focus(base, false);
				}
			}

			// TODO: ipc to privileged process instead
			drmDropMaster(dpy->drm.fd);
		}

		ioctl(dpy->session.tty_fd, VT_RELDISP, 1);
	} else {
		dlg_trace("reacquiring vt");
		ioctl(dpy->session.tty_fd, VT_RELDISP, VT_ACKACQ);

		if(dpy->drm.fd) {
			// TODO: ipc to privileged process instead
			drmSetMaster(dpy->drm.fd);

			for(unsigned i = 0u; i < dpy->drm.n_outputs; ++i) {
				if(!dpy->drm.outputs[i].window) {
					continue;
				}

				dpy->drm.outputs[i].window->defer_events |= swa_kms_defer_draw;
				pml_defer_enable(dpy->drm.outputs[i].window->defer, true);
				struct swa_window* base = &dpy->drm.outputs[i].window->base;
				if(base->listener->focus) {
					base->listener->focus(base, true);
				}
			}
		}

		dpy->session.active = true;
	}
}

static void sigterm_handler(struct pml_io* io, unsigned revents) {
	struct swa_display_kms* dpy = pml_io_get_data(io);
	dlg_assert(pml_io_get_fd(io) == dpy->session.sigtermfd);
	dlg_info("Received SIGTERM");

	// clear input
	struct signalfd_siginfo signal_info;
	int len;
	errno = 0;
	len = read(dpy->session.sigtermfd, &signal_info, sizeof(signal_info));
	if(!(len == -1 && errno == EAGAIN) && len != sizeof(signal_info)) {
		dlg_warn("Reading from signalfd failed: %s (length %d)",
			strerror(errno), len);
	}

	dpy->quit = true;
}

static int vt_setup(struct swa_display_kms* dpy) {
	const char *tty_num_env = getenv("TTYNO");
	int tty_num = 0;
	char tty_dev[32];

	// If $TTYNO is set in the environment, then use that first.
	if(tty_num_env) {
		char *endptr = NULL;
		tty_num = strtoul(tty_num_env, &endptr, 10);
		if(tty_num == 0 || *endptr != '\0') {
			dlg_error("invalid $TTYNO environment variable");
			return -1;
		}
		snprintf(tty_dev, sizeof(tty_dev), "/dev/tty%d", tty_num);
		dlg_trace("tty num %d from env $TTYNO", tty_num);
	} else if(ttyname(STDIN_FILENO)) {
		// Otherwise, if we're running from a VT ourselves, just reuse that
		ttyname_r(STDIN_FILENO, tty_dev, sizeof(tty_dev));
	} else {
		int tty0;

		// Other-other-wise, look for a free VT we can use by querying /dev/tty0
		tty0 = open("/dev/tty0", O_WRONLY | O_CLOEXEC);
		if(tty0 < 0) {
			dlg_error("couldn't open /dev/tty0");
			return -errno;
		}

		if(ioctl(tty0, VT_OPENQRY, &tty_num) < 0 || tty_num < 0) {
			dlg_error("couldn't get free TTY");
			close(tty0);
			return -errno;
		}
		close(tty0);
		sprintf(tty_dev, "/dev/tty%d", tty_num);
		dlg_trace("free tty num %d from tty0", tty_num);
	}

	dpy->session.tty_fd = open(tty_dev, O_RDWR | O_NOCTTY);
	if(dpy->session.tty_fd < 0) {
		dlg_error("failed to open VT %d", tty_num);
		return -errno;
	}

	// If we get our VT from stdin, work painfully backwards to find
	// its VT number.
	if(tty_num == 0) {
		struct stat buf;

		if(fstat(dpy->session.tty_fd, &buf) == -1 ||
		    major(buf.st_rdev) != TTY_MAJOR) {
			dlg_error("VT file %s is bad", tty_dev);
			return -1;
		}

		tty_num = minor(buf.st_rdev);
		dlg_trace("tty num %d from stdin", tty_num);
	}
	assert(tty_num != 0);

	dlg_debug("using VT %d", tty_num);
	if(ioctl(dpy->session.tty_fd, VT_ACTIVATE, tty_num) != 0 ||
			ioctl(dpy->session.tty_fd, VT_WAITACTIVE, tty_num) != 0) {
		dlg_error("couldn't switch to VT %d", tty_num);
		return -errno;
	}

	dlg_debug("switched to VT %d", tty_num);

	// Completely disable kernel keyboard processing: this prevents us
	// from being killed on Ctrl-C.
	if(ioctl(dpy->session.tty_fd, KDGKBMODE, &dpy->session.saved_kb_mode) != 0 ||
	    	ioctl(dpy->session.tty_fd, KDSKBMODE, K_OFF) != 0) {
		dlg_error("failed to disable TTY keyboard processing");
		return -errno;
	}

	// Change the VT into graphics mode, so the kernel no longer prints
	// text out on top of us.
	if(ioctl(dpy->session.tty_fd, KDSETMODE, KD_GRAPHICS) != 0) {
		dlg_error("failed to switch TTY to graphics mode");
		return -errno;
	}

	dlg_debug("VT setup complete");

	// Register signals for terminal switching.
	// We will read them using a signalfd.
	struct vt_mode mode = {
		.mode = VT_PROCESS,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR1,
	};

	if(ioctl(dpy->session.tty_fd, VT_SETMODE, &mode) < 0) {
		dlg_error("Failed to take control of TTY");
		return -errno;
	}

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);

	// Block normal signal handling of our switching signal
	if(sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		dlg_error("Failed to block SIGUSR1 signal handling");
		return -errno;
	}

	dpy->session.sigusrfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	if(dpy->session.sigusrfd < 0) {
		dlg_error("signalfd failed");
		return -errno;
	}

	dpy->session.sigusrio = pml_io_new(dpy->pml, dpy->session.sigusrfd,
		POLLIN, sigusr_handler);
	if(!dpy->session.sigusrio) {
		return -1;
	}

	pml_io_set_data(dpy->session.sigusrio, dpy);

	// We also install a signal fd (and block normal
	// handling) of SIGTERM. This is a fallback that can be used
	// to terminate the process. Could additionally add SIGINT to
	// the mask?
	// TODO: make all of this optional! some processes may wish
	// to handle this signal on their own.
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);

	// Block normal signal handling
	if(sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		dlg_error("Failed to block SIGTERM signal handling");
		return -errno;
	}

	dpy->session.sigtermfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	if(dpy->session.sigtermfd < 0) {
		dlg_error("signalfd failed");
		return -errno;
	}

	dpy->session.sigtermio = pml_io_new(dpy->pml, dpy->session.sigtermfd,
		POLLIN, sigterm_handler);
	if(!dpy->session.sigtermio) {
		return -1;
	}

	pml_io_set_data(dpy->session.sigtermio, dpy);
	dpy->session.active = true;
	return 0;
}

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_kms* dpy = get_display_kms(base);

	struct swa_window_kms* win = calloc(1, sizeof(*win));
	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;

	if(!dpy->session.tty_fd) {
		if(vt_setup(dpy) != 0) {
			dlg_error("couldn't set up VT: %s", strerror(errno));
			goto error;
		}
	}

	win->surface_type = settings->surface;
	if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		// TODO: might be able to make this work
		// Proper multi-window not supported for vulkan anyways
		if(dpy->drm.fd) {
			dlg_error("Can't mix vulkan and non-vulkan windows on drm backend");
			goto error;
		}

		VkInstance instance = (VkInstance) settings->surface_settings.vk.instance;
		if(!(win->vk = swa_kms_vk_surface_create(dpy->pml, instance, &win->base))) {
			goto error;
		}

#else // SWA_WITH_VK
		dlg_error("swa was compiled without vulkan support");
		goto error;
#endif // SWA_WITH_VK
	} else {
		// TODO: fail creation (or fix it, if possible) if there is
		// an active vulkan window (for the gpu we use?). They might
		// interfer with each other.

		if(!dpy->drm.fd) {
			if(!init_drm(dpy)) {
				goto error;
			}
		}

		// Just create it on any free output.
		// If there aren't any remaning outputs, fail
		struct swa_kms_output* output = NULL;
		for(unsigned i = 0u; i < dpy->drm.n_outputs; ++i) {
			if(!dpy->drm.outputs[i].window) {
				output = &dpy->drm.outputs[i];
				break;
			}
		}

		if(!output) {
			dlg_error("Can't create window since there isn't a free output");
			goto error;
		}

		output->window = win;
		win->output = output;
		win->output->needs_modeset = true;

		unsigned width = output->mode.hdisplay;
		unsigned height = output->mode.vdisplay;
		if(win->surface_type == swa_surface_buffer) {
			for(unsigned i = 0u; i < 3u; ++i) {
				if(!init_dumb_buffer(dpy, width, height,
						DRM_FORMAT_XRGB8888, &win->buffer.buffers[i])) {
					goto error;
				}
			}
		} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
			if(!dpy->gbm_device) {
				errno = 0;
				dpy->gbm_device = gbm_create_device(dpy->drm.fd);
				if(!dpy->gbm_device) {
					dlg_error("Failed to create gbm device: %s", strerror(errno));
					goto error;
				}
			}

			if(!dpy->egl) {
				dpy->egl = swa_egl_display_create(EGL_PLATFORM_GBM_KHR,
					dpy->gbm_device);
				if(!dpy->egl) {
					goto error;
				}
			}

			uint32_t format = GBM_FORMAT_ARGB8888;
			uint32_t flags = GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT;
			win->gl.gbm_surface = gbm_surface_create(dpy->gbm_device,
				width, height, format, flags);
			if(!win->gl.gbm_surface) {
				dlg_error("Failed to create gbm surface: %s", strerror(errno));
				goto error;
			}

			const struct swa_gl_surface_settings* gls = &settings->surface_settings.gl;
			bool alpha = settings->transparent;
			EGLContext* ctx = &win->gl.context;
			EGLConfig egl_config;
			if(!swa_egl_init_context(dpy->egl, gls, alpha, &egl_config, ctx)) {
				goto error;
			}

			if(!(win->gl.surface = swa_egl_create_surface(dpy->egl,
					win->gl.gbm_surface, egl_config, gls->srgb))) {
				goto error;
			}
#else // SWA_WITH_GL
			dlg_error("swa was built without GL");
			goto error;
#endif
		}
	}

	win_set_cursor(&win->base, settings->cursor);

	// queue initial events
	// TODO: only defer size event when we don't use requested size
	//  fix used video mode
	win->defer_events = swa_kms_defer_draw | swa_kms_defer_size;
	win->defer = pml_defer_new(dpy->pml, win_handle_deferred);
	pml_defer_set_data(win->defer, win);

	// TODO: hacky! fix for multi monitor support
	if(dpy->input.pointer.present && !dpy->input.pointer.over) {
		dpy->input.pointer.over = win;
	}
	if(dpy->input.keyboard.present && !dpy->input.keyboard.focus) {
		dpy->input.keyboard.focus = win;
	}

	return &win->base;

error:
	win_destroy(&win->base);
	return NULL;
}

static const struct swa_display_interface display_impl = {
	.destroy = display_destroy,
	.dispatch = display_dispatch,
	.wakeup = display_wakeup,
	.capabilities = display_capabilities,
	.vk_extensions = display_vk_extensions,
	.key_pressed = display_key_pressed,
	.key_name = display_key_name,
	.active_keyboard_mods = display_active_keyboard_mods,
	.get_keyboard_focus = display_get_keyboard_focus,
	.mouse_button_pressed = display_mouse_button_pressed,
	.mouse_position = display_mouse_position,
	.get_mouse_over = display_get_mouse_over,
	.get_clipboard = display_get_clipboard,
	.set_clipboard = display_set_clipboard,
	.start_dnd = display_start_dnd,
	.create_window = display_create_window,
};

static void udev_io(struct pml_io* io, unsigned revents) {
	struct swa_display_kms* dpy = pml_io_get_data(io);
	struct udev_device *udev_dev = udev_monitor_receive_device(dpy->udev_monitor);
	if(!udev_dev) {
		return;
	}

	const char *action = udev_device_get_action(udev_dev);
	if(!action || strcmp(action, "change") != 0) {
		goto out;
	}

	// dev_t devnum = udev_device_get_devnum(udev_dev);

	// TODO: compare devnum to drm device and if they are the same,
	// reinitialize drm. This means we might have to destroy outputs
	// (send associated window close event and ignore/fail further
	// requests) and recreate new ones.

out:
	udev_device_unref(udev_dev);
}

static bool init_udev(struct swa_display_kms* dpy) {
	dpy->udev = udev_new();
	if(!dpy->udev) {
		dlg_error("Failed to init udev");
		return false;
	}

	dpy->udev_monitor = udev_monitor_new_from_netlink(dpy->udev, "udev");
	if(!dpy->udev_monitor) {
		dlg_error("Failed to create udev monitor");
		return false;
	}

	udev_monitor_filter_add_match_subsystem_devtype(dpy->udev_monitor, "drm", NULL);
	udev_monitor_enable_receiving(dpy->udev_monitor);

	int fd = udev_monitor_get_fd(dpy->udev_monitor);
	dpy->udev_io = pml_io_new(dpy->pml, fd, POLLIN, udev_io);
	if(!dpy->udev_io) {
		return false;
	}

	pml_io_set_data(dpy->udev_io, dpy);
	return true;
}

// TODO: session abstraction
static int libinput_open_restricted(const char *path,
		int flags, void *data) {
	int fd = open(path, flags);
	dlg_trace("libinput open %s %d -> %d", path, flags, fd);
	return fd;
}

static void libinput_close_restricted(int fd, void *data) {
	dlg_trace("libinput close %d", fd);
	close(fd);
}

static const struct libinput_interface libinput_impl = {
	.open_restricted = libinput_open_restricted,
	.close_restricted = libinput_close_restricted
};

static bool init_xkb(struct swa_display_kms* dpy) {
	struct xkb_rule_names rules = { 0 };
	struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	dpy->input.keyboard.keymap = xkb_map_new_from_names(context, &rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if(!dpy->input.keyboard.keymap) {
		dlg_error("Failed to create xkb keymap: %s", strerror(errno));
		return false;
	}
	dpy->input.keyboard.state = xkb_state_new(dpy->input.keyboard.keymap);
	if(!dpy->input.keyboard.state) {
		dlg_error("Failed to create xkb state: %s", strerror(errno));
		return false;
	}

	xkb_context_unref(context);
	return true;
}

static void handle_device_added(struct swa_display_kms* dpy,
		struct libinput_device* dev) {
	int vendor = libinput_device_get_id_vendor(dev);
	int product = libinput_device_get_id_product(dev);
	const char *name = libinput_device_get_name(dev);
	dlg_info("Added libinput device %s [%d:%d]", name, vendor, product);

	if(libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
		if(!dpy->input.keyboard.keymap) {
			if(init_xkb(dpy)) {
				dpy->input.keyboard.present = true;
			}
		}
	}
	if(libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
		dpy->input.pointer.present = true;
	}
	if(libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
		dpy->input.touch.present = true;
	}
}

static bool check_binding(struct swa_display_kms* dpy, xkb_keysym_t sym) {
	// TODO: probably the wrong place for this.
	// We should inhibit all input when not active...
	if(!dpy->session.active) {
		return false;
	}

	// switch vt
	if(sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
		int vt = 1 + sym - XKB_KEY_XF86Switch_VT_1;
		dlg_info("requested terminal switch to vt %d", vt);
		int err = ioctl(dpy->session.tty_fd, VT_ACTIVATE, vt);
		dlg_assertm(!err, "ioctl: %s", strerror(errno));
		return true;
	}

	return false;
}

static void handle_keyboard_key(struct swa_display_kms* dpy,
		struct libinput_event* event) {
	struct libinput_event_keyboard* kbevent = libinput_event_get_keyboard_event(event);
	uint32_t keycode = libinput_event_keyboard_get_key(kbevent);
	enum libinput_key_state state = libinput_event_keyboard_get_key_state(kbevent);

	uint32_t xkb_keycode = keycode + 8;
	const xkb_keysym_t* syms;
	int nsyms = xkb_state_key_get_syms(dpy->input.keyboard.state,
		xkb_keycode, &syms);

	bool pressed = false;
	switch(state) {
	case LIBINPUT_KEY_STATE_RELEASED:
		pressed = false;
		break;
	case LIBINPUT_KEY_STATE_PRESSED:
		pressed = true;
		break;
	}

	// check for internal keyboard binding handlers
	// mainly for switching vts
	if(pressed) {
		for(int i = 0; i < nsyms; ++i) {
			char buf[64];
			xkb_keysym_get_name(syms[i], buf, 64);
			dlg_info("keysym: %s", buf);

			if(check_binding(dpy, syms[i])) {
				return;
			}
		}
	}

	unsigned idx = keycode / 64;
	unsigned bit = keycode % 64;
	if(pressed) {
		dpy->input.keyboard.key_states[idx] |= (uint64_t)(1 << bit);
	} else {
		dpy->input.keyboard.key_states[idx] &= ~(uint64_t)(1 << bit);
	}

	xkb_state_update_key(dpy->input.keyboard.state, xkb_keycode,
		pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

	struct swa_window_kms* focus = dpy->input.keyboard.focus;
	if(focus && focus->base.listener->key) {
		char buf[8] = {0};
		const char* utf8 = NULL;
		if(nsyms && xkb_keysym_to_utf8(syms[0], buf, 8)) {
			utf8 = buf;
		}

		struct swa_key_event ev = {
			.keycode = keycode,
			.pressed = pressed,
			.utf8 = utf8,
			.repeated = false,
			.modifiers = swa_xkb_modifiers_state(dpy->input.keyboard.state),
		};
		focus->base.listener->key(&focus->base, &ev);
	}

	// TODO: manually trigger repeat events via a timer
}

static void update_cursor_position(struct swa_display_kms* dpy) {
	// TODO: fix for vulkan
	if(!dpy->input.pointer.over ||
			!dpy->input.pointer.over->output ||
			// !dpy->input.pointer.over->output->cursor_plane.id ||
			!dpy->input.pointer.over->cursor.buffer.buffer.fb_id) {
		return;
	}

	struct swa_window_kms* win = dpy->input.pointer.over;
	struct swa_kms_output* output = win->output;

	int32_t x = win->dpy->input.pointer.x - win->cursor.buffer.hx;
	int32_t y = win->dpy->input.pointer.y - win->cursor.buffer.hy;
	int err = drmModeMoveCursor(dpy->drm.fd, output->crtc.id, x, y);
	dlg_assertm(!err, "drmModeMoveCursor: %s", strerror(errno));
}

static void handle_pointer_motion(struct swa_display_kms* dpy,
		struct libinput_event* base_ev) {
	struct libinput_event_pointer* ev =
		libinput_event_get_pointer_event(base_ev);

	double dx = libinput_event_pointer_get_dx(ev);
	double dy = libinput_event_pointer_get_dy(ev);
	int ox = dpy->input.pointer.x;
	int oy = dpy->input.pointer.y;
	dpy->input.pointer.x += dx;
	dpy->input.pointer.y += dy;

	if(ox == (int) dpy->input.pointer.x && oy == (int) dpy->input.pointer.y) {
		return;
	}

	struct swa_window_kms* over = dpy->input.pointer.over;
	if(over && over->base.listener->mouse_move) {
		struct swa_mouse_move_event ev = {
			.x = (int) dpy->input.pointer.x,
			.y = (int) dpy->input.pointer.y,
			.dx = (int) dpy->input.pointer.x - ox,
			.dy = (int) dpy->input.pointer.y - oy,
		};
		over->base.listener->mouse_move(&over->base, &ev);
	}

	update_cursor_position(dpy);
}

static void handle_pointer_motion_abs(struct swa_display_kms* dpy,
		struct libinput_event* base_ev) {
	struct libinput_event_pointer* ev =
		libinput_event_get_pointer_event(base_ev);

	// TODO: real width/height of current output here?
	double x = libinput_event_pointer_get_absolute_x_transformed(ev, 1);
	double y = libinput_event_pointer_get_absolute_y_transformed(ev, 1);

	int ox = dpy->input.pointer.x;
	int oy = dpy->input.pointer.y;
	dpy->input.pointer.x = x;
	dpy->input.pointer.y = y;

	if(ox == (int) dpy->input.pointer.x && oy == (int) dpy->input.pointer.y) {
		return;
	}

	struct swa_window_kms* over = dpy->input.pointer.over;
	if(over && over->base.listener->mouse_move) {
		struct swa_mouse_move_event ev = {
			.x = (int) dpy->input.pointer.x,
			.y = (int) dpy->input.pointer.y,
			.dx = (int) dpy->input.pointer.x - ox,
			.dy = (int) dpy->input.pointer.y - oy,
		};
		over->base.listener->mouse_move(&over->base, &ev);
	}

	update_cursor_position(dpy);
}

static enum swa_mouse_button linux_to_button(uint32_t buttoncode) {
	switch(buttoncode) {
		case BTN_LEFT: return swa_mouse_button_left;
		case BTN_RIGHT: return swa_mouse_button_right;
		case BTN_MIDDLE: return swa_mouse_button_middle;
		case BTN_SIDE: return swa_mouse_button_custom1;
		case BTN_EXTRA: return swa_mouse_button_custom2;
		case BTN_FORWARD: return swa_mouse_button_custom3;
		case BTN_BACK: return swa_mouse_button_custom4;
		case BTN_TASK: return swa_mouse_button_custom5;
		default: return swa_mouse_button_none;
	}
}

static void handle_pointer_button(struct swa_display_kms* dpy,
		struct libinput_event* base_ev) {
	struct libinput_event_pointer* ev =
		libinput_event_get_pointer_event(base_ev);

	bool pressed = false;
	switch(libinput_event_pointer_get_button_state(ev)) {
	case LIBINPUT_BUTTON_STATE_PRESSED:
		pressed = true;
		break;
	case LIBINPUT_BUTTON_STATE_RELEASED:
		pressed = false;
		break;
	default:
		dlg_error("Invalid libinput pointer button state");
		return;
	}

	struct swa_window_kms* over = dpy->input.pointer.over;
	uint32_t linux_button = libinput_event_pointer_get_button(ev);
	enum swa_mouse_button button = linux_to_button(linux_button);

	if(pressed) {
		dpy->input.pointer.button_states |= (uint64_t)(1 << button);
	} else {
		dpy->input.pointer.button_states &= ~(uint64_t)(1 << button);
	}

	if(over && over->base.listener->mouse_button) {
		struct swa_mouse_button_event ev = {
			.x = (int) dpy->input.pointer.x,
			.y = (int) dpy->input.pointer.y,
			.button = button,
			.pressed = pressed,
		};
		over->base.listener->mouse_button(&over->base, &ev);
	}
}

static void handle_libinput_event(struct swa_display_kms* dpy,
		struct libinput_event* event) {
	struct libinput_device* libinput_dev = libinput_event_get_device(event);
	enum libinput_event_type event_type = libinput_event_get_type(event);
	switch (event_type) {
	case LIBINPUT_EVENT_DEVICE_ADDED:
		handle_device_added(dpy, libinput_dev);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		// handle_device_removed(dpy, libinput_dev);
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		handle_keyboard_key(dpy, event);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		handle_pointer_motion(dpy, event);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		handle_pointer_motion_abs(dpy, event);
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		handle_pointer_button(dpy, event);
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		// handle_pointer_axis(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		// handle_touch_down(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		// handle_touch_up(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		// handle_touch_motion(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		// handle_touch_cancel(event, libinput_dev);
		break;
	default:
		break;
	}
}

static void libinput_io(struct pml_io* io, unsigned revents) {
	struct swa_display_kms* dpy = pml_io_get_data(io);
	if(libinput_dispatch(dpy->input.context) != 0) {
		dlg_error("Failed to dispatch libinput");
		return;
	}

	struct libinput_event* event;
	while((event = libinput_get_event(dpy->input.context))) {
		handle_libinput_event(dpy, event);
		libinput_event_destroy(event);
	}
}

static void log_libinput(struct libinput *libinput_context,
		enum libinput_log_priority priority, const char *fmt, va_list args) {
	char buf[256];
	int count = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	if(count < 0) {
		dlg_error("libinput log: invalid format");
		return;
	}

	buf[count] = '\0';

	// strip newline
	if(buf[count - 1] == '\n') {
		buf[count - 1] = '\0';
	}

	enum dlg_level lvl = dlg_level_error;
	switch(priority) {
		case LIBINPUT_LOG_PRIORITY_DEBUG:
			lvl = dlg_level_debug;
			break;
		case LIBINPUT_LOG_PRIORITY_INFO:
			lvl = dlg_level_info;
			break;
		case LIBINPUT_LOG_PRIORITY_ERROR:
			lvl = dlg_level_error;
			break;
		default:
			return;
	}

	dlg_log(lvl, "libinput: %s", buf);
}

static bool init_libinput(struct swa_display_kms* dpy) {
	dpy->input.context = libinput_udev_create_context(&libinput_impl,
		dpy, dpy->udev);
	if(!dpy->input.context) {
		dlg_error("Failed to create libinput context");
		return false;
	}

	// TODO: the seat associated with the used gpu can be found via udev.
	const char* seat_name = "seat0";
	if(libinput_udev_assign_seat(dpy->input.context, seat_name) != 0) {
		dlg_error("Failed to assign libinput seat");
		return false;
	}

	libinput_log_set_handler(dpy->input.context, log_libinput);
	libinput_log_set_priority(dpy->input.context, LIBINPUT_LOG_PRIORITY_DEBUG);

	int fd = libinput_get_fd(dpy->input.context);
	dpy->input.io = pml_io_new(dpy->pml, fd, POLLIN, libinput_io);
	if(!dpy->input.io) {
		return false;
	}

	pml_io_set_data(dpy->input.io, dpy);

	// Dispatch all present events.
	// Important to get initial devices
	dlg_trace("Reading initial libinput events");
	libinput_io(dpy->input.io, POLLIN);
	dlg_debug("keyboard: %d, pointer: %d, touch: %d",
		dpy->input.keyboard.present,
		dpy->input.pointer.present,
		dpy->input.touch.present);

	return true;
}

static void clear_wakeup(struct pml_io* io, unsigned revents) {
	char buf[128];
	int ret;
	int size = sizeof(buf);

	int fd = pml_io_get_fd(io);
	while((ret = read(fd, buf, size)) == size);

	if(ret < 0) {
		dlg_warn("Reading from wakeup pipe failed: %s", strerror(errno));
	}
}

// TODO: we somehow have to make sure that display creation
// fails when we wouldn't have enough rights.
// Not sure how to test that though.
struct swa_display* swa_display_kms_create(const char* appname) {
	(void) appname;

	struct swa_display_kms* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;
	dpy->pml = pml_new();

	// create wakeup pipes
	int fds[2];
	if(!swa_pipe(fds)) {
		goto error;
	}

	dpy->wakeup_pipe_r = fds[0];
	dpy->wakeup_pipe_w = fds[1];
	dpy->wakeup_io = pml_io_new(dpy->pml, dpy->wakeup_pipe_r,
		POLLIN, clear_wakeup);
	pml_io_set_data(dpy->wakeup_io, dpy);

	// We defer all drm stuff until windows are created since for
	// vulkan windows, we use the VkDisplayKHR api instead of drm
	// directly and for gl/buffer surfaces we use drm.

	if(!init_udev(dpy)) {
		goto error;
	}
	if(!init_libinput(dpy)) {
		goto error;
	}

	return &dpy->base;

error:
	display_destroy(&dpy->base);
	return NULL;
}
