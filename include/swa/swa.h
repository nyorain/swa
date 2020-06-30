#pragma once

#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <swa/config.h>
#include <swa/image.h>
#include <swa/key.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_backend;
struct swa_display;
struct swa_window;
struct swa_data_offer;
struct swa_data_source;
struct swa_window_listener;

typedef void (*swa_gl_proc)(void);

// When this value is specified as size in swa_window_settings,
// the system default will be used.
// If the system has no default, will use the SWA_FALLBACK_* values.
// The main advantage of using SWA_DEFAULT_SIZE is that swa will
// always generate a size event as soon as the window is visible
// and has a fixed size and applications that have to create size-dependent
// resources (additional render buffers/swapchain) can postpone
// that until the first size event and have therefore no need for
// the initial resize handling on platforms where the window can't
// be created with the requested size.
#define SWA_DEFAULT_SIZE 0

#define SWA_FALLBACK_WIDTH 800
#define SWA_FALLBACK_HEIGHT 500

// Describes the kind of render surface created for a window.
enum swa_surface_type {
	swa_surface_none = 0,
	swa_surface_buffer,
	swa_surface_gl,
	swa_surface_vk,
};

// Functional capability of a swa_display implementation.
// When a display doesn't have a certain capability, functions
// associated with it should not be called and will result in an error.
// For instance, when a display doesn't report the 'keyboard' capability,
// using it to check whether a key is pressed will return an undefined
// value that has no meaning.
enum swa_display_cap {
	swa_display_cap_none = 0,
	swa_display_cap_gl = (1 << 0),
	swa_display_cap_vk = (1 << 1),
	swa_display_cap_buffer_surface = (1 << 2),
	swa_display_cap_keyboard = (1 << 3),
	swa_display_cap_mouse = (1 << 4),
	swa_display_cap_touch = (1 << 5),
	swa_display_cap_clipboard = (1 << 6),
	swa_display_cap_dnd = (1 << 7),
	swa_display_cap_client_decoration = (1 << 8),
	swa_display_cap_server_decoration = (1 << 9)
};

// Keyboard modifier.
enum swa_keyboard_mod {
	swa_keyboard_mod_none = 0,
	swa_keyboard_mod_shift = (1 << 0),
	swa_keyboard_mod_ctrl = (1 << 1),
	swa_keyboard_mod_alt = (1 << 2),
	swa_keyboard_mod_super = (1 << 3), // aka "windows key"
	swa_keyboard_mod_caps_lock = (1 << 4),
	swa_keyboard_mod_num_lock = (1 << 5)
};

// Functional capability of a swa_window implementation.
// When a window doesn't have a certain capability, functions
// associated with it should not be called and will result in an error.
// For instance, when a window doesn't report the 'size_limits' capability,
// calling set_{min, max}_size won't have any effect.
enum swa_window_cap {
	swa_window_cap_none = 0,
	swa_window_cap_size = (1L << 1),
	swa_window_cap_fullscreen = (1L << 2),
	swa_window_cap_minimize = (1L << 3),
	swa_window_cap_maximize = (1L << 4),
	swa_window_cap_size_limits = (1L << 5),
	swa_window_cap_icon = (1L << 6),
	swa_window_cap_cursor = (1L << 7),
	swa_window_cap_title = (1L << 8),
	swa_window_cap_begin_move = (1L << 9),
	swa_window_cap_begin_resize = (1L << 10),
	swa_window_cap_visibility = (1L << 11),
};

// Represents the current state of a window.
// Note that this state is independent from the window's visibility.
enum swa_window_state {
	swa_window_state_none = 0,
	swa_window_state_normal,
	swa_window_state_maximized,
	swa_window_state_minimized,
	swa_window_state_fullscreen
};

// The edge of a window. Needed for resizing.
enum swa_edge {
	swa_edge_none = 0,
	swa_edge_top = 1,
	swa_edge_bottom = 2,
	swa_edge_left = 4,
	swa_edge_right = 8,
	swa_edge_top_left = 5,
	swa_edge_bottom_left = 6,
	swa_edge_top_right = 9,
	swa_edge_bottom_right = 10,
};

// Specifies the type a cursor has.
// Besides the special types 'image' and 'none' (hides the cursor),
// the cursor types will be translated to the native equivalents
// if possible.
enum swa_cursor_type {
	swa_cursor_unknown = 0,
	swa_cursor_image = 1,
	swa_cursor_none = 2,

