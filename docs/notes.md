Notes
=====

- document that we ignore the meson default_library argument
- when to use events and when just use plain parameters in window listener?
- allow implementations to e.g. not track touch points or other input events
  if the window has no listener at all or the listener doesn't implement
  those functions?
  	- what if the listener is changed during a touch point session though?

todo wayland:

- data exchange (probbaly move that to another file though)
- better gl support, allow settings
- check that everything is cleaned up correctly

general

- merge swa_default_{size, position} into swa_default?
- only build pml as subproject when on unix
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
