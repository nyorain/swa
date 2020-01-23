#include <swa/swa.h>
#include <swa/key.h>
#include <dlg/dlg.h>
#include <string.h>
#include <time.h>

static bool run = true;

// Only for android: show keyboard on touch
void show_keyboard(struct swa_display* dpy, bool show);
static bool showing_keyboard = false;

static void window_draw(struct swa_window* win) {
	struct swa_image img;
	if(!swa_window_get_buffer(win, &img)) {
		return;
	}

	unsigned size = img.height * img.stride;
	memset(img.data, 255, size);

	swa_window_apply_buffer(win);
}

static void window_close(struct swa_window* win) {
	run = false;
}

static void window_focus(struct swa_window* win, bool gained) {
	dlg_info("focus %s", gained ? "gained" : "lost");
}

static void key(struct swa_window* win, const struct swa_key_event* ev) {
	dlg_info("key %d %s %s: utf8 %s", ev->keycode,
		ev->pressed ? "pressed" : "released",
		ev->repeated ? "(repeated)" : "",
		ev->utf8 ? ev->utf8 : "<none>");

	if(ev->pressed && ev->keycode == swa_key_escape) {
		dlg_info("Escape pressed, exiting");
		run = false;
	}
}

static void mouse_move(struct swa_window* win,
		const struct swa_mouse_move_event* ev) {
	dlg_info("mouse moved to (%d, %d)", ev->x, ev->y);
}

static void mouse_cross(struct swa_window* win,
		const struct swa_mouse_cross_event* ev) {
	dlg_info("mouse %s at (%d, %d)", ev->entered ? "entered" : "left",
		ev->x, ev->y);
}

static void mouse_button(struct swa_window* win,
		const struct swa_mouse_button_event* ev) {
	dlg_info("mouse button: button = %d %s, pos = (%d, %d)",
		ev->button, ev->pressed ? "pressed" : "released",
		ev->x, ev->y);
}

static void mouse_wheel(struct swa_window* win,
		float dx, float dy) {
	dlg_info("mouse wheel: (%f, %f)", dx, dy);
}

static void touch_begin(struct swa_window* win,
		const struct swa_touch_event* ev) {
	dlg_info("touch begin: id = %d, pos = (%d, %d)",
		ev->id, ev->x, ev->y);

	showing_keyboard = !showing_keyboard;
	show_keyboard((struct swa_display*) swa_window_get_userdata(win),
		showing_keyboard);
}

static void touch_update(struct swa_window* win,
		const struct swa_touch_event* ev) {
	dlg_info("touch update: id = %d, pos = (%d, %d)",
		ev->id, ev->x, ev->y);
}

static void touch_end(struct swa_window* win, unsigned id) {
	dlg_info("touch end: id = %d", id);
}

static void touch_cancel(struct swa_window* win) {
	dlg_info("touch cancel");
}

static const struct swa_window_listener window_listener = {
	.draw = window_draw,
	.close = window_close,
	.key = key,
	.mouse_move = mouse_move,
	.mouse_cross = mouse_cross,
	.mouse_button = mouse_button,
	.mouse_wheel = mouse_wheel,
	.touch_begin = touch_begin,
	.touch_update = touch_update,
	.touch_end = touch_end,
	.touch_cancel = touch_cancel,
	.focus = window_focus,
};

int main() {
	struct swa_display* dpy = swa_display_autocreate("swa-input");
	if(!dpy) {
		dlg_fatal("No swa backend available");
		return EXIT_FAILURE;
	}

	struct swa_window_settings settings;
	swa_window_settings_default(&settings);
	settings.title = "swa-example-window";
	settings.surface = swa_surface_buffer;
	settings.listener = &window_listener;
	settings.transparent = false;
	struct swa_window* win = swa_display_create_window(dpy, &settings);
	if(!win) {
		dlg_fatal("Failed to create window");
		swa_display_destroy(dpy);
		return EXIT_FAILURE;
	}

	swa_window_set_userdata(win, dpy);

	while(run) {
		if(!swa_display_dispatch(dpy, true)) {
			break;
		}
	}

	swa_window_destroy(win);
	swa_display_destroy(dpy);
}