	swa_cursor_default,
	swa_cursor_left_pointer, // default pointer cursor
	swa_cursor_load, // load icon
	swa_cursor_load_pointer, // load icon combined with default pointer
	swa_cursor_right_pointer, // default pointer to the right (mirrored)
	swa_cursor_hand, // a hande signaling that something can be grabbed
	swa_cursor_grab, // some kind of grabbed cursor (e.g. closed hand)
	swa_cursor_crosshair, // crosshair, e.g. used for move operations
	swa_cursor_help, // help cursor sth like a question mark
	swa_cursor_beam, // beam/caret e.g. for textfield
	swa_cursor_forbidden, // no/not allowed, e.g. for unclickable button
	swa_cursor_size, // general size pointer
	swa_cursor_size_left,
	swa_cursor_size_right,
	swa_cursor_size_top,
	swa_cursor_size_bottom,
	swa_cursor_size_bottom_right,
	swa_cursor_size_bottom_left,
	swa_cursor_size_top_right,
	swa_cursor_size_top_left
};

enum swa_mouse_button {
	swa_mouse_button_none = 0,
	swa_mouse_button_left,
	swa_mouse_button_right,
	swa_mouse_button_middle,
	swa_mouse_button_custom1, // often: back
	swa_mouse_button_custom2, // often: forward
	swa_mouse_button_custom3,
	swa_mouse_button_custom4,
	swa_mouse_button_custom5,
};

enum swa_preference {
	swa_preference_none = 0, // don't care, use default
	swa_preference_yes,
	swa_preference_no,
};

enum swa_api {
	swa_api_gl,
	swa_api_gles,
};

// Defines a cursor.
// hx, hy define the cursor hotspot in local coordinates.
// The hotspot and image members are only relevant
// when type is 'image'.
struct swa_cursor {
	enum swa_cursor_type type;
	int hx, hy;
	struct swa_image image;
};

// Settings for creating opengl surface and context.
struct swa_gl_surface_settings {
	// The requested context version
	unsigned major, minor;
	// Hard constraint. One can use swa_display_get_default_api.
	// But then you have to get all function pointers using
	// swa_display_get_gl_proc_addr and link with neither OpenGL
	// nor OpenGLES.
	enum swa_api api;
	bool forward_compatible;
	bool compatibility;
	bool srgb;
	bool debug;
	unsigned depth;
	unsigned stencil;
	unsigned samples;
};

// The instance must remain valid until the window is destroyed.
// It must have furthermore been created with *all* extensions
// returned by swa_display_vk_extensions.
struct swa_vk_surface_settings {
	uintptr_t instance; // type: VkInstance
};

struct swa_buffer_surface_settings {
	// Only a hint for optimization, backends don't have to returns
	// images with this format.
	// Applications can use swa_write_pixel or swa_convert_image to modify
	// the returned buffer regardless which image format it has.
	enum swa_image_format preferred_format;
};

struct swa_window_settings {
	// The initial window size. Can be SWA_DEFAULT_SIZE in which case
	// the system's default (or SWA_FALLBACK_{WIDTH, HEIGHT} if there isn't any)
	// will be chosen. Note that the backend might not be able to create
	// a window with exactly this size. In that case a resize event
	// will be generated though. If SWA_DEFAULT_SIZE was specified,
	// a resize event will always be generated (for visible windows).
	unsigned width, height;
	const char* title; // Title of the window, optional
	struct swa_cursor cursor; // Initial window cursor

	enum swa_surface_type surface; // Render surface to be created
	union {
		// allow preferred format for buffer surface?
		struct swa_gl_surface_settings gl;
		struct swa_vk_surface_settings vk;
		struct swa_buffer_surface_settings buffer;
	} surface_settings;

	// Whether transparency is needed.
	// On some backends this has a performance impact and should therefore
	// only be enabled when needed. Even when set to false, the window
	// might allow transparency.
	// Not possible on all backends.
	bool transparent;
	bool hide; // create the window in a hidden state
	enum swa_window_state state; // initial window state
	enum swa_preference client_decorate; // prefer client decorations?

	// The listener object must remain valid until it is changed or the window
	// is destroyed. Must not be NULL.
	const struct swa_window_listener* listener;
};

