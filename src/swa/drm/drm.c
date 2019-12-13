#define _POSIX_C_SOURCE 200809L

#include "drm.h"
#include "props.h"
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

#ifdef SWA_WITH_VK
  #include "vulkan.h"
#endif

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;

static struct drm_display* get_display_drm(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct drm_display*) base;
}

static struct drm_window* get_window_drm(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct drm_window*) base;
}

// window
static void win_destroy(struct swa_window* base) {
	struct drm_window* win = get_window_drm(base);
	if(win->output) win->output->window = NULL;
	if(win->dpy->input.pointer.over == win) {
		win->dpy->input.pointer.over = NULL;
	}
	if(win->dpy->input.keyboard.focus == win) {
		win->dpy->input.keyboard.focus = NULL;
	}

	// TODO
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
	// TODO: implement support for cursor via cursor planes
	(void) base; (void) cursor;
	dlg_error("win_set_cursor not supported");
}

static void win_refresh(struct swa_window* base) {
	struct drm_window* win = get_window_drm(base);
	if(win->surface_type == swa_surface_buffer) {
		if(!win->buffer.pending && !win->buffer.active) {
			if(win->base.listener->draw) {
				pml_defer_enable(win->defer_draw, true);
			}
		} else {
			win->redraw = true;
		}
	} else {
		// TODO
		dlg_info("TODO: unimplemented");
	}
}

static void win_surface_frame(struct swa_window* base) {
	(void) base;
	// no-op
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
	struct drm_window* win = get_window_drm(base);
	if(win->surface_type != swa_surface_vk) {
		dlg_warn("can't get vulkan surface from non-vulkan window");
		return 0;
	}

	return (uint64_t) win->vk->surface;
#else
	dlg_warn("swa was compiled without vulkan suport");
	return 0;
#endif
}

static bool win_gl_make_current(struct swa_window* base) {
	dlg_error("TODO: not implemented");
	return false;
}

static bool win_gl_swap_buffers(struct swa_window* base) {
	dlg_error("TODO: not implemented");
	return false;
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
	dlg_error("TODO: not implemented");
	return false;
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	struct drm_window* win = get_window_drm(base);
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

static void win_apply_buffer(struct swa_window* base) {
	struct drm_window* win = get_window_drm(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Cannot apply buffer for non-buffer-surface window");
		return;
	}

	if(!win->buffer.active) {
		dlg_error("Cannot apply buffer when there is still no active one");
		return;
	}

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	uint32_t plane_id = win->output->primary_plane_id;
	union drm_plane_props* pprops = &win->output->props.plane;

	uint32_t width = win->output->mode.hdisplay;
	uint32_t height = win->output->mode.vdisplay;

	struct atomic atom = {req, false};
	atomic_add(&atom, plane_id, pprops->crtc_id, win->output->crtc_id);
	atomic_add(&atom, plane_id, pprops->fb_id, win->buffer.active->fb_id);
	atomic_add(&atom, plane_id, pprops->src_x, 0);
	atomic_add(&atom, plane_id, pprops->src_y, 0);
	atomic_add(&atom, plane_id, pprops->src_w, width << 16);
	atomic_add(&atom, plane_id, pprops->src_h, height << 16);

	atomic_add(&atom, plane_id, pprops->crtc_x, 0);
	atomic_add(&atom, plane_id, pprops->crtc_y, 0);
	atomic_add(&atom, plane_id, pprops->crtc_w, width);
	atomic_add(&atom, plane_id, pprops->crtc_h, height);

	union drm_connector_props* conn_props = &win->output->props.connector;
	uint32_t conn_id = win->output->connector_id;
	atomic_add(&atom, conn_id, conn_props->crtc_id, win->output->crtc_id);

	union drm_crtc_props* crtc_props = &win->output->props.crtc;
	uint32_t crtc_id = win->output->crtc_id;
	atomic_add(&atom, crtc_id, crtc_props->mode_id, win->output->mode_id);
	atomic_add(&atom, crtc_id, crtc_props->active, 1);

	uint32_t flags = (DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT);
	if(win->output->needs_modeset) {
		win->output->needs_modeset = false;
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	int ret = drmModeAtomicCommit(win->dpy->drm.fd, req, flags, win->dpy);
	if(ret != 0) {
		dlg_error("drmModeAtomicCommit: %s", strerror(errno));
	} else {
		dlg_assert(!win->buffer.pending);
		win->buffer.active->in_use = true;
		win->buffer.pending = win->buffer.active;
	}

	win->buffer.active = NULL;
	drmModeAtomicFree(req);
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
static void drm_finish(struct drm_display* dpy) {
	// TODO: cleanup output data
	free(dpy->drm.outputs);
	for(unsigned i = 0u; i < dpy->drm.n_planes; i++) {
		drmModeFreePlane(dpy->drm.planes[i]);
	}
	free(dpy->drm.planes);

	if(dpy->drm.res) drmModeFreeResources(dpy->drm.res);
	if(dpy->drm.io) pml_io_destroy(dpy->drm.io);
	if(dpy->drm.fd) close(dpy->drm.fd);
	memset(&dpy->drm, 0x0, sizeof(dpy->drm));
}

static void display_destroy(struct swa_display* base) {
	struct drm_display* dpy = get_display_drm(base);

	if(dpy->session.tty_fd) {
		struct vt_mode mode = {
			.mode = VT_AUTO,
		};

		ioctl(dpy->session.tty_fd, KDSKBMODE, dpy->session.saved_kb_mode);
		ioctl(dpy->session.tty_fd, KDSETMODE, KD_TEXT);
		ioctl(dpy->session.tty_fd, VT_SETMODE, &mode);
	}

	// TODO: cleanup libinput, udev stuff
	drm_finish(dpy);
	free(dpy);
}

static bool display_dispatch(struct swa_display* base, bool block) {
	struct drm_display* dpy = get_display_drm(base);
	pml_iterate(dpy->pml, block);
	return true;
}

static void display_wakeup(struct swa_display* base) {
	// TODO: use eventfd
	dlg_error("TODO: unimplemented");
}

static enum swa_display_cap display_capabilities(struct swa_display* base) {
	// TODO: implement gl support
	struct drm_display* dpy = get_display_drm(base);
	enum swa_display_cap caps =
#ifdef SWA_WITH_GL
		// swa_display_cap_gl |
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
	};
	*count = sizeof(names) / sizeof(names[0]);
	return names;
#else
	dlg_warn("swa was compiled without vulkan suport");
	*count = 0;
	return NULL;
#endif
}

// TODO
static bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	// struct drm_display* dpy = get_display_drm(base);
	return false;
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	// struct drm_display* dpy = get_display_drm(base);
	return NULL;
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	// struct drm_display* dpy = get_display_drm(base);
	return swa_keyboard_mod_none;
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	// struct drm_display* dpy = get_display_drm(base);
	return NULL;
}

static bool display_mouse_button_pressed(struct swa_display* base, enum swa_mouse_button button) {
	// struct drm_display* dpy = get_display_drm(base);
	return false;
}
static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	// struct drm_display* dpy = get_display_drm(base);
}
static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	// struct drm_display* dpy = get_display_drm(base);
	return NULL;
}
static struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	// struct drm_display* dpy = get_display_drm(base);
	return NULL;
}
static bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source) {
	// struct drm_display* dpy = get_display_drm(base);
	return false;
}
static bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	// struct drm_display* dpy = get_display_drm(base);
	return false;
}

