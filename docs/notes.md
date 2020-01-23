Notes
=====

wayland:

- implement gl swap interval
- implement missing data exchange (probbaly move that to another file though)
- better gl support, allow settings
- check that everything is cleaned up correctly
- cleaner, more advanced gl surfaces
  don't create with fallback size (-> see TODO there)
- always flush the display after wl_surface_commit or other visible
  actions? we only flush it on (before) dispatch atm but some programs
  might have long pauses between iterations. Guess the same
  problem for x11

x11:
the ones with [low] are probably not worth it.
We should evaluate and document the reasons.

- send state change events
- implement gl swap interval
- implement data exchange stuff
- remove xcb_*_checked versions in most places. Or only keep them in the
  debug build somehow?
- test touch input on device. ask fritz or get chromebook to work again?
- [low] we could implement buffer surfaces using present pixmaps
  more complicated though, we have to do maintain multiple
  buffers (pixmaps)
- [low] check/test which wm's support motif wm hints and only
  report the client_decoration display cap on those (we can query
  which wm is active)
- [low] use glx instead of egl? Could egl not be available anywhere?

known issues:
- currently, egl transparency is broken with mesa (because of a year-long bug)
  see https://bugs.freedesktop.org/show_bug.cgi?id=67676.
  The MR (https://gitlab.freedesktop.org/mesa/mesa/merge_requests/1185) that
  fixes it was merged and will probably be in mesa 20.
  Workarounds require significant amount of work

winapi:
[not implemented yet at all, really]

- fix redraw/refresh loop

android:
- add exception catching wrapper around main call?
  that at least prints the exception to the log?
- display dispatch currently not re-entrant at all
  not sure if ALooper is though...
- add support for mouse events (and capability)

general

- fixup meson for linux/unix: dynmically enable backends/parts
  of backends
- x11 & wayland backends: make sure that key_states and button_states
  are never accessed out of range, even for weird codes
- error checking. Make sure we cleanup everything?
	- or don't do that in places where errors aren't expected?
	  tradeoff maintainability and possibly usability here i guess?
	  like do we really have to check the return value of
	  wl_compositor_create_surface? i don't know any condition we can/want
	  to handle in which it returns NULL.

later/low prio

- public apis per backend. E.g. retrieve x11/wayland display from
  display, get android native activity etc.
  Should be possible to check first via something like `swa_display_is_android`
  or something
- further gl settings: accum buffer, color buffer depth,
  robustness/reset strategy
- evaluate once again whether/where deferred events make sense.
  They can be useful for resizing/redrawing (i.e. don't handle
  all resize events directly but rather process all currently
  available events and no matter how many resize/draw events are
  in there, only handle the last one)
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
	- cache loaded procs instead of loading them everytime
- allow to share gl contexts between windows
	- or maybe not create one context per window? not important though,
	  hundreds of windows isn't a priority usecase 
- could add `struct swa_display* swa_window_get_display(struct swa_window*)`
- could add 'refresh' and/or 'frame' to window capabilities
  it can't be implemented on all backends
- set xkbcommon logging function
- add swa_cursor_disable or something that allows to lock pointer
  on wayland and grab the cursor on x11. windows has probably something
  like that as well
- add interface to query platform phdev vulkan support, see glfw and
  example-vulkan.c
  	- evaluate first whether this is really needed
  	- make sure it works for the drm backend prototype
- gl transparency semantics. Does the platform use premultiplied alpha?
  e.g. on wayland we have to use premultiplied alpha, on other platforms
  probably not. Applications have to know that i guess
- re-entrant callbacks. Allowed to modify window state in handler?
  allowed to call data_offer_destroy in data offer callback?
  etc


## References and dev docs

xkbcommon:
Their whole test folder is really good as examples.
Especially test/interactive-x11.c for xkb integration.

xpresent extension:
Keith's blog posts (e.g. https://keithp.com/blogs/Present, there are more
and his blog. Some things seem slightly different now than described
there though). And mesa's vulkan/wsi/wsi_common_x11.c which uses
it extensively.

x11 buffer surfaces:
- https://tronche.com/gui/x/xlib/graphics/images.html
- https://tronche.com/gui/x/xlib/display/image-format-macros.html
- https://www.x.org/releases/X11R7.5/doc/x11proto/proto.pdf
  the spec documents what scanline_pad is. It's not really a padding
  but an alignment requirement
The list of sources i noted for ny:
- https://github.com/freedesktop-unofficial-mirror/xcb__util-image/blob/master/image/xcb_image.c#L158
- http://xcb.pdx.freedesktop.narkive.com/0u3XxxGY/xcb-put-image
- https://github.com/freedesktop-unofficial-mirror/xcb__util-image/blob/master/image/xcb_image.h
- https://www.x.org/releases/X11R7.6/doc/xproto/x11protocol.html#requests:PutImage
- https://tronche.com/gui/x/xlib/graphics/XPutImage.html

xwayland seems to not support shm pixmaps. We don't require them (or use
them at all).

x11 touch events and ownership
https://lwn.net/Articles/475886/
https://lwn.net/Articles/485484/