struct swa_key_event {
	// The text input this key event generated.
	// May be NULL, usually this is the case for key release events
	// or special keys such as escape.
	const char* utf8;
	// Keycode of the pressed or release key.
	enum swa_key keycode;
	// Currently active keyboard modifiers
	enum swa_keyboard_mod modifiers;
	// Whether the key was pressed or released.
	bool pressed;
	// Whether or not this is a repeated event.
	// Usually only true for press events.
	// In some cases it may be useful to ignore repeated events.
	bool repeated;
};

struct swa_mouse_button_event {
	// The new mouse position in window-local coordinates.
	int x, y;
	// The button that was pressed or released.
	enum swa_mouse_button button;
	// Whether the button was pressed or released.
	bool pressed;
};

struct swa_mouse_move_event {
	// The new mouse position in window-local coordinates.
	int x, y;
	// The delta, i.e. the current mouse position minus the last
	// known position in window-local cordinates.
	int dx, dy;
};

struct swa_mouse_cross_event {
	// Whether the mouse entered or left the window.
	bool entered;
	// The position of the mouse in window-local coordinates.
	int x, y;
};

struct swa_dnd_event {
	// The data offer associated with this dnd session.
	// Guaranteed to be valid until `dnd_leave` is called.
	// When `dnd_drop` is called, ownership of this offer is transferred
	// to the application, i.e. it must free it after being finished
	// using it.
	struct swa_data_offer* offer;
	// Position of the dnd item in window-local coorindates.
	int x, y;
};

struct swa_touch_event {
	// Identification of the point. This id will passed to further
	// touch events and can be used to identify this touch point.
	// Touch point ids are unique as long as they exist (between `touch_begin`
	// and `touch_end` or `touch_cancel`) but might be reused after that.
	unsigned id;
	// Position of the touch point in window-local coordinates.
	int x, y;
};

// All callbacks are guaranteed to only be called from inside
// `swa_display_dispatch`
struct swa_window_listener {
	// Called by the system e.g. when the window contents where invalidated
	// or emitted in response to a call to `swa_window_refresh`.
	// For newly created windows, this will usually be called, unless
	// the window is hidden, minimized or otherwise not shown.
	void (*draw)(struct swa_window*);
	// Called when the systems signals that this window should be closed.
	// Can be used to e.g. display a confirmation dialog or just destroy
	// the window.
	void (*close)(struct swa_window*);
	// Called when the corresponding `swa_window` was destroyed.
	// Not further events will be emitted.
	void (*destroyed)(struct swa_window*);

	// Called when the window was resized by the system.
	// For newly created windows, this will be emitted only when
	// the initial window size was chosen to be different from the
	// size passed in the `swa_window_settings`. When the settings
	// used `SWA_DEFAULT_SIZE`, this event will always be emitted after
	// creation. Listeners don't have to call `swa_window_refresh` from
	// within this function (or start drawing directly), backends will
	// send a `draw` event if needed.
	void (*resize)(struct swa_window*, unsigned width, unsigned height);
	// Called when the window state changes.
	// For newly created windows, this will be emitted only when
	// the initial state chosen by the system is different than the
	// state passed in `swa_window_settings`.
	void (*state)(struct swa_window*, enum swa_window_state state);

	// Called when the window receives or loses focus.
	// If the backend gives a newly created window immediately focus,
	// this will be called after window creation.
	// Note that the keys that are currently pressed while the window
	// receives focus can't be known. No key events are generated
	// and `swa_display_key_pressed` doesn't contain those keys.
	void (*focus)(struct swa_window*, bool gained);
	// Called when a key is pressed or released while the window has focus.
	void (*key)(struct swa_window*, const struct swa_key_event*);

	// Called when the mouse enters or leaves the window.
	void (*mouse_cross)(struct swa_window*, const struct swa_mouse_cross_event*);
	// Called when the mouse moves over the window.
	void (*mouse_move)(struct swa_window*, const struct swa_mouse_move_event*);
	// Called when a mouse button is pressed inside the window.
	void (*mouse_button)(struct swa_window*, const struct swa_mouse_button_event*);
	// Called when the mouse wheel is used.
	// dx describes the delta in horizontal direction (only != 0 for
	// special horizontal mouse wheels) while dy describes the delta in
	// vertical direction (this is not 0 for movement on "normal"
	// mouse wheels).
	// A delta of 1 or -1 represents one "tick" in positive or negative
	// direction, respectively.
	void (*mouse_wheel)(struct swa_window*, float dx, float dy);

