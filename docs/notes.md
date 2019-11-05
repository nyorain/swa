Notes
=====

- when to use events and when just use plain parameters in window listener?
- allow implementations to e.g. not track touch points or other input events
  if the window has no listener at all or the listener doesn't implement
  those functions?
  	- what if the listener is changed during a touch point session though?

todo wayland:

- buffer surfaces
- data exchange (probbaly move that to another file though)
- vulkan
- gl
	- implement egl first
- check that everything is cleaned up correctly

later:

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
