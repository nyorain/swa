#include <swa/swa.h>
#include <dlg/dlg.h>
#include <stdlib.h>

static const struct {
	enum swa_cursor_type cursor;
	const char* name[5];
} cursor_map[] = {
	{swa_cursor_left_pointer, {"left_ptr"}},
	{swa_cursor_load, {"watch"}},
	{swa_cursor_load_pointer, {"left_ptr_watch"}},
	{swa_cursor_right_pointer, {"right_ptr"}},
	{swa_cursor_hand, {"pointer"}},
	{swa_cursor_grab, {"grab"}},
	{swa_cursor_crosshair, {"cross"}},
	{swa_cursor_help, {"question_arrow"}},
	{swa_cursor_beam, {"xterm"}},
	{swa_cursor_forbidden, {"crossed_circle"}},
	{swa_cursor_size, {"bottom_left_corner"}},
	{swa_cursor_size_bottom, {"bottom_side"}},
	{swa_cursor_size_bottom_left, {"bottom_left_corner"}},
	{swa_cursor_size_bottom_right, {"bottom_right_corner"}},
	{swa_cursor_size_top, {"top_side"}},
	{swa_cursor_size_top_left, {"top_left_corner"}},
	{swa_cursor_size_top_right, {"top_right_corner"}},
	{swa_cursor_size_left, {"left_side"}},
	{swa_cursor_size_right, {"right_side"}},
};

const char* const* swa_get_xcursor_names(enum swa_cursor_type type) {
	dlg_assert(type != swa_cursor_unknown);
	dlg_assert(type != swa_cursor_default);

	const unsigned count = sizeof(cursor_map) / sizeof(cursor_map[0]);
	for(unsigned i = 0u; i < count; ++i) {
		if(cursor_map[i].cursor == type) {
			return cursor_map[i].name;
		}
	}

	return NULL;
}

