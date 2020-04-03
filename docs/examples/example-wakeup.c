#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

static atomic_bool run = true;
static atomic_bool wakeup = false;
static struct timespec wakeup_time;

static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	dlg_info("drawing window, size: %d %d", img.width, img.height);
	unsigned size = img.height * img.stride;
	memset(img.data, 255, size);
	swa_window_apply_buffer(win);
}

static void window_close(struct swa_window* win) {
	atomic_store(&run, false);
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
};

static void* wakeup_thread(void* data) {
	struct swa_display* dpy = (struct swa_display*) data;
	while(atomic_load(&run)) {
		bool lwakeup = atomic_exchange(&wakeup, true);
		if(!lwakeup) {
			clock_gettime(CLOCK_MONOTONIC, &wakeup_time);
			swa_display_wakeup(dpy);
		} else {
			dlg_error("Main thread takes too long to clear wakeup flag");
		}

		sleep(2);
	}

	return NULL;
}

int main() {
	struct swa_display* dpy = swa_display_autocreate("swa example-wakeup");
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	struct swa_window* win = swa_display_create_window(dpy, &settings);
	if(!win) {
		dlg_fatal("Failed to create window");
		swa_display_destroy(dpy);
		return EXIT_FAILURE;
	}

	swa_window_set_userdata(win, dpy);

	pthread_t wt;
	pthread_create(&wt, NULL, wakeup_thread, dpy);

	while(atomic_load(&run)) {
		if(!swa_display_dispatch(dpy, true)) {
			atomic_store(&run, false);
			break;
		}

		if(atomic_load(&wakeup)) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			int64_t ns = now.tv_nsec - wakeup_time.tv_nsec;
			ns += (1000 * 1000 * 1000) * (now.tv_sec - wakeup_time.tv_sec);
			dlg_assert(ns < 10 * 1000 * 1000); // 10 ms is way too much
			dlg_info("Took %ld ns to receive wakeup", (long) ns);
			atomic_store(&wakeup, false);
		}
	}

	swa_window_destroy(win);

	dlg_info("waiting for wakeup thread to join...");
	void* _;
	pthread_join(wt, &_);
	swa_display_destroy(dpy);
}

