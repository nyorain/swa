Notes
=====

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

laster

- integration with posix api (see docs/posix.h)
- option to load vkGetInstanceProcAddr dynamically
- allow to not create a gl context for every window
