# Multithreading

Backends are not required to be internally synchronized (and are not,
in general). Calling *any* two functions of a backend (no matter
if on seperate window objects, display and window or both on the
display) is not guaranteed to work. If you can make sure - using
external synchronization - that only one thread at a time is
using a display (and its windows), you'll be fine on all backends
except Windows: Since most winapi functions are thread-dependent
you have to call *all* backend functions from the same thread:
the thread that created the display. So if you want to support
windows, make sure to only access your display and window 
functions from one thread. It's obviously ok to let other threads call
display-independent swa functions, such as manipulating images or
cursor objects.

The only exception to all these rules - and what allows to nontheless
easily write multithreaded applications interacting with swa - is
swa_display_wakeup_wait. This function can be called from all
threads, at any time, even on Windows.
If you e.g. decide in a thread that doesn't own the swa display
that you want your window to be maximized,
just pass that message to your swa thread (using whatever mechanism
you use for message passing e.g. a ring buffer or a lock-based
data structure). If your swa thread is currently waiting for
events using swa_display_dispatch with the wait parameter set to true,
it won't receive that message though.
So if you use blocking event dispatching you have to call
swa_display_wakeup_wait after queueing the message in the non-swa
thread to make sure that the thread interfacing with swa will wake up
and be able to read the message in finite time.
There you can then just handle the message and maximize the window.

Even if you don't care for the Windows backend, this is probably
the cleanest way to do it. You have to make sure that
during a call to swa_display_dispatch no other thread is accessing
that swa display or its windows. Especially when waiting for events
in swa_display_dispatch, using a lock around it is a bad idea.

## Rationale

The only way we could lift the heavy requirements on multithreading
when interfacing with swa would be to move this message passing and
wakeup mechanism into swa or the different backends. This is a
bad idea since most code should have no problem at all just using
one thread for interfacing with swa, i.e. multithreaded window
manipulation isn't a common use case since most applications don't
have multiple threads for UI. At the same time it would make swa
more complicated and would require the library to implement a message
passing mechanism that your application probably already has anyways
if you're using multiple threads.