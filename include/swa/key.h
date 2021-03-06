#pragma once

// Modeles after linux keycodes
enum swa_key {
	swa_key_none = 0,
	swa_key_escape,

	swa_key_k1,
	swa_key_k2,
	swa_key_k3,
	swa_key_k4,
	swa_key_k5,
	swa_key_k6,
	swa_key_k7,
	swa_key_k8,
	swa_key_k9,
	swa_key_k0,
	swa_key_minus,
	swa_key_equals,
	swa_key_backspace,
	swa_key_tab,

	swa_key_q,
	swa_key_w,
	swa_key_e,
	swa_key_r,
	swa_key_t,
	swa_key_y,
	swa_key_u,
	swa_key_i,
	swa_key_o,
	swa_key_p,
	swa_key_leftbrace,
	swa_key_rightbrace,
	swa_key_enter,
	swa_key_leftctrl,

	swa_key_a,
	swa_key_s,
	swa_key_d,
	swa_key_f,
	swa_key_g,
	swa_key_h,
	swa_key_j,
	swa_key_k,
	swa_key_l,
	swa_key_semicolon,
	swa_key_apostrophe,
	swa_key_grave,
	swa_key_leftshift,
	swa_key_backslash,

	swa_key_z,
	swa_key_x,
	swa_key_c,
	swa_key_v,
	swa_key_b,
	swa_key_n,
	swa_key_m,
	swa_key_comma,
	swa_key_period,
	swa_key_slash,
	swa_key_rightshift,
	swa_key_kpmultiply,
	swa_key_leftalt,
	swa_key_space,
	swa_key_capslock,

	swa_key_f1,
	swa_key_f2,
	swa_key_f3,
	swa_key_f4,
	swa_key_f5,
	swa_key_f6,
	swa_key_f7,
	swa_key_f8,
	swa_key_f9,
	swa_key_f10,

	swa_key_numlock,
	swa_key_scrollock,
	swa_key_kp7,
	swa_key_kp8,
	swa_key_kp9,
	swa_key_kpminus,
	swa_key_kp4,
	swa_key_kp5,
	swa_key_kp6,
	swa_key_kpplus,
	swa_key_kp1,
	swa_key_kp2,
	swa_key_kp3,
	swa_key_kp0,
	swa_key_kpperiod,

	swa_key_zenkakuhankaku = 85,
	swa_key_102nd, // OEM 102/non-us hash
	swa_key_f11,
	swa_key_f12,
	swa_key_ro,
	swa_key_katakana,
	swa_key_hiragana,
	swa_key_henkan,
	swa_key_katakanahiragana,
	swa_key_muhenkan,
	swa_key_kpjpcomma,
	swa_key_kpenter,
	swa_key_rightctrl,
	swa_key_kpdivide,
	swa_key_sysrq,
	swa_key_rightalt,
	swa_key_linefeed,
	swa_key_home,
	swa_key_up,
	swa_key_pageup,
	swa_key_left,
	swa_key_right,
	swa_key_end,
	swa_key_down,
	swa_key_pagedown,
	swa_key_insert,
	swa_key_del,
	swa_key_macro,
	swa_key_mute,
	swa_key_volumedown,
	swa_key_volumeup,
	swa_key_power,
	swa_key_kpequals,
	swa_key_kpplusminus,
	swa_key_pause,
	swa_key_scale,
	swa_key_kpcomma,
	swa_key_hangeul,
	swa_key_hanguel = swa_key_hangeul,
	swa_key_hanja,
	swa_key_yen,
	swa_key_leftmeta, // aka leftsuper
	swa_key_rightmeta, // aka rightsuper
	swa_key_compose,
	swa_key_stop,
	swa_key_again,
	swa_key_props,
	swa_key_undo,
	swa_key_front,
	swa_key_copy,
	swa_key_open,
	swa_key_paste,
	swa_key_find,
	swa_key_cut,
	swa_key_help,
	swa_key_menu,
	swa_key_calc,
	swa_key_setup,
	swa_key_sleep,
	swa_key_wakeup,
	swa_key_file,
	swa_key_sendfile,
	swa_key_deletefile,
	swa_key_xfer,
	swa_key_prog1,
	swa_key_prog2,
	swa_key_www,
	swa_key_msdos,
	swa_key_coffee,
	swa_key_screenlock = swa_key_coffee,
	swa_key_rotate_display,
	swa_key_direction = swa_key_rotate_display,
	swa_key_cyclewindows,
	swa_key_mail,
	swa_key_bookmarks,
	swa_key_computer,
	swa_key_back,
	swa_key_forward,
	swa_key_closecd,
	swa_key_ejectcd,
	swa_key_ejectclosecd,
	swa_key_nextsong,
	swa_key_playpause,
	swa_key_previoussong,
	swa_key_stopcd,
	swa_key_record,
	swa_key_rewind,
	swa_key_phone,
	swa_key_iso,
	swa_key_config,
	swa_key_homepage,
	swa_key_refresh,
	swa_key_exit,
	swa_key_move,
	swa_key_edit,
	swa_key_scrollup,
	swa_key_scrolldown,
	swa_key_kpleftparen,
	swa_key_kprightparen,
	swa_key_knew,
	swa_key_redo,
	swa_key_f13,
	swa_key_f14,
	swa_key_f15,
	swa_key_f16,
	swa_key_f17,
	swa_key_f18,
	swa_key_f19,
	swa_key_f20,
	swa_key_f21,
	swa_key_f22,
	swa_key_f23,
	swa_key_f24,

