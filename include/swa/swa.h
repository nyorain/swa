#pragma once

#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
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
struct swa_image;

// When these values are specified as size/position respectively
// in swa_window_settings, the system default will be used.
// If the system has no default, will use the SWA_FALLBACK_* values
#define SWA_DEFAULT_SIZE 0
#define SWA_DEFAULT_POSITION INT_MAX

#define SWA_FALLBACK_WIDTH 800
#define SWA_FALLBACK_HEIGHT 500

// Describes the layout of pixel color values in the data
// buffer of a swa_image.
// Example: rgba32 means that a pixel uses 32 bits (4 bytes),
// where the lowest 8 bit contain the 'red' value and the
// highest 8 bit contain the 'alpha' value.
// This means the representation is dependent on the endianess
// of the system, but the components can (independent
// from endianess) always be extracted using logical C operations.
// This definition is consistent with OpenGL, Vulkan and Cairo,
// but different to SDL or wayland.
enum swa_image_format {
    swa_image_format_none = 0,
    swa_image_format_a8,
    swa_image_format_rgba32,
    swa_image_format_rgb24,
    swa_image_format_argb32,
    swa_image_format_xrgb32,
};

// Describes the kind of render surface created for a window.
enum swa_window_surface {
    swa_window_surface_none = 0,
    swa_window_surface_buffer,
    swa_window_surface_gl,
    swa_window_surface_vk,
};

// Describes a 2 dimensional image.
struct swa_image {
    unsigned width, height;
    unsigned stride; // in bytes
    enum swa_image_format format;
    const char* data;
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
	swa_window_cap_position = (1L << 5),
	swa_window_cap_size_limits = (1L << 6),
	swa_window_cap_icon = (1L << 7),
	swa_window_cap_cursor = (1L << 8),
	swa_window_cap_title = (1L << 9),
	swa_window_cap_begin_move = (1L << 10),
	swa_window_cap_begin_resize = (1L << 11),
	swa_window_cap_visibility = (1L << 12),
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

// Defines a cursor.
// hx, hy define the cursor hotspot in local coordinates.
// The hotspot and image members are only relevant
// when type is 'image'.
struct swa_cursor {
    enum swa_cursor_type type;
    int hx, hy;
    struct swa_image image;
};

// TODO
struct swa_gl_surface_settings {
    unsigned major, minor;
	struct swa_window* reuse_context;
};

// The instance must remain valid until the window is destroyed.
struct swa_vk_surface_settings {
    uint64_t instance; // type: vkInstance
};

struct swa_window_settings {
    unsigned width, height;
    int x, y;
	const char* app_name;
    const char* title;
    struct swa_cursor cursor;

    enum swa_window_surface surface;
    union {
		// allow preferred format for buffer surface?
        struct swa_gl_surface_settings gl;
        struct swa_vk_surface_settings vk;
    } surface_settings;

    bool hide; // create the window in a hidden state
    bool transparent; // allow transparency
    enum swa_window_state state;
    enum swa_preference client_decorate;
	const struct swa_window_listener* listener;
};

struct swa_size_event {
    unsigned width;
    unsigned height;
};

struct swa_key_event {
    const char* utf8;
    enum swa_key keycode;
    enum swa_keyboard_mod modifiers;
    bool pressed;
    bool repeated;
    void* data;
};

struct swa_mouse_button_event {
    enum swa_mouse_button button;
    bool pressed;
    void *data;
};

struct swa_mouse_move_event {
    int x, y;
    int dx, dy;
};

struct swa_dnd_event {
    struct swa_data_offer* offer;
    int x, y;
};

struct swa_window_listener {
    void (*draw)(struct swa_window*);
    void (*close)(struct swa_window*);
    void (*destroyed)(struct swa_window*);

    void (*resize)(struct swa_window*, unsigned width, unsigned height);
    void (*state)(struct swa_window*, enum swa_window_state state);

    void (*focus)(struct swa_window*, bool gained);
    void (*key)(struct swa_window*, const struct swa_key_event*);

    void (*mouse_cross)(struct swa_window*, bool entered);
    void (*mouse_move)(struct swa_window*, const struct swa_mouse_move_event*);
    void (*mouse_button)(struct swa_window*, const struct swa_mouse_button_event*);
    void (*mouse_wheel)(struct swa_window*, int dx, int dy);

    void (*touch_begin)(struct swa_window*, unsigned id, int x, int y);
    void (*touch_update)(struct swa_window*, unsigned id, int x, int y);
    void (*touch_end)(struct swa_window*, unsigned id);
    void (*touch_cancel)(struct swa_window*);

