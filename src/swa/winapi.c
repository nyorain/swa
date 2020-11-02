#include <swa/private/winapi.h>
#include <dlg/dlg.h>

#ifdef SWA_WITH_VK
  #define VK_USE_PLATFORM_WIN32_KHR
  #ifndef SWA_WITH_LINKED_VK
	#define VK_NO_PROTOTYPES
  #endif // SWA_WITH_LINKED_VK
  #include <vulkan/vulkan.h>
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

static unsigned int edge_to_winapi(enum swa_edge edge) {
	unsigned len = sizeof(edge_map) / sizeof(edge_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(edge_map[i].edge == edge) {
			return edge_map[i].winapi_code;
		}
	}

	return 0;
}

static enum swa_edge winapi_to_edge(unsigned code) {
	unsigned len = sizeof(edge_map) / sizeof(edge_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(edge_map[i].winapi_code == code) {
			return edge_map[i].edge;
		}
	}

	return swa_edge_none;
}

static const struct {
	enum swa_key key;
	unsigned vkcode;
} key_map [] = {
	{swa_key_none, 0x0},
	{swa_key_a, 'A'},
	{swa_key_b, 'B'},
	{swa_key_c, 'C'},
	{swa_key_d, 'D'},
	{swa_key_e, 'E'},
	{swa_key_f, 'F'},
	{swa_key_g, 'G'},
	{swa_key_h, 'H'},
	{swa_key_i, 'I'},
	{swa_key_j, 'J'},
	{swa_key_k, 'K'},
	{swa_key_l, 'L'},
	{swa_key_m, 'M'},
	{swa_key_n, 'N'},
	{swa_key_o, 'O'},
	{swa_key_p, 'P'},
	{swa_key_q, 'Q'},
	{swa_key_r, 'R'},
	{swa_key_s, 'S'},
	{swa_key_t, 'T'},
	{swa_key_u, 'U'},
	{swa_key_v, 'V'},
	{swa_key_w, 'W'},
	{swa_key_x, 'X'},
	{swa_key_y, 'Y'},
	{swa_key_z, 'Z'},
	{swa_key_k0, '0'},
	{swa_key_k1, '1'},
	{swa_key_k2, '2'},
	{swa_key_k3, '3'},
	{swa_key_k4, '4'},
	{swa_key_k5, '5'},
	{swa_key_k6, '6'},
	{swa_key_k7, '7'},
	{swa_key_k8, '8'},
	{swa_key_k9, '9'},
	{swa_key_backspace, VK_BACK},
	{swa_key_tab, VK_TAB},
	{swa_key_clear, VK_CLEAR},
	{swa_key_enter, VK_RETURN},
	{swa_key_leftshift, VK_SHIFT},
	{swa_key_leftctrl, VK_CONTROL},
	{swa_key_leftalt, VK_MENU},
	{swa_key_capslock, VK_CAPITAL},
	{swa_key_katakana, VK_KANA},
	{swa_key_hanguel, VK_HANGUL},
	{swa_key_hanja, VK_HANJA},
	{swa_key_escape, VK_ESCAPE},
	{swa_key_space, VK_SPACE},
	{swa_key_pageup, VK_PRIOR},
	{swa_key_pagedown, VK_NEXT},
	{swa_key_end, VK_END},
	{swa_key_home, VK_HOME},
	{swa_key_left, VK_LEFT},
	{swa_key_right, VK_RIGHT},
	{swa_key_up, VK_UP},
	{swa_key_down, VK_DOWN},
	{swa_key_select, VK_SELECT},
	{swa_key_print, VK_PRINT},
	{swa_key_insert, VK_INSERT},
	{swa_key_del, VK_DELETE},
	{swa_key_help, VK_HELP},
	{swa_key_leftmeta, VK_LWIN},
	{swa_key_rightmeta, VK_RWIN},
	{swa_key_sleep, VK_SLEEP},
	{swa_key_kp0, VK_NUMPAD0},
	{swa_key_kp1, VK_NUMPAD1},
	{swa_key_kp2, VK_NUMPAD2},
	{swa_key_kp3, VK_NUMPAD3},
	{swa_key_kp4, VK_NUMPAD4},
	{swa_key_kp5, VK_NUMPAD5},
	{swa_key_kp6, VK_NUMPAD6},
	{swa_key_kp7, VK_NUMPAD7},
	{swa_key_kp8, VK_NUMPAD8},
	{swa_key_kp9, VK_NUMPAD9},
	{swa_key_kpmultiply, VK_MULTIPLY},
	{swa_key_kpplus, VK_ADD},
	{swa_key_kpminus, VK_SUBTRACT},
	{swa_key_kpdivide, VK_DIVIDE},
	{swa_key_kpperiod, VK_SEPARATOR}, //XXX not sure
	{swa_key_f1, VK_F1},
	{swa_key_f2, VK_F2},
	{swa_key_f3, VK_F3},
	{swa_key_f4, VK_F4},
	{swa_key_f5, VK_F5},
	{swa_key_f6, VK_F6},
	{swa_key_f7, VK_F7},
	{swa_key_f8, VK_F8},
	{swa_key_f9, VK_F9},
	{swa_key_f10, VK_F10},
	{swa_key_f11, VK_F11},
	{swa_key_f12, VK_F12},
	{swa_key_f13, VK_F13},
	{swa_key_f14, VK_F14},
	{swa_key_f15, VK_F15},
	{swa_key_f16, VK_F16},
	{swa_key_f17, VK_F17},
	{swa_key_f18, VK_F18},
	{swa_key_f19, VK_F19},
	{swa_key_f20, VK_F20},
	{swa_key_f21, VK_F21},
	{swa_key_f22, VK_F22},
	{swa_key_f23, VK_F23},
	{swa_key_f24, VK_F24},
	{swa_key_numlock, VK_NUMLOCK},
	{swa_key_scrollock, VK_SCROLL},
	{swa_key_leftshift, VK_LSHIFT},
	{swa_key_rightshift, VK_RSHIFT},
	{swa_key_leftctrl, VK_LCONTROL},
	{swa_key_rightctrl, VK_RCONTROL},
	{swa_key_leftalt, VK_LMENU},
	{swa_key_rightalt, VK_RMENU},
	// XXX: some browser keys after this. not sure about it
	{swa_key_mute, VK_VOLUME_MUTE},
	{swa_key_volumedown, VK_VOLUME_DOWN},
	{swa_key_volumeup, VK_VOLUME_UP},
	{swa_key_nextsong, VK_MEDIA_NEXT_TRACK},
	{swa_key_previoussong, VK_MEDIA_PREV_TRACK},
	{swa_key_stopcd, VK_MEDIA_STOP}, // XXX: or keycode::stop?
	{swa_key_playpause, VK_MEDIA_PLAY_PAUSE},
	{swa_key_mail, VK_LAUNCH_MAIL},

	{swa_key_period, VK_OEM_PERIOD},
	{swa_key_comma, VK_OEM_COMMA},
	{swa_key_equals, VK_OEM_PLUS},
	{swa_key_minus, VK_OEM_MINUS},
	{swa_key_102nd, VK_OEM_102},

	{swa_key_semicolon, VK_OEM_1},
	{swa_key_slash, VK_OEM_2},
	{swa_key_grave, VK_OEM_3},
	{swa_key_leftbrace, VK_OEM_4},
	{swa_key_backslash, VK_OEM_5},
	{swa_key_rightbrace, VK_OEM_6},
	{swa_key_apostrophe, VK_OEM_7},

	{swa_key_play, VK_PLAY},
	{swa_key_zoom, VK_ZOOM},
};