static bool init_dumb_buffer(struct drm_display* dpy,
		struct drm_output* output, struct drm_dumb_buffer* buf) {
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
		.width = output->mode.hdisplay,
		.height = output->mode.vdisplay,
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
		goto err_dumb;
	}

	buf->data = mmap(NULL, create.size, PROT_WRITE, MAP_SHARED,
		dpy->drm.fd, map.offset);
	if(buf->data == MAP_FAILED) {
		dlg_error("failed to mmap %u x %u dumb buffer: %s",
			create.width, create.height, strerror(errno));
		goto err_dumb;
	}

	buf->size = create.size;

	// create framebuffer
	// TODO: use modifier api
	uint32_t pitches[4] = {buf->stride, 0, 0, 0};
	uint32_t gem_handles[4] = {buf->gem_handle, 0, 0, 0};
	uint32_t offsets[4] = {0, 0, 0, 0};
	err = drmModeAddFB2(dpy->drm.fd, create.width, create.height,
		DRM_FORMAT_XRGB8888, gem_handles, pitches,
		offsets, &buf->fb_id, 0);

	if(err != 0 || buf->fb_id == 0) {
		dlg_error("AddFB2 failed: %s", strerror(errno));
		goto err_dumb;
	}

	return true;

err_dumb:;
	struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
	drmIoctl(dpy->drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
err:
	return false;
}

