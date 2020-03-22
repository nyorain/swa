#include <swa/private/winapi.h>
#include <dlg/dlg.h>

#ifdef SWA_WITH_VK
  #include <vulkan/vulkan.h>
  #include <vulkan/vulkan_win32.h>
#endif

#include <wingdi.h>

// define constants that are sometimes not included
#ifndef SC_DRAGMOVE
 #define SC_DRAGMOVE 0xf012
#endif

static const struct swa_display_interface display_impl;
static const struct swa_window_interface window_impl;
static const wchar_t* window_class_name = L"swa_window_class";

// utility
static struct swa_display_win* get_display_win(struct swa_display* base) {
	dlg_assert(base->impl == &display_impl);
	return (struct swa_display_win*) base;
}

static struct swa_window_win* get_window_win(struct swa_window* base) {
	dlg_assert(base->impl == &window_impl);
	return (struct swa_window_win*) base;
}

// defined as macro so we don't lose file and line information
#define print_winapi_error(func) do { \
	wchar_t* buffer; \
	int code = GetLastError(); \
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | \
		FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, \
		code, 0, (wchar_t*) &buffer, 0, NULL); \
	dlg_errort(("winapi_error"), "%s: %ls", func, buffer); \
	LocalFree(buffer); \
} while(0)

static const struct {
	enum swa_edge edge;
	unsigned int winapi_code;
} edge_map[] = {
	{swa_edge_top, 3u},
	{swa_edge_bottom, 6u},
	{swa_edge_left, 1u},
	{swa_edge_right, 2u},
	{swa_edge_top_left, 4u},
	{swa_edge_bottom_left, 7u},
	{swa_edge_top_right, 5u},
	{swa_edge_bottom_right, 8u},
};

unsigned int edge_to_winapi(enum swa_edge edge) {
	unsigned len = sizeof(edge_map) / sizeof(edge_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(edge_map[i].edge == edge) {
			return edge_map[i].winapi_code;
		}
	}

	return 0;
}

enum swa_edge winapi_to_edge(unsigned code) {
	unsigned len = sizeof(edge_map) / sizeof(edge_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(edge_map[i].winapi_code == code) {
			return edge_map[i].edge;
		}
	}

	return swa_edge_none;
}

static const struct {
	enum swa_cursor_type cursor;
	const wchar_t* idc;
} cursor_map[] = {
	{swa_cursor_left_pointer, IDC_ARROW},
	{swa_cursor_load, IDC_WAIT},
	{swa_cursor_load_pointer, IDC_APPSTARTING},
	{swa_cursor_right_pointer, IDC_ARROW},
	{swa_cursor_hand, IDC_HAND},
	{swa_cursor_grab, IDC_HAND},
	{swa_cursor_crosshair, IDC_CROSS},
	{swa_cursor_help, IDC_HELP},
	{swa_cursor_beam, IDC_IBEAM},
	{swa_cursor_forbidden, IDC_NO},
	{swa_cursor_size, IDC_SIZEALL},
	{swa_cursor_size_left, IDC_SIZEWE},
	{swa_cursor_size_right, IDC_SIZEWE},
	{swa_cursor_size_top, IDC_SIZENS},
	{swa_cursor_size_bottom, IDC_SIZENS},
	{swa_cursor_size_top_left, IDC_SIZENWSE},
	{swa_cursor_size_bottom_right, IDC_SIZENWSE},
	{swa_cursor_size_top_right, IDC_SIZENESW},
	{swa_cursor_size_bottom_left, IDC_SIZENESW},
};

const wchar_t* cursor_to_winapi(enum swa_cursor_type cursor) {
	unsigned len = sizeof(cursor_map) / sizeof(cursor_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(cursor_map[i].cursor == cursor) {
			return cursor_map[i].idc;
		}
	}

	return NULL;
}

// window api
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
		swa_window_cap_size_limits |
		swa_window_cap_begin_move |
		swa_window_cap_begin_resize |
		swa_window_cap_title |
		swa_window_cap_visibility;
}

static void win_set_min_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_win* win = get_window_win(base);
	win->min_width = w;
	win->min_height = h;
}

static void win_set_max_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_win* win = get_window_win(base);
	win->max_width = w;
	win->max_height = h;
}

static void win_show(struct swa_window* base, bool show) {
	struct swa_window_win* win = get_window_win(base);
	ShowWindowAsync(win->handle, show ? SW_SHOW : SW_HIDE);
}