	// Called when a new touch point is created.
	void (*touch_begin)(struct swa_window*, const struct swa_touch_event*);
	// Updates the position of a touch point.
	void (*touch_update)(struct swa_window*, const struct swa_touch_event*);
	// touch_end: ends a touch point. No further events for this
	// touch point will be generated.
	// id: the identification of the touch point as previously introduced
	//   by `touch_begin`.
	void (*touch_end)(struct swa_window*, unsigned id);
	// Cancels all currently active touch points.
	// Should be interpreted as a canceled gesture.
	void (*touch_cancel)(struct swa_window*);

	void (*dnd_enter)(struct swa_window*, const struct swa_dnd_event*);
	void (*dnd_move)(struct swa_window*, const struct swa_dnd_event*);
	void (*dnd_leave)(struct swa_window*, struct swa_data_offer*);
	void (*dnd_drop)(struct swa_window*, const struct swa_dnd_event*);

	void (*surface_destroyed)(struct swa_window*);
	void (*surface_created)(struct swa_window*);
};

struct swa_exchange_data {
	const char* data; // textual or raw data
	uint64_t size;
};

enum swa_data_action {
	swa_data_action_none = 0,
	swa_data_action_copy,
	swa_data_action_move,
};

struct swa_data_source_interface {
	void (*destroy)(struct swa_data_source*);
	const char** (*formats)(struct swa_data_source*, unsigned* count);
	struct swa_exchange_data (*data)(struct swa_data_source*, const char* format);
	struct swa_image (*image)(struct swa_data_source*);
	enum swa_data_action (*supported_actions)(struct swa_data_source*);
	void (*selected_action)(struct swa_data_source*, enum swa_data_action);
};

struct swa_data_source {
	struct swa_data_source_interface* impl;
};

// display api
// Automatically chooses a backend and creates a display for it.
// Returns NULL on error. The returned display must be destroyed using
// `swa_display_destroy`.
// The given `appname` will be used to identify the application with
// the backend's display server and can be NULL.
SWA_API struct swa_display* swa_display_autocreate(const char* appname);

// Destroys and frees the passed display. It must not be used after this.
SWA_API void swa_display_destroy(struct swa_display*);

// Reads and dispatches all available events.
// If 'block' is true and no event is currently available, will block
// until at least one event has been dispatched.
// Returns false if there was a critical error that means this display
// should be destroyed (e.g. connection to display server lost).
// It's not allowed to call this function recursively, i.e. from
// a callback triggered from this function.
SWA_API bool swa_display_dispatch(struct swa_display*, bool block);

// Can be used to wakeup `swa_display_wait_events` from another thread.
// Has no effect when `swa_display_wait_events` isn't currently called.
// Note that it never makes sense to call this from the same thread
// that called `swa_display_wait_events`.
SWA_API void swa_display_wakeup(struct swa_display*);

// Returns the capabilities of the passed display.
// See `swa_display_cap` for details.
SWA_API enum swa_display_cap swa_display_capabilities(struct swa_display*);

// Returns the vulkan instance extensions required by this backend to
// create vulkan surfaces.
// - count: the number of required extensions, i.e. the size of the
//   returned array pointer. Must not be NULL
// The returned array must not be freed, neither must the strings in it.
SWA_API const char** swa_display_vk_extensions(struct swa_display*, unsigned* count);


// Returns whether the given key is currently pressed.
// Backends may implement this in an asynchronous manner, i.e. this may
// return information for which no events were dispatched yet.
// Only valid if the display has the 'keyboard' capability.
SWA_API bool swa_display_key_pressed(struct swa_display*, enum swa_key);

// Returns the name that could be associated with the given keycode.
// Note that this isn't equivalent to the text input that should be generated
// when the given key is being pressed. It's rather an abstract label
// for this key that is nontheless dependent on the keyboard layout and
// could e.g. be shown to the user when configuring keyboard shortcuts.
// Might return NULL on failure or when there is no name for this keycode.
// The returned buffer must be freed.
// Only valid if the display has the 'keyboard' capability.
SWA_API const char* swa_display_key_name(struct swa_display*, enum swa_key);

// Returns all currently active keyboard modifiers.
// Backends may implement this in an asynchronous manner, i.e. this may
// return information for which no events were dispatched yet.
// Only valid if the display has the 'keyboard' capability.
SWA_API enum swa_keyboard_mod swa_display_active_keyboard_mods(struct swa_display*);

