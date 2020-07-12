Alternative event model, where applications poll/wait for events
instead of getting them per callback:

```
enum swa_event_type {
	// window events
	swa_event_type_draw, // swa_window_event
	swa_event_type_close, // swa_window_event
	swa_event_type_destroyed, // swa_window_event
	swa_event_type_resize,
	swa_event_type_state,
	swa_event_type_focus,
	swa_event_type_key,
	swa_event_type_mouse_cross,
	swa_event_type_mouse_move,
	swa_event_type_mouse_button,
	swa_event_type_mouse_wheel,
	swa_event_type_touch_begin,
	swa_event_type_touch_end,
	swa_event_type_touch_cancel,
	swa_event_type_dnd_enter,
	swa_event_type_dnd_move,
	swa_event_type_dnd_leave,
	swa_event_type_dnd_drop,
	swa_event_type_surface_destroyed,
	swa_event_type_surface_created,

	// data offer events
	swa_event_type_data_offer_formats,
	swa_event_type_data_offer_data,
};

struct swa_event {
	enum swa_event_type type;
};

struct swa_window_event {
	struct swa_event base;
	struct swa_window* window;
};

struct swa_resize_event {
	struct swa_window_event base;
	unsigned width;
	unsigned height;
};

struct swa_state_event {
	struct swa_window_event base;
	enum swa_window_state state;
};

struct swa_focus_event {
	struct swa_window_event base;
	bool gained;
};

struct swa_key_event {
	struct swa_window_event base;
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

struct swa_mouse_wheel_event {
	struct swa_window_event base;
	float dx;
	float dy;
};

struct swa_mouse_button_event {
	struct swa_window_event base;
	// The new mouse position in window-local coordinates.
	int x, y;
	// The button that was pressed or released.
	enum swa_mouse_button button;
	// Whether the button was pressed or released.
	bool pressed;
};

struct swa_mouse_move_event {
	struct swa_window_event base;
	// The new mouse position in window-local coordinates.
	int x, y;
	// The delta, i.e. the current mouse position minus the last
	// known position in window-local cordinates.
	int dx, dy;
};

struct swa_mouse_cross_event {
	struct swa_window_event base;
	// Whether the mouse entered or left the window.
	bool entered;
	// The position of the mouse in window-local coordinates.
	int x, y;
};

struct swa_dnd_event {
	struct swa_window_event base;
	// The data offer associated with this dnd session.
	// Guaranteed to be valid until `dnd_leave` is called.
	// When `dnd_drop` is called, ownership of this offer is transferred
	// to the application, i.e. it must free it after being finished
	// using it.
	struct swa_data_offer* offer;
	// Position of the dnd item in window-local coorindates.
	// This must not be provided for swa_event_type_dnd_leave.
	int x, y;
};

struct swa_touch_event {
	struct swa_window_event base;
	// Identification of the point. This id will passed to further
	// touch events and can be used to identify this touch point.
	// Touch point ids are unique as long as they exist (between `touch_begin`
	// and `touch_end` or `touch_cancel`) but might be reused after that.
	unsigned id;
	// Position of the touch point in window-local coordinates.
	// This must not be provided for swa_event_type_touch_end.
	int x, y;
};

// Retrieves a single event from the given display.
// Will simply return the first event in the event queue. When the queue is
// empty, will either wait for an event (for wait = true) or return NULL
// (for wait = false).
// The returned event must not be free'd but it will only remain valid
// until the next time a function on this display, an associated window
// or data offer is called.
struct swa_event* swa_display_get_event(struct swa_display* dpy, bool wait);
```