static enum swa_key winapi_to_key(unsigned vkcode) {
	unsigned len = sizeof(key_map) / sizeof(key_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(key_map[i].vkcode == vkcode) {
			return key_map[i].key;
		}
	}

	return swa_key_none;
}

static unsigned key_to_winapi(enum swa_key key) {
	unsigned len = sizeof(key_map) / sizeof(key_map[0]);
	for(unsigned i = 0u; i < len; ++i) {
		if(key_map[i].key == key) {
			return key_map[i].vkcode;
		}
	}

	return 0x0;
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

static const wchar_t* cursor_to_winapi(enum swa_cursor_type cursor) {
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
	// TODO
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
	// TODO: not finished
	struct swa_window_win* win = get_window_win(base);
	if(state == swa_window_state_fullscreen) {
		long style = GetWindowLong(win->handle, GWL_STYLE);
		long exstyle = GetWindowLong(win->handle, GWL_EXSTYLE);

		MONITORINFO monitorinfo;
		monitorinfo.cbSize = sizeof(monitorinfo);
		GetMonitorInfo(MonitorFromWindow(win->handle, MONITOR_DEFAULTTONEAREST), &monitorinfo);
		RECT rect = monitorinfo.rcMonitor;
		rect.right -= rect.left;
		rect.bottom -= rect.top;

		SetWindowLong(win->handle, GWL_STYLE, (style | WS_POPUP) & ~(WS_OVERLAPPEDWINDOW));
		SetWindowLong(win->handle, GWL_EXSTYLE, exstyle & ~(WS_EX_DLGMODALFRAME |
			WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

		// TODO
		// the rect.bottom + 1 is needed here since some (buggy?) winapi implementations
		// go automatically in real fullscreen mode when the window is a popup and the size
		// the same as the monitor (observed behaviour).
		// we do not handle/support real fullscreen mode since then
		// the window has to take care about correct alt-tab/minimize handling
		// which becomes easily buggy.
		// SetWindowPos(win->handle, HWND_TOP, rect.left, rect.top, rect.right, rect.bottom + 1,
		SetWindowPos(win->handle, NULL, rect.left, rect.top, rect.right, rect.bottom,
			// SWP_NOOWNERZORDER |	SWP_ASYNCWINDOWPOS | SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
			SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
	}
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
	.set_cursor = win_set_cursor,
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
static void display_destroy(struct swa_display* base) {
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

static bool display_dispatch(struct swa_display* base, bool block) {
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

static void display_wakeup(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
	PostThreadMessage(dpy->main_thread_id, WM_USER, 0, 0);
}

static enum swa_display_cap display_capabilities(struct swa_display* base) {
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

static const char** display_vk_extensions(struct swa_display* base, unsigned* count) {
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

static bool async_pressed(unsigned vkcode) {
	SHORT state = GetAsyncKeyState(vkcode);
	return ((1 << 16) & state);
}

static enum swa_keyboard_mod async_keyboard_mods(void) {
	enum swa_keyboard_mod mods = 0;
	if(async_pressed(VK_SHIFT))	mods |= swa_keyboard_mod_shift;
	if(async_pressed(VK_CONTROL)) mods |= swa_keyboard_mod_ctrl;
	if(async_pressed(VK_MENU)) mods |= swa_keyboard_mod_alt;
	if(async_pressed(VK_LWIN)) mods |= swa_keyboard_mod_super;
	if(async_pressed(VK_CAPITAL)) mods |= swa_keyboard_mod_caps_lock;
	if(async_pressed(VK_NUMLOCK)) mods |= swa_keyboard_mod_num_lock;
	return mods;
}

static bool display_key_pressed(struct swa_display* base, enum swa_key key) {
	(void) base;
	return async_pressed(key_to_winapi(key));
}

static const char* display_key_name(struct swa_display* base, enum swa_key key) {
	// struct swa_display_win* dpy = get_display_win(base);
	return NULL;
}

static enum swa_keyboard_mod display_active_keyboard_mods(struct swa_display* base) {
	(void) base;
	return async_keyboard_mods();
}

static struct swa_window* display_get_keyboard_focus(struct swa_display* base) {
	// struct swa_display_win* dpy = get_display_win(base);
	return NULL;
}

static unsigned button_to_winapi(enum swa_mouse_button button) {
	switch(button) {
		case swa_mouse_button_right: return VK_RBUTTON;
		case swa_mouse_button_left: return VK_LBUTTON;
		case swa_mouse_button_middle: return VK_MBUTTON;
		case swa_mouse_button_custom1: return VK_XBUTTON1;
		case swa_mouse_button_custom2: return VK_XBUTTON2;
		default: return 0u;
	}
}

static bool display_mouse_button_pressed(struct swa_display* base, enum swa_mouse_button button) {
	unsigned state = GetAsyncKeyState(button_to_winapi(button));
	return ((1 << 16) & state);
}

static void display_mouse_position(struct swa_display* base, int* x, int* y) {
	struct swa_display_win* dpy = get_display_win(base);
	*x = dpy->mx;
	*y = dpy->my;
}
static struct swa_window* display_get_mouse_over(struct swa_display* base) {
	struct swa_display_win* dpy = get_display_win(base);
	return dpy->mouse_over ? &dpy->mouse_over->base : NULL;
}
static struct swa_data_offer* display_get_clipboard(struct swa_display* base) {
	// struct swa_display_win* dpy = get_display_win(base);
	return NULL;
}
static bool display_set_clipboard(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_win* dpy = get_display_win(base);
	return false;
}
static bool display_start_dnd(struct swa_display* base,
		struct swa_data_source* source) {
	// struct swa_display_win* dpy = get_display_win(base);
	return false;
}

static swa_proc display_get_gl_proc_addr(struct swa_display* base, const char* name) {
	(void) base;
#ifdef SWA_WITH_GL
	return (swa_proc) wglGetProcAddress(name);
#else
	dlg_error("swa was built without gl");
	return NULL;
#endif
}

static void handle_mouse_button(struct swa_window_win* win, bool pressed,
		enum swa_mouse_button btn, LPARAM lparam) {
	if(win->base.listener->mouse_button) {
		struct swa_mouse_button_event ev = {0};
		ev.pressed = pressed;
		ev.button = btn;
		ev.x = GET_X_LPARAM(lparam);
		ev.y = GET_Y_LPARAM(lparam);
		win->base.listener->mouse_button(&win->base, &ev);
	}

	// TODO: store pressed state in win->dpy
}

static void handle_key(struct swa_window_win* win, bool pressed,
		WPARAM wparam, LPARAM lparam) {
	// extractd utf8
	MSG msg;
	unsigned i = 0;
	wchar_t src[9] = {0};
	while(PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) && i < 8) {
		if((msg.message != WM_CHAR) && (msg.message != WM_SYSCHAR)) {
			break;
		}

		dlg_assert(GetMessage(&msg, NULL, 0, 0)); // remove it
		src[i++] = msg.wParam;
	}

	char* utf8 = NULL;
	if(i > 0) {
		utf8 = narrow(src);
	}

	if(win->base.listener->key) {
		struct swa_key_event ev = {0};
		ev.pressed = pressed;
		ev.keycode = winapi_to_key(wparam);
		ev.repeated = pressed && (lparam & 0x40000000);
		ev.utf8 = utf8;

		win->base.listener->key(&win->base, &ev);
	}

	free(utf8);
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
		case WM_LBUTTONDOWN:
			handle_mouse_button(win, true, swa_mouse_button_left, lparam);
			break;
		case WM_LBUTTONUP:
			handle_mouse_button(win, false, swa_mouse_button_left, lparam);
			break;
		case WM_RBUTTONDOWN:
			handle_mouse_button(win, true, swa_mouse_button_right, lparam);
			break;
		case WM_RBUTTONUP:
			handle_mouse_button(win, false, swa_mouse_button_right, lparam);
			break;
		case WM_MBUTTONDOWN:
			handle_mouse_button(win, true, swa_mouse_button_middle, lparam);
			break;
		case WM_MBUTTONUP:
			handle_mouse_button(win, false, swa_mouse_button_middle, lparam);
			break;
		case WM_XBUTTONDOWN:
			handle_mouse_button(win, true, (HIWORD(wparam) == 1) ?
				swa_mouse_button_custom1 :
				swa_mouse_button_custom2, lparam);
			break;
		case WM_XBUTTONUP:
			handle_mouse_button(win, false, (HIWORD(wparam) == 1) ?
				swa_mouse_button_custom1 :
				swa_mouse_button_custom2, lparam);
			break;
		case WM_MOUSELEAVE: {
			if(win->base.listener->mouse_cross) {
				struct swa_mouse_cross_event ev = {0};
				ev.entered = false;
				ev.x = win->dpy->mx;
				ev.y = win->dpy->my;
				win->base.listener->mouse_cross(&win->base, &ev);
			}

			dlg_assert(win == win->dpy->mouse_over);
			win->dpy->mouse_over = NULL;
			win->dpy->mx = 0;
			win->dpy->my = 0;
			break;
		} case WM_MOUSEMOVE: {
			struct swa_mouse_move_event ev = {0};
			ev.x = GET_X_LPARAM(lparam);
			ev.y = GET_Y_LPARAM(lparam);
			ev.dx = ev.x - win->dpy->mx;
			ev.dy = ev.y - win->dpy->my;

			// check for implicit mouse over change
			// windows does not send any mouse enter events, we have to detect them this way
			if(win != win->dpy->mouse_over) {
				if(win->base.listener->mouse_cross) {
					struct swa_mouse_cross_event cev = {0};
					cev.entered = true;
					cev.x = ev.x;
					cev.y = ev.y;
					win->base.listener->mouse_cross(&win->base, &cev);
				}

				win->dpy->mouse_over = win;

				// Request wm_mouseleave events
				// we have to do this everytime
				// therefore we do not send leave event here (should be generated)
				TRACKMOUSEEVENT tm = {0};
				tm.cbSize = sizeof(tm);
				tm.dwFlags = TME_LEAVE;
				tm.hwndTrack = win->handle;
				TrackMouseEvent(&tm);
			}

			if(win->base.listener->mouse_move) {
				win->base.listener->mouse_move(&win->base, &ev);
			}

			win->dpy->mx = ev.x;
			win->dpy->my = ev.y;
			break;
		} case WM_MOUSEWHEEL: {
			if(win->base.listener->mouse_wheel) {
				float dy = GET_WHEEL_DELTA_WPARAM(wparam) / 120.0;
				win->base.listener->mouse_wheel(&win->base, 0.f, dy);
			}
			break;
		} case WM_MOUSEHWHEEL: {
			if(win->base.listener->mouse_wheel) {
				float dx = -GET_WHEEL_DELTA_WPARAM(wparam) / 120.0;
				win->base.listener->mouse_wheel(&win->base, dx, 0.f);
			}
			break;
		} case WM_KEYDOWN: {
			handle_key(win, true, wparam, lparam);
			break;
		} case WM_KEYUP: {
			handle_key(win, false, wparam, lparam);
			break;
		}
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
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

static struct swa_window* display_create_window(struct swa_display* base,
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

		PFN_vkGetInstanceProcAddr fpGetProcAddr = (PFN_vkGetInstanceProcAddr)
			settings->surface_settings.vk.get_instance_proc_addr;
#ifdef SWA_WITH_LINKED_VK
		if(!fpGetProcAddr) {
			fpGetProcAddr = &vkGetInstanceProcAddr;
		}
#else // SWA_WITH_LINKED_VK
		if(!fpGetProcAddr) {
			dlg_error("No vkGetInstanceProcAddr provided, swa wasn't linked "
				"against vulkan");
			goto error;
		}
#endif // SWA_WITH_LINKED_VK

		PFN_vkCreateWin32SurfaceKHR fn = (PFN_vkCreateWin32SurfaceKHR)
			fpGetProcAddr(instance, "vkCreateWin32SurfaceKHR");
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
		win->vk.destroy_surface_pfn = (void*)
			fpGetProcAddr(instance, "vkDestroySurfaceKHR");
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