#ifdef __ANDROID__

#include <swa/android.h>
#include <android/native_activity.h>
#include <jni.h>

void show_keyboard(struct swa_display* dpy, bool show) {
	dlg_assert(swa_display_is_android(dpy));
	ANativeActivity* activity = swa_display_android_get_native_activity(dpy);

    JavaVMAttachArgs attach_args;
    attach_args.version = JNI_VERSION_1_6;
    attach_args.name = "NativeThread";
    attach_args.group = NULL;

	JNIEnv* jni_env;
	JavaVM* vm = activity->vm;
    jint res = (*vm)->AttachCurrentThread(vm, &jni_env, &attach_args);
    if(res == JNI_ERR) {
		dlg_error("AttachCurrentThread failed");
		return;
    }

	jobject activity_object = activity->clazz;

    // java: Context.INPUT_METHOD_SERVICE.
    jclass class_native_activity = (*jni_env)->GetObjectClass(jni_env, activity_object);
    jclass class_ctx = (*jni_env)->FindClass(jni_env, "android/content/Context");
    jfieldID field_input_method_service =
        (*jni_env)->GetStaticFieldID(jni_env, class_ctx,
		"INPUT_METHOD_SERVICE", "Ljava/lang/String;");
    jobject input_method_service = (*jni_env)->GetStaticObjectField(jni_env,
			class_ctx, field_input_method_service);

    // java: getSystemService(Context.INPUT_METHOD_SERVICE).
    jclass class_input_method_manager = (*jni_env)->FindClass(jni_env,
        "android/view/inputmethod/InputMethodManager");
    jmethodID method_get_system_service = (*jni_env)->GetMethodID(jni_env,
        class_native_activity, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject input_method_manager = (*jni_env)->CallObjectMethod(jni_env,
        activity_object, method_get_system_service,
        input_method_service);

    // java: getWindow().getDecorView().
    jmethodID method_get_window = (*jni_env)->GetMethodID(jni_env,
        class_native_activity, "getWindow", "()Landroid/view/Window;");
    jobject window = (*jni_env)->CallObjectMethod(jni_env, activity_object,
        method_get_window);
    jclass class_window = (*jni_env)->FindClass(jni_env, "android/view/Window");
    jmethodID method_get_decor_view = (*jni_env)->GetMethodID(jni_env,
        class_window, "getDecorView", "()Landroid/view/View;");
    jobject decor_view = (*jni_env)->CallObjectMethod(jni_env, window,
        method_get_decor_view);

	jint flags = 0;
    if(show) {
        // java: lInputMethodManager.showSoftInput(...).
        jmethodID method_show_soft_input = (*jni_env)->GetMethodID(jni_env,
            class_input_method_manager, "showSoftInput", "(Landroid/view/View;I)Z");
        jboolean result = (*jni_env)->CallBooleanMethod(jni_env,
            input_method_manager, method_show_soft_input,
            decor_view, flags);
		(void) result; // return value not documented
    } else {
        // java: lWindow.getViewToken()
        jclass class_view = (*jni_env)->FindClass(jni_env, "android/view/View");
        jmethodID method_get_window_token = (*jni_env)->GetMethodID(jni_env,
            class_view, "getWindowToken", "()Landroid/os/IBinder;");
        jobject binder = (*jni_env)->CallObjectMethod(jni_env, decor_view,
            method_get_window_token);

        // java: lInputMethodManager.hideSoftInput(...).
        jmethodID method_hide_soft_input = (*jni_env)->GetMethodID(jni_env,
            class_input_method_manager, "hideSoftInputFromWindow",
			"(Landroid/os/IBinder;I)Z");
        jboolean result = (*jni_env)->CallBooleanMethod(jni_env,
            input_method_manager, method_hide_soft_input,
            binder, flags);
		(void) result; // return value not documented
    }

    (*vm)->DetachCurrentThread(vm);
}

#else

void show_keyboard(struct swa_display* dpy, bool show) {
	(void) dpy;
	(void) show;
}
#endif
