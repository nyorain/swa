Notes
=====

wayland:

- data exchange (probbaly move that to another file though)
- better gl support, allow settings
- check that everything is cleaned up correctly
- always flush the display after wl_surface_commit or other visible
  actions? we only flush it on (before) dispatch atm but some programs
  might have long pauses between iterations. Guess the same
  problem for x11

x11:

- [low] we could implement buffer surfaces using present pixmaps
  more complicated though, we have to do maintain multiple
  buffers (pixmaps)

general

- x11 & wayland backends: make sure that key_states and button_states
  are never access out of range, even for weird codes
- add swa_cursor_disable or something that allows to lock pointer
  on wayland and grab the cursor on x11
- interface to query platform phdev vulkan support, see glfw
- figure out when to use events and when just use plain parameters in window listener?
	- clean up touch events. Really include dx, dy in movement?
	  would make some backend implementations *way* more complicated
- gl transparency semantics. Does the platform use premultiplied alpha?
  e.g. on wayland we have to use premultiplied alpha, on other platforms
  probably not
- further gl settings: api (gl/gles, also give way to query which
  api is used in context; then also give way to query function
  pointers to allow apps to support both dynamically), srgb,
  debug, forward_compatible, compatibility (legacy), depth, etc...
  hdr? i.e. color spaces/>8bit color output?
- error checking. Make sure we cleanup everything?
	- or don't do that in places where errors aren't expected?
	  tradeoff maintainability and possibly usability here i guess?
	  like do we really have to check the return value of
	  wl_compositor_create_surface? i don't know any condition we can/want
	  to handle in which it returns NULL.
- re-entrant callbacks. Allowed to modify window state in handler?
  allowed to call data_offer_destroy in data offer callback?
  etc

laster

- optimization: don't track e.g. touch events for a window if
  it has no touch event listener
- integration with posix api (see docs/posix.h)
	- nvm, android couldn't support it like this since inputs are tied
	  to the looper and we can't retrieve looper fds.
	  We could instead do just something like `swa_display_posix_add_fd`
	  and `swa_display_posix_remove_fd` instead i guess.
	  Something like this could be added for windows as well though,
	  using handles instead of file descriptors. Maybe just make this
	  part of the cross platform api somehow?
- option to load vkGetInstanceProcAddr dynamically
- allow to share gl contexts between windows
	- or maybe not create one context per window? not important though,
	  hundreds of windows isn't a priority usecase 