static void win_set_size(struct swa_window* base, unsigned w, unsigned h) {
	struct swa_window_win* win = get_window_win(base);
	SetWindowPos(win->handle, HWND_TOP, 0, 0, w, h,
		SWP_NOMOVE | SWP_ASYNCWINDOWPOS | SWP_NOZORDER);
}

static void win_set_cursor(struct swa_window* base, struct swa_cursor cursor) {
	struct swa_window_win* win = get_window_win(base);

	bool owned = false;
	HCURSOR handle = NULL;
	if(cursor.type == swa_cursor_none) {
		win->cursor.handle = NULL;
		win->cursor.owned = false;
	} else if(cursor.type == swa_cursor_image) {
		// TODO: implement bitmap from window utility
		// and then create owned cursor from bitmap
		dlg_error("TODO: not implemented");
		return;
	} else {
		enum swa_cursor_type type = cursor.type;
		if(type == swa_cursor_default) {
			win->cursor.set = false;
			return;
		}

		const wchar_t* idc = cursor_to_winapi(cursor.type);
		if(!idc) {
			dlg_warn("Invalid/Unsupported cursor type: %d", (int) cursor.type);
			return;
		}

		handle = LoadCursor(NULL, idc);
		if(!handle) {
			print_winapi_error("LoadCursor");
			return;
		}
	}

	// destroy previous cursor if needed
	if(win->cursor.handle && win->cursor.owned) {
		DestroyCursor(win->cursor.handle);
	}

	win->cursor.set = true;
	win->cursor.owned = owned;
	win->cursor.handle = handle;

	if(win->dpy->mouse_over == win) {
		SetCursor(win->cursor.handle);
	}

	// Alternative way of setting the cursor, we would not
	// have to handle WM_SETCURSOR messages when doing it like this.
	// Might have problems with multiple windows (since it changes
	// the window class), so we don't do it.
	// SetClassLongPtr(win->handle, GCL_HCURSOR, (LONG_PTR)(win->cursor.handle));
}