// Returns the `swa_window` that currently has keyboard focus or NULL
// if there is none.
// Backends may implement this in an asynchronous manner, i.e. this may
// return information for which no events were dispatched yet.
// Only valid if the display has the 'keyboard' capability.
SWA_API struct swa_window* swa_display_get_keyboard_focus(struct swa_display*);


// Returns whether the given mouse button is currently pressed.
// Backends may implement this in an asynchronous manner, i.e. this may
// return information for which no events were dispatched yet.
// Only valid if the display has the 'mouse' capability.
SWA_API bool swa_display_mouse_button_pressed(struct swa_display*, enum swa_mouse_button);

// Returns the last known mouse position in window-local coordinates.
// The window the mouse is currently over can be retrieved using
// `swa_display_get_mouse_over`. The values are not changed if
// the mouse is currently not over a swa_window.
// Backends may implement this in an asynchronous manner, i.e. this may
// return information for which no events were dispatched yet.
// Only valid if the display has the 'mouse' capability.
SWA_API void swa_display_mouse_position(struct swa_display*, int* x, int* y);

// Returns the `swa_window` that the mouse is currently over or NULL
// if there is none.
// Backends may implement this in an asynchronous manner, i.e. this may
// return information for which no events were dispatched yet.
// Only valid if the display has the 'mouse' capability.
SWA_API struct swa_window* swa_display_get_mouse_over(struct swa_display*);


// Returns a `swa_data_offer` representing the system clipboard.
// Returns NULL if the clipboard has no content.
// The returned object is only guaranteed to be valid until an event-dispatching
// display function is called the next time (or control returns there).
// Only valid if the display has the 'clipboard' capability.
SWA_API struct swa_data_offer* swa_display_get_clipboard(struct swa_display*);

// Requests the system to set the clipboard to the given data source.
// This call passes ownership of the given `swa_data_source` to the display.
// It must remain valid (and able to provide data) until the display destroys it.
// Will return whether setting the clipboard was succesful.
// Only valid if the display has the 'clipboard' capability.
SWA_API bool swa_display_set_clipboard(struct swa_display*, struct swa_data_source*);

// Will return whether starting dnd was succesful (not whether it
// was actually dropped anywhere, this information will be provided to the
// data source implementation). Note that on some implementations (mainly windows)
// this may block (but internally continue to dispatch events) until the dnd
// session is complete.
// Only valid if the display has the 'dnd' capability.
// The dnd session will be considered to be started by the last
// dispatched event, i.e. you want to call this directly from within
// the callback of the event that triggers this behavior.
SWA_API bool swa_display_start_dnd(struct swa_display*, struct swa_data_source*);

// Returns the default gl/gles api used on this display.
// Other values may be supported though.
// E.g. for an android backend this would return swa_api_gles (and context
// creation with swa_api_gl will fail) but on desktop swa_api_gl,
// even though on most linux system context creation with swa_api_gles
// will work as well.
// Only valid if the display has the 'gl' capability.
SWA_API enum swa_api swa_display_get_default_api(struct swa_display*);

// Returns the gl/gles proc address for the function with the given name.
// Must only be called while a context is current (see
// `swa_window_gl_make_current`) and the returned proc address may
// only be used with the current context.
// Returns NULL on failure.
// Only valid if the display has the 'gl' capability.
SWA_API swa_gl_proc swa_display_get_gl_proc_addr(struct swa_display*,
		const char* name);

// Creates a new window with the specified settings.
// To use the default settings, you probably want to initialize them
// using `swa_window_settings_default`.
// Might return NULL on failure.
// The created window must be destroyed using `swa_window_destroy`.
// On some platforms, the number of windows that can be created is strictly
// limited (e.g. to 1 or the number of available outputs).
SWA_API struct swa_window* swa_display_create_window(struct swa_display*,
	const struct swa_window_settings*);

// window api
SWA_API void swa_window_destroy(struct swa_window*);
SWA_API enum swa_window_cap swa_window_get_capabilities(struct swa_window*);
SWA_API void swa_window_set_min_size(struct swa_window*, unsigned w, unsigned h);
SWA_API void swa_window_set_max_size(struct swa_window*, unsigned w, unsigned h);

// Shows or hides the window.
// Only valid if the window has the window has the 'visibility' capability.
SWA_API void swa_window_show(struct swa_window*, bool show);

// Changes the size of a window.
// Calling this function will not emit a size event.
// It will usually emit a draw event though (but might not e.g. if the
// window is somehow hidden anyways).
// Only valid if the window has the window has the 'size' capability.
SWA_API void swa_window_set_size(struct swa_window*, unsigned w, unsigned h);

