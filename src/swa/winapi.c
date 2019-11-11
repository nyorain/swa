#include <swa/winapi.h>

#define UNICODE
#include <windows.h>
#include <winuser.h>
#include <windowsx.h>

// undefine the shittiest macros
// holy fuck microsoft...
#undef near
#undef far
#undef ERROR
#undef MemoryBarrier
#undef UNICODE

#ifdef SWA_WITH_VK
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#endif

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;

// utility
static struct swa_display_win* get_display_win(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_win*) base;
}

static struct swa_window_win* get_window_win(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_win*) base;
}

// display api
void display_destroy(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
    free(dpy);
}

static bool dispatch_one(void) {
	MSG msg;
	if(!PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
		return false;
	}

	TranslateMessage(&msg);
	DispatchMessage(&msg);
	return true;
}

bool display_dispatch(struct swa_display* base, bool block) {
	struct swa_display_win* dpy = get_display_win(base);
    if(dpy->error) {
        return false;
    }

    // wait for first message if we are allowed to block
    if(block) {
        MSG msg;
        int ret = GetMessage(&msg, NULL, 0, 0);
        if(ret == -1) {
            // winapi documentation suggests that errors here are
            // of critical nature and exiting the application is the
            // usual strategy.
            wchar_t* buffer;
            int code = GetLastError();
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL,
                code, 0, (wchar_t*) &buffer, 0, NULL);
            dlg_error("GetMessage: ", buffer);
            LocalFree(buffer);
            dpy->error = true;
            return false;
        } else {
		    TranslateMessage(&msg);
		    DispatchMessage(&msg);
        }
    }

    // dispatch all messages
    while(dispatch_one());
    return true;
}

void display_wakeup(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
}

enum swa_display_cap display_capabilities(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
	enum swa_display_cap caps =
#ifdef SWA_WITH_GL
		swa_display_cap_gl |
#endif
#ifdef SWA_WITH_VK
		swa_display_cap_vk |
#endif
		swa_display_cap_client_decoration |
		swa_display_cap_keyboard |
		swa_display_cap_mouse |
		swa_display_cap_touch |
        // TODO: implement data exchange
		// swa_display_cap_dnd |
		// swa_display_cap_clipboard |
		swa_display_cap_buffer_surface;
	return caps;
}

const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
#ifdef SWA_WITH_VK
	static const char* names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};
	*count = sizeof(names) / sizeof(names[0]);
	return names;
#else
	dlg_warn("swa was compiled without vulkan suport");
	*count = 0;
	return NULL;
#endif
}

bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	struct swa_display_win* dpy = get_display_win(base);
    return false;
}

const char* display_key_name(struct swa_display* base, enum swa_key key) {
	struct swa_display_win* dpy = get_display_win(base);
    return NULL;
}

enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
    return swa_keyboard_mod_none;
}

struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
    return NULL;
}

bool display_mouse_button_pressed(struct swa_display* base, enum swa_mouse_button button) {
	struct swa_display_win* dpy = get_display_win(base);
    return false;
}
void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_win* dpy = get_display_win(base);
}
struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
    return NULL;
}
struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	// struct swa_display_win* dpy = get_display_win(base);
    return NULL; 
}
bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source, void* trigger) {
	// struct swa_display_win* dpy = get_display_win(base);
	return false;
}
bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source, void* trigger) {
	// struct swa_display_win* dpy = get_display_win(base);
	return false;
}

struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_win* dpy = get_display_win(base);
	struct swa_window_win* win = calloc(1, sizeof(*win));

	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;
	return &win->base;
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

static void win_destroy(struct swa_window* base) {
	struct swa_window_win* win = get_window_win(base);
    free(win);
}

static enum swa_window_cap win_get_capabilities(struct swa_window* base) {
    (void) base;
    return swa_window_cap_cursor |
        swa_window_cap_fullscreen |
        swa_window_cap_maximize |
        swa_window_cap_minimize |
        swa_window_cap_size |
        swa_window_cap_position |
        swa_window_cap_size_limits |
        swa_window_cap_title |
        swa_window_cap_visibility;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_show(struct swa_window* base, bool show) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_set_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_set_position(struct swa_window* base, int x, int y) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_surface_frame(struct swa_window* base) {
    (void) base;
    // no-op
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	struct swa_window_win* win = get_window_wl(base);
}

static void win_begin_move(struct swa_window* base, void* trigger) {
}

static void win_begin_resize(struct swa_window* base, enum swa_edge edges,
		void* trigger) {
}

static void win_set_title(struct swa_window* base, const char* title) {
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
}

static bool win_is_client_decorated(struct swa_window* base) {
}

static bool win_get_vk_surface(struct swa_window* base, void* vkSurfaceKHR) {
}

static bool win_gl_make_current(struct swa_window* base) {
}

static bool win_gl_swap_buffers(struct swa_window* base) {
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
}

static void win_apply_buffer(struct swa_window* base) {
}

static const struct swa_window_interface window_impl = {
	.destroy = win_destroy,
	.get_capabilities = win_get_capabilities,
	.set_min_size = win_set_min_size,
	.set_max_size = win_set_max_size,
	.show = win_show,
	.set_size = win_set_size,
	.set_position = win_set_position,
	.refresh = win_refresh,
	.surface_frame = win_surface_frame,
	.set_state = win_set_state,
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

struct swa_display* swa_display_win_create(void) {
    struct swa_display_win* dpy = calloc(1, sizeof(*dpy));
    dpy->base.impl = &display_impl;
    return &dpy->base;
}