#include <swa/private/xkb.h>
#include <dlg/dlg.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <locale.h>
#include <string.h>

bool swa_xkb_init_default(struct swa_xkb_context* xkb) {
	xkb->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if(!xkb->context) {
		dlg_error("xkb_context_new failed");
		return false;
	}

	struct xkb_rule_names rules = {0};
	rules.rules = getenv("XKB_DEFAULT_RULES");
	rules.model = getenv("XKB_DEFAULT_MODEL");
	rules.layout = getenv("XKB_DEFAULT_LAYOUT");
	rules.variant = getenv("XKB_DEFAULT_VARIANT");
	rules.options = getenv("XKB_DEFAULT_OPTIONS");

	xkb->keymap = xkb_map_new_from_names(xkb->context, &rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if(!xkb->keymap) {
		dlg_error("xkb_map_new_from_names failed");
		return false;
	}

	xkb->state = xkb_state_new(xkb->keymap);
	if(!xkb->state) {
		dlg_error("xkb_state_new failed");
		return false;
	}

	return true;
}

bool swa_xkb_init_compose(struct swa_xkb_context* xkb) {
	// Although named 'setlocale', this will only query the
	// current locale. Recommended way by xkbommon
	// https://xkbcommon.org/doc/current/group__compose.html
	const char* locale = setlocale(LC_CTYPE, NULL);
	if(!locale) {
		dlg_error("Couldn't query locale using setlocale");
		return false;
	}

	xkb->compose_table = xkb_compose_table_new_from_locale(xkb->context,
		locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
	if(!xkb->compose_table) {
		dlg_error("xkb_compose_table_new failed");
		return false;
	}

	xkb->compose_state = xkb_compose_state_new(xkb->compose_table,
		XKB_COMPOSE_STATE_NO_FLAGS);
	if(!xkb->compose_state) {
		dlg_error("xkb_compose_state_new failed");
		return false;
	}

	return true;
}

void swa_xkb_finish(struct swa_xkb_context* xkb) {
	if(xkb->compose_table) xkb_compose_table_unref(xkb->compose_table);
	if(xkb->compose_state) xkb_compose_state_unref(xkb->compose_state);

	if(xkb->state) xkb_state_unref(xkb->state);
	if(xkb->keymap) xkb_keymap_unref(xkb->keymap);
	if(xkb->context) xkb_context_unref(xkb->context);
	memset(xkb, 0, sizeof(*xkb));
}

enum swa_keyboard_mod swa_xkb_modifiers_state(struct xkb_state* state) {
	struct {
		const char* xkb;
		enum swa_keyboard_mod modifier;
	} map[] = {
		{XKB_MOD_NAME_SHIFT, swa_keyboard_mod_shift},
		{XKB_MOD_NAME_CAPS, swa_keyboard_mod_caps_lock},
		{XKB_MOD_NAME_CTRL, swa_keyboard_mod_ctrl},
		{XKB_MOD_NAME_ALT, swa_keyboard_mod_alt},
		{XKB_MOD_NAME_NUM, swa_keyboard_mod_num_lock},
		{XKB_MOD_NAME_LOGO, swa_keyboard_mod_super}
	};

	enum swa_keyboard_mod ret = swa_keyboard_mod_none;
	const unsigned count = sizeof(map) / sizeof(map[0]);
	for(unsigned i = 0u; i < count; ++i) {
		bool active = xkb_state_mod_name_is_active(state,
			map[i].xkb, XKB_STATE_MODS_EFFECTIVE);
		if(active) {
			ret |= map[i].modifier;
		}
	}

	return ret;
}

enum swa_keyboard_mod swa_xkb_modifiers(struct swa_xkb_context* xkb) {
	return swa_xkb_modifiers_state(xkb->state);
}

void swa_xkb_key(struct swa_xkb_context* xkb, uint8_t keycode,
		char** out_utf8, bool* canceled) {
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb->state, keycode);
	*canceled = false;
	*out_utf8 = NULL;

	xkb_compose_state_feed(xkb->compose_state, keysym);
	enum xkb_compose_status status = xkb_compose_state_get_status(xkb->compose_state);
	if(status == XKB_COMPOSE_NOTHING) {
		unsigned count = xkb_state_key_get_utf8(xkb->state, keycode, NULL, 0);
		if(count > 0) {
			*out_utf8 = malloc(count + 1);
			xkb_state_key_get_utf8(xkb->state, keycode, *out_utf8, count + 1);
		}
	} else if(status == XKB_COMPOSE_COMPOSED) {
		unsigned count = xkb_compose_state_get_utf8(xkb->compose_state, NULL, 0);
		if(count > 0) {
			*out_utf8 = malloc(count + 1);
			xkb_compose_state_get_utf8(xkb->compose_state, *out_utf8, count + 1);
			xkb_compose_state_reset(xkb->compose_state);
		}
	} else if(status == XKB_COMPOSE_CANCELLED) {
		xkb_compose_state_reset(xkb->compose_state);
		*canceled = true;
	}
}

const char* swa_xkb_key_name_keymap(struct xkb_keymap* keymap, enum swa_key key) {
	uint8_t code = (uint8_t)key - 8;
	// temporary dummy state, nothing pressed
	struct xkb_state* state = xkb_state_new(keymap);
	if(!state) {
		dlg_error("xkb_state_new failed");
		return NULL;
	}

	unsigned count = xkb_state_key_get_utf8(state, code, NULL, 0) + 1;
	char* name = malloc(count);
	xkb_state_key_get_utf8(state, code, name, count + 1);
	xkb_state_unref(state);
	return name;
}

const char* swa_xkb_key_name(struct swa_xkb_context* xkb, enum swa_key key) {
	return swa_xkb_key_name_keymap(xkb->keymap, key);
}

void swa_xkb_update_state(struct swa_xkb_context* xkb, int mods[3],
		int layouts[3]) {
	xkb_state_update_mask(xkb->state,
		mods[0], mods[1], mods[2],
		layouts[0], layouts[1], layouts[2]);
}