// If the given cursor is an image cursor, the stored image data
// will be copied or otherwise used, it must not remain valid after this call.
// Only valid if the window has the 'cursor' capability.
SWA_API void swa_window_set_cursor(struct swa_window*, struct swa_cursor cursor);

// Asks the backend to emit a draw event when it is a good time to redraw.
// Backends will internally try to implement redraw throttling, i.e.
// roughly synchronize redrawing with the monitor/compositor.
// This function will never call the draw handler itself but rather at
// least defer the event. This means, it is safe to call this function
// from the draw handler itself for continuous redrawing. But calling
// this inside the draw handler *before* the surface contents were
// changed (and swa_window_frame implicitly or explicitly triggered)
// is an error.
// If the window is currently hidden (e.g. on a
// different workspace or minimized or below another window), no
// draw event might be emitted at all.
// Important: See swa_window_surface_frame.
SWA_API void swa_window_refresh(struct swa_window*);

// Notifies the window to perform redraw throttling.
// When drawing on the window using vulkan or a platform-specific
// method, it is important to call swa_window_surface_frame before
// applying the new contents (i.e. presenting/commiting) to allow
// the implementation to implement redraw throttling. Otherwise
// the behavior of `swa_window_refresh` might be unexpected or suboptimal.
// Calling this without presenting afterwards is an error.
// If draw handlers aren't used, this isn't needed at all.
// When applying surface contents via 'swa_window_apply_buffer' or
// 'swa_window_gl_swap_buffers' this must not be called since those
// functions will trigger it implicitly.
SWA_API void swa_window_surface_frame(struct swa_window*);

// Changes the window state.
// Calling this function will not emit a state event.
// Calling this with the respective states is only valid if they
// are included in the window's capabilities.
SWA_API void swa_window_set_state(struct swa_window*, enum swa_window_state);

// Asks the system to start a session where the user moves the window.
// Useful when implementing client-side decorations.
// Only valid when the window has the 'begin_move' capability.
// The movement session will be considered to be started by the last
// dispatched event, i.e. you want to call this directly from within
// the callback of the event that triggers this behavior.
SWA_API void swa_window_begin_move(struct swa_window*);

// Asks the system to start a session where the user resizes the window.
// Useful when implementing client-side decorations.
// Only valid when the window has the 'begin_resize' capability.
// The resizing session will be considered to be started by the last
// dispatched event, i.e. you want to call this directly from within
// the callback of the event that triggers this behavior.
SWA_API void swa_window_begin_resize(struct swa_window*, enum swa_edge edges);

// Changes the window title. The given null-terminated string must be utf8.
// Only valid if the window has the 'title' capability.
SWA_API void swa_window_set_title(struct swa_window*, const char* utf8);

// The passed image must not remain valid after this call.
// Can pass NULL to unset the icon.
// Only valid if the window has the 'icon' capability.
SWA_API void swa_window_set_icon(struct swa_window*, const struct swa_image* image);

// Returns whether the window should be decorated by the user.
// This can either be the case because the backend uses client-side decorations
// by default or because the window was explicitly created with client decorations
// (and those are supported by the backend as well).
SWA_API bool swa_window_is_client_decorated(struct swa_window*);
SWA_API const struct swa_window_listener* swa_window_get_listener(struct swa_window*);

// Allows to set a word of custom data.
// Can be later on retrieved using `swa_window_get_userdata`.
// Mainly present for window listeners.
SWA_API void swa_window_set_userdata(struct swa_window*, void* data);

// Retrieves the data previously set with `swa_window_set_userdata`.
SWA_API void* swa_window_get_userdata(struct swa_window*);

// Returns the vulkan surface associated with this window or 0 on error.
// The returned value is a VkSurfaceKHR handle.
// Only valid if the window was created with surface set to `swa_surface_vk`.
// The surface will automatically be destroyed when the window is destroyed.
// Note that the surface might get lost on some platforms, signaled by the
// surface_destroyed event.
SWA_API uint64_t swa_window_get_vk_surface(struct swa_window*);

// Only valid if the window was created with surface set to `swa_surface_gl`.
SWA_API bool swa_window_gl_make_current(struct swa_window*);
SWA_API bool swa_window_gl_swap_buffers(struct swa_window*);
SWA_API bool swa_window_gl_set_swap_interval(struct swa_window*, int interval);