	swa_key_playcd = 200,
	swa_key_pausecd,
	swa_key_prog3,
	swa_key_prog4,
	swa_key_dashboard,
	swa_key_suspend,
	swa_key_close,
	swa_key_play,
	swa_key_fastforward,
	swa_key_bassboost,
	swa_key_print,
	swa_key_hp,
	swa_key_camera,
	swa_key_sound,
	swa_key_question,
	swa_key_email,
	swa_key_chat,
	swa_key_search,
	swa_key_connect,
	swa_key_finance,
	swa_key_sport,
	swa_key_shop,
	swa_key_alterase,
	swa_key_cancel,
	swa_key_brightnessdown,
	swa_key_brightnessup,
	swa_key_media,
	swa_key_switchvideomode,
	swa_key_kbdillumtoggle,
	swa_key_kbdillumdown,
	swa_key_kbdillumup,
	swa_key_send,
	swa_key_reply,
	swa_key_forwardmail,
	swa_key_save,
	swa_key_documents,
	swa_key_battery,
	swa_key_bluetooth,
	swa_key_wlan,
	swa_key_uwb,
	swa_key_unknown,
	swa_key_video_next,
	swa_key_video_prev,
	swa_key_brightness_cycle,
	swa_key_brightness_auto,
	swa_key_brightness_zero = swa_key_brightness_auto,
	swa_key_display_off,
	swa_key_wwan,
	swa_key_wimax = swa_key_wwan,
	swa_key_rfkill,
	swa_key_micmute,

	// extra keycodes that are usually not used in any way and just here for completeness.
	swa_key_ok = 352,
	swa_key_select,
	swa_key_kgoto,
	swa_key_clear,
	swa_key_power2,
	swa_key_option,
	swa_key_info,
	swa_key_time,
	swa_key_vendor,
	swa_key_archive,
	swa_key_program,
	swa_key_channel,
	swa_key_favorites,
	swa_key_epg,
	swa_key_pvr,
	swa_key_mhp,
	swa_key_language,
	swa_key_title,
	swa_key_subtitle,
	swa_key_angle,
	swa_key_zoom,
	swa_key_mode,
	swa_key_keyboard,
	swa_key_screen,
	swa_key_pc,
	swa_key_tv,
	swa_key_tv2,
	swa_key_vcr,
	swa_key_vcr2,
	swa_key_sat,
	swa_key_sat2,
	swa_key_cd,
	swa_key_tape,
	swa_key_radio,
	swa_key_tuner,
	swa_key_player,
	swa_key_text,
	swa_key_dvd,
	swa_key_aux,
	swa_key_mp3,
	swa_key_audio,
	swa_key_video,
	swa_key_directory,
	swa_key_list,
	swa_key_memo,
	swa_key_calendar,
	swa_key_red,
	swa_key_green,
	swa_key_yellow,
	swa_key_blue,
	swa_key_channelup,
	swa_key_channeldown,
	swa_key_first,
	swa_key_last,
	swa_key_ab,
	swa_key_next,
	swa_key_restart,
	swa_key_slow,
	swa_key_shuffle,
	swa_key_kbreak,
	swa_key_previous,
	swa_key_digits,
	swa_key_teen,
	swa_key_twen,
	swa_key_videophone,
	swa_key_games,
	swa_key_zoomin,
	swa_key_zoomout,
	swa_key_zoomreset,
	swa_key_wordprocessor,
	swa_key_editor,
	swa_key_spreadsheet,
	swa_key_graphicseditor,
	swa_key_presentation,
	swa_key_database,
	swa_key_news,
	swa_key_voicemail,
	swa_key_addressbook,
	swa_key_messenger,
	swa_key_display_toggle,
	swa_key_brightness_toggle = swa_key_display_toggle,
	swa_key_spellcheck,
	swa_key_logoff,