static void win_refresh(struct swa_window* base) {
	struct swa_window_win* win = get_window_win(base);
	// TODO: just use InvalidateRect? use RDW_FRAME?
	RedrawWindow(win->handle, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
	// InvalidateRect(win->handle, NULL, false);
}

static void win_surface_frame(struct swa_window* base) {
	(void) base;
	// we might be able to hack something together using
	// D3DKMTGetDWMVerticalBlankEvent
	// (we can get the adapter using D3DKMTOpenAdapterFromHdc).
	// Although it sounds like direct3d stuff, it actually
	// sits in the gdi library.
	// There are also blocking functions that just wait for
	// the next vblank we could run in a new thread.
}

static void win_set_state(struct swa_window* base, enum swa_window_state state) {
	// struct swa_window_win* win = get_window_win(base);
}

static void win_begin_move(struct swa_window* base) {
	struct swa_window_win* win = get_window_win(base);
	PostMessage(win->handle, WM_SYSCOMMAND, SC_DRAGMOVE, 0);
}

static void win_begin_resize(struct swa_window* base, enum swa_edge edges) {
	struct swa_window_win* win = get_window_win(base);
	unsigned code = edge_to_winapi(edges);
	PostMessage(win->handle, WM_SYSCOMMAND, SC_SIZE + code, 0);
}

static void win_set_title(struct swa_window* base, const char* title) {
}

static void win_set_icon(struct swa_window* base, const struct swa_image* img) {
}

static bool win_is_client_decorated(struct swa_window* base) {
	return false;
}

static uint64_t win_get_vk_surface(struct swa_window* base) {
#ifdef SWA_WITH_VK
	struct swa_window_win* win = get_window_win(base);
	if(win->surface_type != swa_surface_vk) {
		dlg_warn("can't get vulkan surface from non-vulkan window");
		return 0;
	}

	return win->vk.surface;
#else
	dlg_warn("swa was compiled without vulkan suport");
	return 0;
#endif
}

static bool win_gl_make_current(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_win* win = get_window_win(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Can't make non-gl window current");
		return false;
	}

	HDC hdc = GetDC(win->handle);
	if(!wglMakeCurrent(hdc, (HGLRC) win->gl_context)) {
		print_winapi_error("wglMakeCurrent");
		return false;
	}

	return true;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_swap_buffers(struct swa_window* base) {
#ifdef SWA_WITH_GL
	struct swa_window_win* win = get_window_win(base);
	if(win->surface_type != swa_surface_gl) {
		dlg_error("Can't make non-gl window current");
		return false;
	}

	HDC hdc = GetDC(win->handle);
	if(!SwapBuffers(hdc)) {
		print_winapi_error("SwapBuffers");
		return false;
	}

	return true;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_gl_set_swap_interval(struct swa_window* base, int interval) {
#ifdef SWA_WITH_GL
	dlg_error("unimplemented");
	return false;
#else
	dlg_warn("swa was compiled without gl suport");
	return false;
#endif
}

static bool win_get_buffer(struct swa_window* base, struct swa_image* img) {
	struct swa_window_win* win = get_window_win(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return false;
	}

	if(win->buffer.active) {
		dlg_error("There is already an active buffer");
		return false;
	}

	win->buffer.wdc = GetDC(win->handle);
	if(!win->buffer.wdc) {
		print_winapi_error("GetDC");
		return false;
	}

	// check if we have to recreate the bitmap.
	// Even though we might get a differrent DC every time, this
	// works as the bitmap created by CreateDIBSection works
	// with other DCs as well.
	if(win->buffer.width != win->width || win->buffer.height != win->height) {
		if(win->buffer.bitmap) {
			DeleteObject(win->buffer.bitmap);
		}

		win->buffer.width = win->width;
		win->buffer.height = win->height;

		BITMAPINFO bmi = {0};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = win->buffer.width;
		bmi.bmiHeader.biHeight = -(int)win->buffer.height; // top down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		win->buffer.bitmap = CreateDIBSection(win->buffer.wdc, &bmi, DIB_RGB_COLORS,
			&win->buffer.data, NULL, 0);
		if(!win->buffer.bitmap) {
			print_winapi_error("CreateDIBSection");
			return false;
		}
	}

	win->buffer.active = true;

	// See documentation for BITMAPINFOHEADER.
	// The bitmap RGB format specifies that blue is in the least
	// significant 8 bits and unused (alpha) the 8 most significant
	// bits. We therefore have word-order argb32
	img->format = swa_image_format_toggle_byte_word(swa_image_format_argb32);
	img->data = win->buffer.data;
	img->stride = 4 * win->width;
	img->width = win->width;
	img->height = win->height;

	return true;
}

static void win_apply_buffer(struct swa_window* base) {
	struct swa_window_win* win = get_window_win(base);
	if(win->surface_type != swa_surface_buffer) {
		dlg_error("Window doesn't have buffer surface");
		return;
	}

	if(!win->buffer.active) {
		dlg_error("There is no active buffer to apply");
		return;
	}

	dlg_assert(win->buffer.bitmap);
	dlg_assert(win->buffer.wdc);

	HDC bdc = CreateCompatibleDC(win->buffer.wdc);
	if(!bdc) {
		print_winapi_error("CreateCompatibleDC");
		goto cleanup;
	}

	HGDIOBJ prev = SelectObject(bdc, win->buffer.bitmap);
	if(!prev) {
		print_winapi_error("SelectObject");
		goto cleanup_bdc;
	}

	bool res = BitBlt(win->buffer.wdc, 0, 0,
		win->buffer.width, win->buffer.height, bdc, 0, 0, SRCCOPY);
	if(!res) {
		print_winapi_error("BitBlt");
	}

	SelectObject(bdc, prev);

cleanup_bdc:
	DeleteDC(bdc);

cleanup:
	ReleaseDC(win->handle, win->buffer.wdc);
	win->buffer.active = false;
	win->buffer.wdc = NULL;
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


// display api
void display_destroy(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
	UnregisterClassW(window_class_name, GetModuleHandle(NULL));
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
			print_winapi_error("Critical error in GetMessage");
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
	PostThreadMessage(dpy->main_thread_id, WM_USER, 0, 0);
}

enum swa_display_cap display_capabilities(struct swa_display* base) {
	// struct swa_display_win* dpy = get_display_win(base);
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
		struct swa_data_source* source) {
	// struct swa_display_win* dpy = get_display_win(base);
	return false;
}
bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_win* dpy = get_display_win(base);
	return false;
}

swa_gl_proc display_get_gl_proc_addr(struct swa_display* base, const char* name) {
	(void) base;
#ifdef SWA_WITH_GL
	return (swa_gl_proc) wglGetProcAddress(name);
#else
	dlg_error("swa was built without gl");
	return NULL;
#endif
}

static LRESULT CALLBACK win_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	struct swa_window_win* win = (struct swa_window_win*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if(!win) {
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}

	// TODO:
	// - WM_INPUTLANGCHANGE

	switch(msg) {
		case WM_PAINT: {
			if(win->width == 0 && win->height == 0) {
				break;
			}

			if(win->base.listener->draw) {
				win->base.listener->draw(&win->base);
			}

			// validate the window
			// do this before rendering so refresh being called
			// during rendering invalidates it again?
			// but that might create infinite render loops, i.e.
			// display_dispatch will not return at all anymore.
			// we probably have to defer something
			return DefWindowProc(hwnd, msg, wparam, lparam);
		} case WM_CLOSE: {
			if(win->base.listener->close) {
				win->base.listener->close(&win->base);
			}
			return 0;
		} case WM_SIZE: {
			unsigned width = LOWORD(lparam);
			unsigned height = HIWORD(lparam);
			if((win->width == width && win->height == height) ||
					(width == 0 && height == 0)) {
				break;
			}

			win->width = width;
			win->height = height;
			if(win->base.listener->resize) {
				win->base.listener->resize(&win->base, width, height);
			}

			return 0;
		} case WM_SYSCOMMAND: {
			enum swa_window_state state = swa_window_state_none;

			if(wparam == SC_MAXIMIZE) {
				state = swa_window_state_maximized;
			} else if(wparam == SC_MINIMIZE) {
				state = swa_window_state_minimized;
			} else if(wparam == SC_RESTORE) {
				state = swa_window_state_normal;
			} else if(wparam >= SC_SIZE && wparam <= SC_SIZE + 8) {
			// 	// TODO: set cursor
			// 	auto currentCursor = GetClassLongPtr(handle(), -12);
			// 	auto edge = winapiToEdges(wparam - SC_SIZE);
			// 	auto c = sizeCursorFromEdge(edge);
			// 	cursor(c);
			// 	result = ::DefWindowProc(handle(), message, wparam, lparam);
			// 	::SetClassLongPtr(handle(), -12, currentCursor);
			// 	break;
			}

			if(state != swa_window_state_none && win->base.listener->state) {
				win->base.listener->state(&win->base, state);
			}

			break;
		} case WM_GETMINMAXINFO: {
			MINMAXINFO* mmi = (MINMAXINFO*)(lparam);
			mmi->ptMaxTrackSize.x = win->max_width;
			mmi->ptMaxTrackSize.y = win->max_height;
			mmi->ptMinTrackSize.x = win->min_width;
			mmi->ptMinTrackSize.y = win->min_height;
			return 0;
		} case WM_ERASEBKGND: {
			return 1; // prevent the background erase
		} case WM_SETCURSOR: {
			if(win->cursor.set && LOWORD(lparam) == HTCLIENT) {
				SetCursor(win->cursor.handle);
				return 1;
			}
			break;
		}
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static wchar_t* widen(const char* utf8) {
	int count = MultiByteToWideChar(CP_UTF8, 0 , utf8, -1, NULL, 0);
	wchar_t* wide = malloc(count * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, count);
	return wide;
}

static char* narrow(const wchar_t* wide) {
	int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	char* utf8 = malloc(count * sizeof(char));
	WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, count, NULL, NULL);
	return utf8;
}

static bool register_window_class(void) {
	WNDCLASSEX wcx;
	wcx.cbSize = sizeof(wcx);
	// TODO: OWNDC not needed for buffer surfaces.
	// Not sure if it's needed for vulkan.
	// Maybe create two seperate window classes? do so lazily then though
	wcx.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcx.lpfnWndProc = win_proc;
	wcx.cbClsExtra = 0;
	wcx.cbWndExtra = 0;
	wcx.hInstance = GetModuleHandle(NULL);
	wcx.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcx.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcx.hbrBackground = NULL;
	wcx.lpszMenuName = NULL;
	wcx.lpszClassName = window_class_name;

	if(!RegisterClassExW(&wcx)) {
		print_winapi_error("RegisterClassEx");
		return false;
	}

	return true;
}

struct swa_window* display_create_window(struct swa_display* base,
		const struct swa_window_settings* settings) {
	struct swa_display_win* dpy = get_display_win(base);
	struct swa_window_win* win = calloc(1, sizeof(*win));

	win->base.impl = &window_impl;
	win->base.listener = settings->listener;
	win->dpy = dpy;

	// TODO, use GetSystemMetrics. See docs for MINMAXSIZE
	win->max_width = win->max_height = 9999;
	HINSTANCE hinstance = GetModuleHandle(NULL);

	// create window
	unsigned exstyle = 0;
	unsigned style = WS_OVERLAPPEDWINDOW;

	// NOTE: in theory this is not supported when we use CS_OWNDC but in practice it's
	// the only way and works.
	if(settings->transparent) {
		exstyle |= WS_EX_LAYERED;
	}

	wchar_t* titlew = settings->title ? widen(settings->title) : L"";
	int width = settings->width == SWA_DEFAULT_SIZE ? CW_USEDEFAULT : settings->width;
	int height = settings->height == SWA_DEFAULT_SIZE ? CW_USEDEFAULT : settings->height;
	win->handle = CreateWindowEx(exstyle, window_class_name, titlew, style,
		CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, hinstance, win);
	if(!win->handle) {
		print_winapi_error("CreateWindowEx");
		goto error;
	}

	SetWindowLongPtr(win->handle, GWLP_USERDATA, (uintptr_t) win);
	if(settings->transparent) {
		// This will simply cause windows to respect the alpha bits in the content of the window
		// and not actually blur anything.
		DWM_BLURBEHIND bb = {0};
		bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
		bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);  // makes the window transparent
		bb.fEnable = TRUE;
		DwmEnableBlurBehindWindow(win->handle, &bb);

		// This is not what makes the window transparent.
		// We simply have to do this so the window contents are shown.
		// We only have to set the layered flag to make DwmEnableBlueBehinWindow function
		// correctly and this causes the flag to have no further effect.
		SetLayeredWindowAttributes(win->handle, 0x1, 0, LWA_COLORKEY);
	}

	win_set_cursor(&win->base, settings->cursor);
	ShowWindowAsync(win->handle, SW_SHOWDEFAULT);

	// surface
	win->surface_type = settings->surface;
	// no-op for buffer surface
	if(win->surface_type == swa_surface_vk) {
#ifdef SWA_WITH_VK
		win->vk.instance = settings->surface_settings.vk.instance;
		if(!win->vk.instance) {
			dlg_error("No vulkan instance passed for vulkan window");
			goto error;
		}

		VkWin32SurfaceCreateInfoKHR info = {0};
		info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		info.hinstance = hinstance;
		info.hwnd = win->handle;

		VkInstance instance = (VkInstance) win->vk.instance;
		VkSurfaceKHR surface;

		PFN_vkCreateWin32SurfaceKHR fn = (PFN_vkCreateWin32SurfaceKHR)
			vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
		if(!fn) {
			dlg_error("Failed to load 'vkCreateWin32SurfaceKHR' function");
			goto error;
		}

		VkResult res = fn(instance, &info, NULL, &surface);
		if(res != VK_SUCCESS) {
			dlg_error("Failed to create vulkan surface: %d", res);
			goto error;
		}

		win->vk.surface = (uint64_t)surface;
#else
		dlg_error("swa was compiled without vulkan support");
		goto error;
#endif
	} else if(win->surface_type == swa_surface_gl) {
#ifdef SWA_WITH_GL
		HDC hdc = GetDC(win->handle);
		if(!swa_wgl_init_context(dpy, hdc, &settings->surface_settings.gl,
				settings->transparent, &win->gl_context)) {
			goto error;
		}
#else
		dlg_error("swa was compiled without gl support");
		goto error;
#endif
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
	.get_gl_proc_addr = display_get_gl_proc_addr,
};

struct swa_display* swa_display_win_create(const char* appname) {
	(void) appname;

	if(!register_window_class()) {
		return NULL;
	}

	struct swa_display_win* dpy = calloc(1, sizeof(*dpy));
	dpy->base.impl = &display_impl;
	dpy->main_thread_id = GetCurrentThreadId();
	dpy->dummy_window = CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, window_class_name,
		L"swa dummy window", WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 1, 1,
		NULL, NULL, GetModuleHandleW(NULL), NULL);
	if(!dpy->dummy_window) {
		print_winapi_error("CreateWindowEx dummy window");
		goto error;
	}

	return &dpy->base;

error:
	display_destroy(&dpy->base);
	return NULL;
}