// Only valid if the window was created with surface set to `swa_surface_buffer`.
// Implementations might use multiple buffers to avoid flickering, i.e.
// the caller should not expect two calls to `get_buffer` ever to
// return the same image.
// Returns false on error, in this case no valid image is returned
// and `swa_window_apply_buffer` must not be called.
// The returned image data is only valid until events are dispatched the
// next time. Therefore, if this function returns succesfully
// the caller must call swa_window_apply_buffer before dispatching
// events again.
SWA_API bool swa_window_get_buffer(struct swa_window*, struct swa_image*);

// Only valid if the window was created with surface set to `buffer`.
// Sets the window contents to the image data stored in the image
// returned by the last call to `get_buffer`.
// For each call of `apply_buffer`, there must have been a previous
// call to `get_buffer`.
SWA_API void swa_window_apply_buffer(struct swa_window*);

// data offers
typedef void (*swa_formats_handler)(struct swa_data_offer*,
	const char** formats, unsigned n_formats);

// When called, ownership of the data is transferred to the
// application, i.e. it is reponsible for freeing it.
// Safe to free data offer from this callback
typedef void (*swa_data_handler)(struct swa_data_offer*,
	const char* format, struct swa_exchange_data);

SWA_API void swa_data_offer_destroy(struct swa_data_offer*);

// Requests all formats in which this data offer can provide its data.
// The given callback will be called when the formats were retrieved.
// Returns false on error. When an error occurs later, will
// call the given callback without any formats (n_formats = 0).
// At any time, there can only be one formats request, i.e. calling
// this again before the callback was triggered is an error.
// Implementations may call the callback from within this function, if
// the format list is immediately available.
SWA_API bool swa_data_offer_formats(struct swa_data_offer*, swa_formats_handler cb);

// Requests the data_offer's data in a specific format.
// The given callback will be called with the data when retrieved.
// Returns false on error. When an error occurs later, will call the
// given callback without any data.
// Only one data request at a time is allowed, i.e. calling this again
// before the callback was triggered is an error. The format parameter
// will be copied and doesn't have to remain valid.
// Implementations might call the callback before returning, if
// the data is immediately available.
SWA_API bool swa_data_offer_data(struct swa_data_offer*, const char* format,
	swa_data_handler cb);

// Sets the preferred format and action on a data offer.
// Only relevant and expected to be used for data offers introduced
// by dnd events.
// Pass format = NULL or action = action_none to signal that this
// offer can't be handled (can be in response to the current dnd position).
SWA_API void swa_data_offer_set_preferred(struct swa_data_offer*, const char* format,
	enum swa_data_action action);

// Returns the currently selected action for the given data offer.
SWA_API enum swa_data_action swa_data_offer_action(struct swa_data_offer*);

// Returns all supported actions by this offer.
SWA_API enum swa_data_action swa_data_offer_supported_actions(struct swa_data_offer*);
SWA_API void swa_data_offer_set_userdata(struct swa_data_offer*, void*);
SWA_API void* swa_data_offer_get_userdata(struct swa_data_offer*);

// utility
// Initializes the given settings to the default state.
// Will especially set width, height to SWA_DEFAULT_SIZE and x, y
// to SWA_DEFAULT_POSITION. Will otherwise memset the settings
// to 0. Will not specify any surface to create.
SWA_API void swa_window_settings_default(struct swa_window_settings*);

// Returns the name of the given key enumeration (null-terminated).
// For example, swa_key_to_name(swa_key_tab) returns "tab".
// Returns "<invalid>" if the key isn't known.
SWA_API const char* swa_key_to_name(enum swa_key);

// Tries to find a swa_key enumeration value for the given key name.
// For exapmle, swa_key_from_name("w") returns swa_key_w.
// If no matching swa_key exists, returns swa_key_none.
SWA_API enum swa_key swa_key_from_name(const char* name);

// Returns whether the key is textual, i.e. is usually textually
// represented. Returns true for keys like swa_key_w, swa_key_k0
// or swa_key_leftbrace while it returns false for keys like swa_key_enter,
// swa_key_backspace, swa_key_leftctrl, swa_key_f1 or swa_key_left.
// Whether or not a key is textual can depend on the circumstance (e.g.
// you might want to interpret keys like enter as textual).
SWA_API bool swa_key_is_textual(enum swa_key key);

#ifdef __cplusplus
}
#endif
