Notes
=====

- when to use events and when just use plain parameters in window listener?
- allow implementations to e.g. not track touch points or other input events
  if the window has no listener at all or the listener doesn't implement
  those functions?
  	- what if the listener is changed during a touch point session though?

todo wayland:

- data exchange (probbaly move that to another file though)
- wakeup pipe
- gl
	- implement egl first
- check that everything is cleaned up correctly
- pre-mulitplied alpha. Do other compositors/backends require that
  as well for transprency?

later:

- only build pml as subproject when on unix
- gl transparency semantics. Does the platform use premultiplied alpha?
  e.g. on wayland we have to use premultiplied alpha, on other platforms
  probably not
- error checking. Make sure we cleanup everything?
	- or don't do that in places where errors aren't expected?
	  tradeoff maintainability and possibly usability here i guess?
	  like do we really have to check the return value of
	  wl_compositor_create_surface? i don't know any condition we can/want
	  to handle in which it returns NULL.
- re-entrant callbacks. Allowed to modify window state in handler?
  allowed to call data_offer_destroy in data offer callback?
  etc
- option to load vkGetInstanceProcAddr dynamically
- abolish event trigger datas? just use the last received serial?
  what for x11 and other backends?
- for most timers (e.g. key repeat timer wayland), CLOCK_MONOTONIC
  would be better. requires ml support
- wayland handle multiple images
	- when setting a cursor, store time (clock_monotonic? or realtime?)
	- just add a timer that is restarted with cursor_img->delay
	  there, check the elapsed ms since cursor was set and query the
	  current image (and time until next) with wl_cursor_frame_and_duration.
	  reset timer with duration
	- do we have to use frame callbacks? would probably be nice.
	  when the frame callback wasn't yet triggered in the change timer,
	  just set a flag that next commit should happen on frame callback.
	  Otherwise redraw immediately.
- image_format endian conversion
	- TODO in wayland.c for that
- integration with posix api
- allow to not create a gl context for every window