    void (*dnd_enter)(struct swa_window*, const struct swa_dnd_event*);
    void (*dnd_move)(struct swa_window*, const struct swa_dnd_event*);
    void (*dnd_leave)(struct swa_window*, struct swa_data_offer*);
    void (*dnd_drop)(struct swa_window*, const struct swa_dnd_event*);

    void (*surface_destroyed)(struct swa_window*);
    void (*surface_created)(struct swa_window*);
};

union swa_exchange_data {
    const char* text;
    const char** text_list;
    struct swa_image image;
};

enum swa_data_action {
    none,
    copy,
    move,
};

struct swa_data_source_interface {
    void (*destroy)(struct swa_data_source*);
    const char** (*formats)(struct swa_data_source*, unsigned* count);
    union swa_exchange_data (*data)(struct swa_data_source*, const char* format);
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
struct swa_display* swa_display_autocreate(void);

// Destroys and frees the passed display. It must not be used after this.
void swa_display_destroy(struct swa_display*);

// Reads and dispatches all available events without waiting for new ones.
// Returns false if there was a critical error that means this display
// should be destroyed (e.g. connection to display server lost).
bool swa_display_poll_events(struct swa_display*);

// Read and dispatches all available events.
// If and only if none are available at the moment, will wait until
// at least one event could be dispatched.
// Returns false if there was a critical error that means this display
// should be dstroyed (e.g. connection to display server lost).
bool swa_display_wait_events(struct swa_display*);

// Can be used to wakeup `swa_display_wait_events` from another thread.
// Has no effect when `swa_display_wait_events` isn't currently called.
// Note that it never makes sense to call this from the same thread
// that called `swa_display_wait_events`.
void swa_display_wakeup(struct swa_display*);

// Returns the capabilities of the passed display.
// See `swa_display_cap` for details.
enum swa_display_cap swa_display_capabilities(struct swa_display*);

// Returns the vulkan instance extensions required by this backend to
// create vulkan surfaces.
// - count: the number of required extensions, i.e. the size of the
//   returned array pointer. Must not be NULL
const char** swa_display_vk_extensions(struct swa_display*, unsigned* count);


// Returns whether the given key is currently pressed.
// Only valid if the display has the 'keyboard' capability.
bool swa_display_key_pressed(struct swa_display*, enum swa_key);

// Returns the name that could be associated with the given keycode.
// Note that this isn't equivalent to the text input that should be generated
// when the given key is being pressed. It's rather an abstract label
// for this key that is nontheless dependent on the keyboard layout and
// could e.g. be shown to the user when configuring keyboard shortcuts.
// Might return NULL on failure or when there is no name for this keycode.
// Only valid if the display has the 'keyboard' capability.
const char* swa_display_key_name(struct swa_display*, enum swa_key);

// Returns all currently active keyboard modifiers.
// Only valid if the display has the 'keyboard' capability.
enum swa_keyboard_mod swa_display_active_keyboard_mods(struct swa_display*);

// Returns the `swa_window` that currently has keyboard focus or NULL
// if there is none.
// Only valid if the display has the 'keyboard' capability.
struct swa_window* swa_display_get_keyboard_focus(struct swa_display*);


// Returns whether the given mouse button is currently pressed.
// Only valid if the display has the 'mouse' capability.
bool swa_display_mouse_button_pressed(struct swa_display*, enum swa_mouse_button);

// Returns the last known mouse position in window-local coordinates.
// The window the mouse is currently over can be retrieved using
// `swa_display_get_mouse_over`.
// Only valid if the display has the 'mouse' capability.
void swa_display_mouse_position(struct swa_display*, int* x, int* y);

// Returns the `swa_window` that the mouse is currently over or NULL
// if there is none.
// Only valid if the display has the 'mouse' capability.
struct swa_window* swa_display_get_mouse_over(struct swa_display*);


// Returns a `swa_data_offer` representing the system clipboard.
// Returns NULL if the clipboard has no content.
// The returned object is only guaranteed to be valid until an event-dispatching
// display function is called the next time (or control returns there).
// Only valid if the display has the 'clipboard' capability.
struct swa_data_offer* swa_display_get_clipboard(struct swa_display*);

// Requests the system to set the clipboard to the given data source.
// This call passes ownership of the given `swa_data_source` to the display.
// It must remain valid (and able to provide data) until the display destroys it.
// The `trigger_event_data` parameter should be set to the event data pointer
// provided with the event that triggered this action, as that is needed by some
// systems. Will return whether setting the clipboard was succesful.
// Only valid if the display has the 'clipboard' capability.
bool swa_display_set_clipboard(struct swa_display*, struct swa_data_source*, void* trigger_event_data);

// The `trigger_event_data` parameter should be set to the event data pointer
// provided with the event that triggered this action, as that is needed by some
// systems. Will return whether starting dnd was succesful (not whether it
// was actually dropped anywhere, this information will be provided to the
// data source implementation). Note that on some implementations (mainly windows)
// this may block (but internally continue to dispatch events) until the dnd
// session is complete.
// Only valid if the display has the 'dnd' capability.
bool swa_display_start_dnd(struct swa_display*, struct swa_data_source*, void* trigger_event_data);

// Creates a new window with the specified settings.
// To use the default settings, you probably want to initialize them
// using `swa_window_settings_default`.
// Might return NULL on failure.
// The created window must be destroyed using `swa_window_destroy`.
// On some platforms, the number of windows that can be created is strictly
// limited (e.g. to 1 or the number of available outputs).
struct swa_window* swa_display_create_window(struct swa_display*,
	struct swa_window_settings);

// Initializes the given settings to the default state.
// Will especially set width, height to SWA_DEFAULT_SIZE and x, y
// to SWA_DEFAULT_POSITION. Will otherwise memset the settings
// to 0. Will not specify any surface to create.
void swa_window_settings_default(struct swa_window_settings*);

// window api
void swa_window_destroy(struct swa_window*);
enum swa_window_cap swa_window_get_capabilities(struct swa_window*);
void swa_window_set_min_size(struct swa_window*, unsigned w, unsigned h);
void swa_window_set_max_size(struct swa_window*, unsigned w, unsigned h);
void swa_window_show(struct swa_window*, bool show);

// Calling this function will not emit a size event.
void swa_window_set_size(struct swa_window*, unsigned w, unsigned h);
void swa_window_set_position(struct swa_window*, int x, int y);
void swa_window_set_cursor(struct swa_window*, struct swa_cursor cursor);
void swa_window_refresh(struct swa_window*);
void swa_window_surface_frame(struct swa_window*);

// Calling this function will not emit a state event.
void swa_window_set_state(struct swa_window*, enum swa_window_state);
void swa_window_begin_move(struct swa_window*, void* trigger_event_data);
void swa_window_begin_resize(struct swa_window*, enum swa_edge edges, void* trigger_event_data);
void swa_window_set_title(struct swa_window*, const char*);

// The passed image must not remain valid after this call.
// Can pass NULL to unset the icon.
void swa_window_set_icon(struct swa_window*, const struct swa_image* image);

// Returns whether the window should be decorated by the user.
// This can either be the case because the backend uses client-side decorations
// by default or because the window was explicitly created with client decorations
// (and those are supported by the backend as well).
bool swa_window_is_client_decorated(struct swa_window*);

// The objects must remain valid until it is changed or the window is destroyed.
void swa_window_set_listener(struct swa_window*, const struct swa_window_listener*);
void swa_window_set_userdata(struct swa_window*, void* data);
void* swa_window_get_userdata(struct swa_window*);

// Only valid if the window was created with surface set to vk.
// The surface will automatically be destroyed when the window is destroyed.
// Note that the surface might get lost on some platforms, signaled by the
// surface_destroyed event.
bool swa_window_get_vk_surface(struct swa_window*, void* vkSurfaceKHR);

// Only valid if the window was created with surface set to gl.
bool swa_window_gl_make_current(struct swa_window*);
bool swa_window_gl_swap_buffers(struct swa_window*);
bool swa_window_gl_set_swap_interval(struct swa_window*);

// Only valid if the window was created with surface set to buffer.
// Implementations might use multiple buffers to avoid flickering, i.e.
// the caller should not expect two calls to `get_buffer` ever to
// return the same image.
bool swa_window_get_buffer(struct swa_window*, struct swa_image*);

// Sets the window contents to the image data stored in the image
// returned by the last call to `get_buffer`.
// For each call of `apply_buffer`, there must have been a previous
// call to `get_buffer`.
void swa_window_apply_buffer(struct swa_window*);

// data offers
typedef void (*swa_formats_handler)(const char** formats, unsigned count, void* data);
typedef void (*swa_data_handler)(union swa_exchange_data, void* data);

bool swa_data_offer_types(struct swa_data_offer*, swa_formats_handler cb, void* data);
bool swa_data_offer_data(struct swa_data_offer*, const char* format, swa_data_handler cb, void* data);
void swa_data_offer_set_preferred(struct swa_data_offer*, const char* foramt, enum swa_data_action action);
enum swa_data_action swa_data_offer_action(struct swa_data_offer*);
enum swa_data_action swa_data_offer_supported_actions(struct swa_data_offer*);

#ifdef __cplusplus
}
#endif