	swa_key_dollar,
	swa_key_euro,

	swa_key_frame_back,
	swa_key_frame_forward,
	swa_key_context_menu,
	swa_key_media_repeat,
	swa_key_channels_up_10,
	swa_key_channels_down_10,
	swa_key_images,

	swa_key_del_eol = 0x1c0,
	swa_key_del_eos,
	swa_key_ins_line,
	swa_key_del_line,

	swa_key_fn = 0x1d0,
	swa_key_fn_esc,
	swa_key_fn_f1,
	swa_key_fn_f2,
	swa_key_fn_f3,
	swa_key_fn_f4,
	swa_key_fn_f5,
	swa_key_fn_f6,
	swa_key_fn_f7,
	swa_key_fn_f8,
	swa_key_fn_f9,
	swa_key_fn_f10,
	swa_key_fn_f11,
	swa_key_fn_f12,
	swa_key_fn_1,
	swa_key_fn_2,
	swa_key_fn_d,
	swa_key_fn_e,
	swa_key_fn_f,
	swa_key_fn_s,
	swa_key_fn_b,

	swa_key_brl_dot1 = 0x1f1,
	swa_key_brl_dot2,
	swa_key_brl_dot3,
	swa_key_brl_dot4,
	swa_key_brl_dot5,
	swa_key_brl_dot6,
	swa_key_brl_dot7,
	swa_key_brl_dot8,
	swa_key_brl_dot9,
	swa_key_brl_dot10,

	swa_key_numeric0 = 0x200,
	swa_key_numeric1,
	swa_key_numeric2,
	swa_key_numeric3,
	swa_key_numeric4,
	swa_key_numeric5,
	swa_key_numeric6,
	swa_key_numeric7,
	swa_key_numeric8,
	swa_key_numeric9,
	swa_key_numeric_star,
	swa_key_numeric_pound,
	swa_key_numeric_a,
	swa_key_numeric_b,
	swa_key_numeric_c,
	swa_key_numeric_d,

	swa_key_camera_focus,
	swa_key_wps_button,

	swa_key_touchpad_toggle,
	swa_key_touchpad_on,
	swa_key_touchpad_off,

	swa_key_camera_zoomin,
	swa_key_camera_zoomout,
	swa_key_camera_up,
	swa_key_camera_down,
	swa_key_camera_left,
	swa_key_camera_right,

	swa_key_attendant_on,
	swa_key_attendant_off,
	swa_key_attendant_toggle,
	swa_key_lights_toggle,

	swa_key_alsToggle = 0x230,

	swa_key_buttonconfig = 0x240,
	swa_key_taskmanager,
	swa_key_journal,
	swa_key_controlpanel,
	swa_key_appselect,
	swa_key_screensaver,
	swa_key_voicecommand,

	swa_key_brightness_min = 0x250,
	swa_key_brightness_max,

	swa_key_kbdinputassist_prev = 0x260,
	swa_key_kbdinputassist_next,
	swa_key_kbdinputassist_prevgroup,
	swa_key_kbdinputassist_nextgroup,
	swa_key_kbdinputassist_accept,
	swa_key_kbdinputassist_cancel,

	swa_key_right_up,
	swa_key_right_down,
	swa_key_left_up,
	swa_key_left_down,

	swa_key_root_menu,
	swa_key_media_top_menu,
	swa_key_numeric11,
	swa_key_numeric12,

	swa_key_audio_esc,
	swa_key_mode_3d,
	swa_key_next_avorite,
	swa_key_stop_record,
	swa_key_pause_record,
	swa_key_vod,
	swa_key_unmute,
	swa_key_fast_reverse,
	swa_key_slow_reverse,

	swa_key_data = swa_key_fast_reverse,

	swa_key_extra = 0x10000,
};

