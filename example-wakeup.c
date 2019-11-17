#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

static bool run = true;
static bool wakeup = false;
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
	run = false;
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
};

static void* wakeup_thread(void* data) {
	struct swa_display* dpy = (struct swa_display*) data;
	while(run) {
		dlg_assert(!wakeup); // 5 secs should be enough to clear it
		wakeup = true;
		clock_gettime(CLOCK_MONOTONIC, &wakeup_time);
		swa_display_wakeup(dpy);
		sleep(2);
	}

	return NULL;
}

int main() {
	struct swa_display* dpy = swa_display_autocreate();
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.app_name = "swa-example";
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

	while(run) {
		if(!swa_display_dispatch(dpy, true)) {
			break;
		}

		if(wakeup) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			int64_t ns = now.tv_nsec - wakeup_time.tv_nsec;
			ns += (1000 * 1000 * 1000) * (now.tv_sec - wakeup_time.tv_sec);
			dlg_assert(ns < 1000 * 1000); // 1 ms is way too much
			dlg_info("Took %ld ns to receive wakeup", ns);
			wakeup = false;
		}
	}

	swa_window_destroy(win);
	swa_display_destroy(dpy);

	dlg_info("waiting for wakeup thread to join...");
	void* _;
	pthread_join(wt, &_);
}

