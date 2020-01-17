Notes on the android backend
============================

Android handles processes and threads a bit differently. Furthermore, swa
has to emulate the actual `main` function being called and so some things
might not be as expected.

Mainly: globals. The problem is that two *instances* of the same app may
be active at the same time. This can happen e.g. when one instance is
still destroying itself (cleaning up, running destructors etc.) while
the app is restarted again - spawning a new instance. But both instances
will use the same globals. There are multiple ways to work around this:

First, just don't use globals. While that is obviously usually the right
way, it may not always be possible/the best way. You can also just protect
yourself from it by just adding a synchronization primitive to the main
function like this:

```c
static mutex g_mtx; // global mutex

int main() {
	lock_guard guard(g_mtx); // will unlock the mutex when main exits
	// normal main stuff here
}
```

This way, you prevent two main functions from being active (and therefore
potentially two programs messing with globals) at once. But even then,
you have to keep in mind that the globals may not be zero/default-initialized
when the second instance is started. Most programs depend on this to
be true.

Furthermore, this might lead to a delay when the an instance of the app is
being started while another one is still cleaning up.

---

There might be a way in swa to give stronger guarantees, should investigate.