static void win_send_draw(struct pml_defer* defer) {
	struct drm_window* win = pml_defer_get_data(defer);
	pml_defer_enable(defer, false);
	if(win->base.listener->draw) {
		win->base.listener->draw(&win->base);
	}
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, unsigned crtc_id, void *data) {
	struct drm_display* dpy = data;
	dlg_assert(dpy);
	dlg_assert(dpy->drm.fd);

	struct drm_output* output = NULL;
	for(unsigned i = 0u; i < dpy->drm.n_outputs; ++i) {
		if(dpy->drm.outputs[i].crtc_id == crtc_id) {
			output = &dpy->drm.outputs[i];
			break;
		}
	}

	if(!output) {
		dlg_debug("[CRTC:%u] atomic completion for unknown CRTC", crtc_id);
		return;
	}

	if(!output->window) {
		dlg_debug("[CRTC:%u] atomic completion for windowless output", crtc_id);
		return;
	}

	// manage buffers
	dlg_assert(output->window->buffer.pending);
	dlg_assert(output->window->buffer.pending->in_use);
	if(output->window->buffer.last) {
		dlg_assert(output->window->buffer.last->in_use);
		output->window->buffer.last->in_use = false;
	}
	output->window->buffer.last = output->window->buffer.pending;
	output->window->buffer.pending = NULL;

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
	struct drm_display* dpy = pml_io_get_data(io);
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

static bool output_init(struct drm_display* dpy,struct drm_output* output,
		drmModeConnectorPtr connector) {
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
	drmModePlanePtr plane = NULL;
	for(unsigned p = 0; p < dpy->drm.n_planes; p++) {
		dlg_debug("[PLANE: %" PRIu32 "] CRTC ID %" PRIu32 ", FB %" PRIu32,
			dpy->drm.planes[p]->plane_id,
			dpy->drm.planes[p]->crtc_id,
			dpy->drm.planes[p]->fb_id);
		if(dpy->drm.planes[p]->crtc_id == crtc->crtc_id &&
		    	dpy->drm.planes[p]->fb_id == crtc->buffer_id) {
			plane = dpy->drm.planes[p];
			break;
		}
	}
	assert(plane);

	// DRM is supposed to provide a refresh interval, but often doesn't;
	// calculate our own in milliHz for higher precision anyway.
	uint64_t refresh = ((crtc->mode.clock * 1000000LL / crtc->mode.htotal) +
		   (crtc->mode.vtotal / 2)) / crtc->mode.vtotal;
	dlg_debug("[CRTC:%" PRIu32 ", CONN %" PRIu32 ", PLANE %" PRIu32 "]: "
		"active at %u x %u, %" PRIu64 " mHz",
		crtc->crtc_id, connector->connector_id, plane->plane_id,
	    crtc->width, crtc->height, refresh);

	output->primary_plane_id = plane->plane_id;
	output->crtc_id = crtc->crtc_id;
	output->connector_id = connector->connector_id;
	output->mode = crtc->mode;

	int ret = drmModeCreatePropertyBlob(dpy->drm.fd, &output->mode,
		sizeof(output->mode), &output->mode_id);
	if(ret != 0) {
		dlg_error("Unable to create property blob: %s", strerror(errno));
		goto out_plane;
	}

	// Just reuse the CRTC's existing mode: requires it to already be
	// active. In order to use a different mode, we could look at the
	// mode list exposed in the connector, or construct a new DRM mode
	// from EDID.
	// output->mode = crtc->mode;
	// output->refresh_interval_nsec = millihz_to_nsec(refresh);
	// output->mode_blob_id = mode_blob_create(device, &output->mode);
	if(!get_drm_connector_props(dpy->drm.fd, output->connector_id,
				&output->props.connector)) {
		goto out_plane;
	}
	if(!get_drm_plane_props(dpy->drm.fd, output->primary_plane_id,
				&output->props.plane)) {
		goto out_plane;
	}
	if(!get_drm_crtc_props(dpy->drm.fd, output->crtc_id,
				&output->props.crtc)) {
		goto out_plane;
	}

	success = true;

out_plane:
	drmModeFreePlane(plane);
out_crtc:
	drmModeFreeCrtc(crtc);
out_encoder:
	drmModeFreeEncoder(encoder);
	return success;
}

static bool init_drm_dev(struct drm_display* dpy, const char* filename) {
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
		struct drm_output* output = &dpy->drm.outputs[dpy->drm.n_outputs];
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

static bool init_drm(struct drm_display* dpy) {
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

// TODO: handle it for vulkan as well
// we have to set the mode again somehow? see wlroots vk_display backend
static void sigusr_handler(struct pml_io* io, unsigned revents) {
	struct drm_display* dpy = pml_io_get_data(io);
	if(dpy->session.active) {
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
		ioctl(dpy->session.tty_fd, VT_RELDISP, VT_ACKACQ);

		if(dpy->drm.fd) {
			// TODO: ipc to privileged process instead
			drmSetMaster(dpy->drm.fd);

			for(unsigned i = 0u; i < dpy->drm.n_outputs; ++i) {
				if(!dpy->drm.outputs[i].window) {
					continue;
				}

				struct swa_window* base = &dpy->drm.outputs[i].window->base;
				if(base->listener->focus) {
					base->listener->focus(base, true);
				}
			}
		}

		dpy->session.active = true;
	}
}

static int vt_setup(struct drm_display* dpy) {
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
	} else if(ttyname(STDIN_FILENO)) {
		// Otherwise, if we're running from a VT ourselves, just reuse that
		ttyname_r(STDIN_FILENO, tty_dev, sizeof(tty_dev));
	} else {
		int tty0;

		//Other-other-wise, look for a free VT we can use by querying /dev/tty0
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

	// Block normal signal handling our switching signal
	if(sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		dlg_error("Failed to block SIGUSR1 signal handling");
		return -errno;
	}

	dpy->session.sigusrfd = signalfd(-1, &mask, 0);
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
	return 0;
}

static struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct drm_display* dpy = get_display_drm(base);

	struct drm_window* win = calloc(1, sizeof(*win));
	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;

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
		if(!(win->vk = drm_vk_surface_create(instance))) {
			goto error;
		}

#else // SWA_WITH_VK
		dlg_error("swa was compiled without vulkan support");
		goto error;
#endif // SWA_WITH_VK
	} else {
		if(!dpy->drm.fd) {
			if(!init_drm(dpy)) {
				goto error;
			}
		}

		// Just create it on any free output.
		// If there aren't any remaning outputs, fail
		struct drm_output* output = NULL;
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

		if(win->surface_type == swa_surface_buffer) {
			for(unsigned i = 0u; i < 3u; ++i) {
				if(!init_dumb_buffer(dpy, output, &win->buffer.buffers[i])) {
					goto error;
				}
			}
		} else if(win->surface_type == swa_surface_gl) {
			// TODO
			dlg_error("TODO: Not implemented");
			goto error;
		}
	}

	if(!dpy->session.tty_fd) {
		if(vt_setup(dpy) != 0) {
			dlg_error("couldn't set up VT: %s", strerror(errno));
			goto error;
		}
	}

	// queue initial events
	win->defer_draw = pml_defer_new(dpy->pml, win_send_draw);
	pml_defer_set_data(win->defer_draw, win);

	// TODO: somewhat hacky
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
	struct drm_display* dpy = pml_io_get_data(io);
	struct udev_device *udev_dev = udev_monitor_receive_device(dpy->udev_monitor);
	if(!udev_dev) {
		return;
	}

	const char *action = udev_device_get_action(udev_dev);
	if(!action || strcmp(action, "change") != 0) {
		goto out;
	}

	dev_t devnum = udev_device_get_devnum(udev_dev);
	struct wlr_device* dev;

	// TODO: compare devnum to drm device and if they are the same,
	// reinitialize drm. This means we might have to destroy outputs
	// (send associated window close event and ignore/fail further
	// requests) and recreate new ones.

out:
	udev_device_unref(udev_dev);
}

static bool init_udev(struct drm_display* dpy) {
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

static void handle_device_added(struct drm_display* dpy,
		struct libinput_device* dev) {
	int vendor = libinput_device_get_id_vendor(dev);
	int product = libinput_device_get_id_product(dev);
	const char *name = libinput_device_get_name(dev);
	dlg_info("Added libinput device %s [%d:%d]", name, vendor, product);

	if(libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
		dpy->input.keyboard.present = true;
	}
	if(libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
		dpy->input.pointer.present = true;
	}
	if(libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
		dpy->input.touch.present = true;
	}
}

static void handle_keyboard_key(struct drm_display* dpy,
		struct libinput_event* event) {
	struct libinput_event_keyboard* kbevent = libinput_event_get_keyboard_event(event);
	uint32_t keycode = libinput_event_keyboard_get_key(kbevent);
	enum libinput_key_state state = libinput_event_keyboard_get_key_state(kbevent);
	bool pressed = false;

	switch(state) {
	case LIBINPUT_KEY_STATE_RELEASED:
		pressed = true;
		break;
	case LIBINPUT_KEY_STATE_PRESSED:
		pressed = false;
		break;
	}

	struct drm_window* focus = dpy->input.keyboard.focus;
	if(focus && focus->base.listener->key) {
		struct swa_key_event ev = {
			.keycode = keycode,
			.pressed = pressed,
			.utf8 = "",
			.repeated = false,
			.modifiers = swa_keyboard_mod_none,
		};
		focus->base.listener->key(&focus->base, &ev);
	}
}

static void handle_libinput_event(struct drm_display* dpy,
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
		// handle_pointer_motion(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		// handle_pointer_motion_abs(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		// handle_pointer_button(event, libinput_dev);
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
	struct drm_display* dpy = pml_io_get_data(io);
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

static bool init_libinput(struct drm_display* dpy) {
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

struct swa_display* drm_display_create(const char* appname) {
	(void) appname;

	struct drm_display* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;
	dpy->pml = pml_new();

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
