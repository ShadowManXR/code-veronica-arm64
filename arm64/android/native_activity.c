#include "adv.h"
#include "main.h"
#include "sdfunc.h"
#include "android_egl.h"
#include "boot_disc.h"
#include "disc.h"
#include "movie_port.h"
#include "event_movie.h"
#include "opening.h"
#include "event_audio.h"
#include "pad_input.h"
#include "title_draw.h"
#include "theater.h"

#include <GLES3/gl3.h>
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define TAG "RECVX-ARM64"

typedef struct {
    RxEgl egl;
    RxAndroidDisc disc;
    RxBootDisc boot;
    RxTitleDraw title;
    RxOpening opening;
    RxMovieDraw movie_draw;
    RxTitleScreen screen;
    RxPadInput pad;
    RxTheater *theater;
    double game_elapsed;
    ANativeWindow *window;
    AInputQueue *input;
    const char *internal_directory;
    ANativeActivity *activity;
    int resumed;
    int focused, frame_timer, logged_frame;
} Host;

static void update_frame_timer(Host *host);

static void close_window_graphics(Host *host)
{
    if (host->title.program) rx_title_close(&host->title);
    if (host->movie_draw.program) rx_game_movie_close_graphics(&host->movie_draw);
    rx_opening_close_graphics(&host->opening);
    rx_theater_close(host->theater);
    host->theater = NULL;
    rx_egl_close(&host->egl);
    host->logged_frame = 0;
}

static void present(Host *host, int interactive)
{
    if (!host->resumed || !host->window) return;
    if (host->egl.surface == EGL_NO_SURFACE && !rx_egl_open(&host->egl, NULL, 1024, 768)) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "EGL theater initialization failed");
        ANativeActivity_finish(host->activity);
        return;
    }
    if (!host->theater) {
        const char *error = rx_theater_open(&host->theater, host->activity, &host->egl);
        if (error) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "%s", error);
            close_window_graphics(host);
            ANativeActivity_finish(host->activity);
            return;
        }
    }
    rx_theater_target(host->theater);

    int width, height;
    if (!rx_egl_size(&host->egl, &width, &height)) return;
    int scale = width / 4 < height / 3 ? width / 4 : height / 3;
    if (!scale) return;
    int view_width = scale * 4, view_height = scale * 3;
    glViewport((width - view_width) / 2, (height - view_height) / 2, view_width, view_height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    int drew_title = 0;
    RxMovie *movie = rx_game_movie_active();
    if (movie && !rx_game_movie_draw(movie, &host->movie_draw)) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not present movie frame");
        PlayStopMovieEx(0);
        rx_game_movie_close_graphics(&host->movie_draw);
        movie = NULL;
        update_frame_timer(host);
    }
    if(movie&&rx_event_movie_running()){
        const char *error=rx_message_present(&host->opening.particles);
        if(error)__android_log_print(ANDROID_LOG_ERROR,TAG,"Movie message draw: %s",error);
    }
    if (!movie && host->opening.arena) {
        const char *error = rx_opening_present(&host->opening);
        if (error) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "New Game room draw: %s", error);
            rx_opening_close_graphics(&host->opening);
            rx_opening_end(&host->opening);
            update_frame_timer(host);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    }
    if (!movie && !host->opening.arena && host->boot.title_package) {
        RxTitleScreen screen = host->screen;
        if (host->title.program ||
            rx_title_open(&host->title, &host->boot))
            drew_title = rx_title_present(&host->title, &host->screen, interactive);
        if (screen == RX_TITLE_OPTIONS && host->screen == RX_TITLE_MENU &&
            !rx_title_write_settings(host->internal_directory))
            __android_log_print(ANDROID_LOG_WARN, TAG, "Could not save Options settings");
        if (!drew_title) __android_log_print(ANDROID_LOG_ERROR, TAG, "Title rendering failed");
    }
    if (!host->logged_frame && (!movie || host->movie_draw.uploaded)) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Presented %s in %dx%d window with %dx%d viewport on %s",
                            movie ? "original movie" : host->opening.arena ? "original starting room" :
                            drew_title ? "original title" : "cleared frame", width, height,
                            view_width, view_height, (const char *)glGetString(GL_RENDERER));
        host->logged_frame = 1;
    }
}

static void step_frame(Host *host)
{
    if (rx_game_movie_active()&&!rx_event_movie_running()) {
        // Touch A is also a convenient opening-video skip on the virtual screen.
        if (host->pad.pending & RX_PAD_CONFIRM) host->pad.pending |= RX_PAD_START;
        rx_pad_poll(&host->pad);
        int ended = PlayMovieMain();
        if (ended) {
            __android_log_print(ended == 3 ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, TAG,
                                "Movie %s", ended == 1 ? "completed" : ended == 2 ? "skipped" : "failed");
            if (host->movie_draw.program) rx_game_movie_close_graphics(&host->movie_draw);
            rx_theater_suppress_held_buttons(host->theater);
            rx_pad_clear(&host->pad);
            host->logged_frame = 0;
            update_frame_timer(host);
        }
        present(host, 0);
        return;
    }
    if (host->opening.arena) {
        int was_event_movie=rx_event_movie_running();
        rx_pad_poll_game(&host->pad);
        unsigned before = sys->pad_on;
        if(host->opening.events_ready)rx_opening_read_runtime_controls();
        else rx_opening_read_controls();
        if (before != sys->pad_on)
            __android_log_print(ANDROID_LOG_DEBUG, TAG,
                                "Player pad held=%05x press=%05x release=%05x locomotion=%u",
                                sys->pad_on, sys->pad_ps, sys->pad_rs, (unsigned)plp->mode2);
        const char *error = rx_opening_move_player(&host->opening);
        if (error) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "New Game player: %s", error);
            rx_opening_close_graphics(&host->opening);
            rx_opening_end(&host->opening);
            update_frame_timer(host);
        }
        if(was_event_movie&&!rx_event_movie_running()&&!rx_game_movie_active()){
            rx_game_movie_close_graphics(&host->movie_draw);
            rx_theater_suppress_held_buttons(host->theater);rx_pad_clear(&host->pad);
            host->logged_frame=0;update_frame_timer(host);
        }
        present(host, 0);
        return;
    }
    rx_pad_poll(&host->pad);
    RxTitleScreen previous = host->screen;
    int difficulty = rx_title_menu_update(&host->screen);
    if (difficulty >= 0) {
        const char *error = rx_opening_start(&host->opening, (unsigned)difficulty);
        if (error)
            __android_log_print(ANDROID_LOG_ERROR, TAG, "New Game: %s", error);
        else {
            rx_pad_clear(&host->pad);
            host->logged_frame = 0;
            __android_log_print(ANDROID_LOG_INFO, TAG, "New Game difficulty %d initialized RM_0000", difficulty);
        }
    }
    if (previous != host->screen)
        __android_log_print(ANDROID_LOG_INFO, TAG, "Title screen changed to %d", host->screen);
    present(host, previous == RX_TITLE_OPTIONS);
    if (host->opening.arena) update_frame_timer(host);
}

static int frame_tick(int fd, int events, void *context)
{
    (void)events;
    Host *host = context;
    uint64_t ticks;
    if (read(fd, &ticks, sizeof(ticks)) != sizeof(ticks) ||
        !host->window || !host->boot.title_package) return 1;
    if (!host->theater) present(host, 0);
    if (!host->theater) return 1;
    int focused;
    double seconds;
    int frame = rx_theater_begin(host->theater, &host->pad, &focused, &seconds);
    rx_event_audio_pause(!(frame>0&&focused&&host->resumed));
    RxMovie *movie = rx_game_movie_active();
    if (movie) rx_game_movie_pause(movie, !(focused && host->resumed));
    if (frame > 0) {
        if (focused && host->resumed) {
            host->game_elapsed += seconds;
            double step = movie&&!rx_event_movie_running() ? 1.0/60 : 1.0/30;
            while (host->game_elapsed >= step) {
                host->game_elapsed -= step;
                step_frame(host);
                movie = rx_game_movie_active();
                step = movie&&!rx_event_movie_running() ? 1.0/60 : 1.0/30;
            }
        } else {
            host->game_elapsed = 0;
            if (host->opening.arena) rx_opening_clear_controls();
        }
        if (!rx_theater_end(host->theater)) frame = -1;
    }
    if (frame < 0 || rx_theater_exiting(host->theater)) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Virtual room session ended");
        ANativeActivity_finish(host->activity);
    }
    return 1;
}

static void update_frame_timer(Host *host)
{
    if (host->frame_timer < 0) return;
    struct itimerspec interval = {0};
    // Poll XR lifecycle events through focus changes and session stopping.
    if (host->window && host->boot.title_package)
        interval.it_value.tv_nsec = interval.it_interval.tv_nsec = 1000000000L / 90;
    timerfd_settime(host->frame_timer, 0, &interval, NULL);
}

static void resume(ANativeActivity *activity)
{
    Host *host = activity->instance;
    host->resumed = 1;
    if (!host->disc.image.stream) {
        const char *error = rx_android_mount_disc(&host->disc, activity->obbPath);
        const RxBootDisc *boot = &host->boot;
        if (!error) error = rx_android_start_disc(&host->disc, &host->boot);
        if (error) {
            __android_log_print(ANDROID_LOG_WARN, TAG, "%s", error);
            rx_android_unmount_disc(&host->disc);
        } else {
            const RxAndroidDisc *disc = &host->disc;
            __android_log_print(ANDROID_LOG_INFO, TAG,
                                "Mounted SLUS-20184 from app OBB (%" PRIu64 " bytes). "
                                "Boot ELF %u bytes at sector %u, entry 0x%08" PRIx32
                                ". SYSTEM.AFS %u bytes, RDX_LNK.AFS %u bytes",
                                disc->image.bytes, disc->boot_size, disc->boot_lsn, disc->boot_entry,
                                disc->system_size, disc->rooms_size);
            __android_log_print(ANDROID_LOG_INFO, TAG,
                                "Original InitGdSystemEx completed. Disc ID %u; linked RM_0000.RDX %u bytes",
                                boot->disc_id, boot->opening_room_size);
            __android_log_print(ANDROID_LOG_INFO, TAG,
                                "Original ADXF reads completed. Font %u, warning %u, title %u, Options %u bytes",
                                boot->font_size, boot->warning_size, boot->title_size, boot->options_size);
            rx_game_movie_bind(&disc->image);
            if (host->frame_timer >= 0) {
                if (AdvStartStartupMovie() != 0)
                    __android_log_print(ANDROID_LOG_WARN, TAG, "Original startup movie could not be opened");
                else
                    ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
            }
        }
    }
    update_frame_timer(host);
    present(host, 0);
}

static void pause_activity(ANativeActivity *activity)
{
    Host *host = activity->instance;
    host->resumed = 0;
    rx_event_audio_pause(1);
    RxMovie *movie = rx_game_movie_active();
    if (movie) rx_game_movie_pause(movie, 1);
    rx_pad_clear(&host->pad);
    if (host->opening.arena) rx_opening_clear_controls();
    update_frame_timer(host);
}

static void focus_changed(ANativeActivity *activity, int focused)
{
    Host *host = activity->instance;
    host->focused = focused;
    if(!focused)rx_event_audio_pause(1);
    if (!focused) {
        rx_pad_clear(&host->pad);
        if (host->opening.arena) rx_opening_clear_controls();
    }
    update_frame_timer(host);
}

static void window_created(ANativeActivity *activity, ANativeWindow *window)
{
    Host *host = activity->instance;
    if (host->window != window) close_window_graphics(host);
    host->window = window;
    update_frame_timer(host);
    present(host, 0);
}

static void window_redraw(ANativeActivity *activity, ANativeWindow *window)
{
    Host *host = activity->instance;
    if (host->window == window) present(host, 0);
}

static void window_destroyed(ANativeActivity *activity, ANativeWindow *window)
{
    Host *host = activity->instance;
    if (host->window != window) return;
    close_window_graphics(host);
    host->window = NULL;
    rx_event_audio_pause(1);
    update_frame_timer(host);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Native window released");
}

static int pointer_input(Host *host, const AInputEvent *event)
{
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION ||
        (AInputEvent_getSource(event) & AINPUT_SOURCE_CLASS_MASK) != AINPUT_SOURCE_CLASS_POINTER ||
        host->opening.arena) return 0;

    int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) return 1;
    if (action != AMOTION_EVENT_ACTION_DOWN || AMotionEvent_getPointerCount(event) != 1)
        return 0;
    if (host->screen == RX_TITLE_OPTIONS && !rx_game_movie_active()) return 0;

    int width, height;
    if (!rx_egl_size(&host->egl, &width, &height)) return 0;
    int scale = width / 4 < height / 3 ? width / 4 : height / 3;
    if (!scale) return 0;
    float x = (AMotionEvent_getX(event, 0) - (width - scale * 4) / 2) * 160.0f / scale;
    float y = (AMotionEvent_getY(event, 0) - (height - scale * 3) / 2) * 160.0f / scale;
    if (x < 0 || x >= 640 || y < 0 || y >= 480) return 0;
    unsigned button;
    if (rx_game_movie_active()) {
        button = RX_PAD_START;
    } else {
        button = rx_title_pointer_button(host->screen, x - sys->adjust_x, y - sys->adjust_y);
    }
    if (!button) return 0;
    // Quest's flat-window trigger click arrives as a pointer, with no Start key.
    host->pad.pending |= button;
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "Pointer action %04x at %.0f %.0f", button, x, y);
    return 1;
}

static int dispatch_input(int fd, int events, void *context)
{
    (void)fd;
    (void)events;
    Host *host = context;
    AInputQueue *queue = host->input;
    AInputEvent *event;
    while (AInputQueue_getEvent(queue, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(queue, event)) continue;
        int handled = host->resumed && host->focused && host->boot.title_package &&
                      (pointer_input(host, event) || rx_pad_event(&host->pad, event));
        AInputQueue_finishEvent(queue, event, handled);
        if (handled && host->frame_timer < 0) step_frame(host);
    }
    return 1;
}

static void input_created(ANativeActivity *activity, AInputQueue *queue)
{
    Host *host = activity->instance;
    host->input = queue;
    AInputQueue_attachLooper(queue, ALooper_forThread(), ALOOPER_POLL_CALLBACK, dispatch_input, host);
}

static void input_destroyed(ANativeActivity *activity, AInputQueue *queue)
{
    Host *host = activity->instance;
    if (host->input != queue) return;
    AInputQueue_detachLooper(queue);
    host->input = NULL;
    rx_pad_clear(&host->pad);
    if (host->opening.arena) rx_opening_clear_controls();
}

static void destroy(ANativeActivity *activity)
{
    Host *host = activity->instance;
    if (host->frame_timer >= 0) {
        ALooper_removeFd(ALooper_forThread(), host->frame_timer);
        close(host->frame_timer);
    }
    if (host->input) input_destroyed(activity, host->input);
    close_window_graphics(host);
    if (host->opening.arena) rx_opening_end(&host->opening);
    PlayStopMovieEx(0);
    rx_game_movie_bind(NULL);
    rx_android_stop_disc();
    rx_android_unmount_disc(&host->disc);
    free(host);
    activity->instance = NULL;
    __android_log_print(ANDROID_LOG_INFO, TAG, "Native activity destroyed");
}

__attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity *activity, void *saved_state, size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;
    Host *host = calloc(1, sizeof(*host));
    if (!host) {
        ANativeActivity_finish(activity);
        return;
    }
    rx_pad_init(&host->pad);
    host->activity = activity;
    rx_title_menu_init();
    host->internal_directory = activity->internalDataPath;
    rx_title_read_settings(host->internal_directory);
    host->frame_timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (host->frame_timer >= 0 &&
        ALooper_addFd(ALooper_forThread(), host->frame_timer, ALOOPER_POLL_CALLBACK,
                      ALOOPER_EVENT_INPUT, frame_tick, host) != 1) {
        close(host->frame_timer);
        host->frame_timer = -1;
    }
    activity->instance = host;
    activity->callbacks->onResume = resume;
    activity->callbacks->onPause = pause_activity;
    activity->callbacks->onWindowFocusChanged = focus_changed;
    activity->callbacks->onNativeWindowCreated = window_created;
    activity->callbacks->onNativeWindowResized = window_redraw;
    activity->callbacks->onNativeWindowRedrawNeeded = window_redraw;
    activity->callbacks->onNativeWindowDestroyed = window_destroyed;
    activity->callbacks->onInputQueueCreated = input_created;
    activity->callbacks->onInputQueueDestroyed = input_destroyed;
    activity->callbacks->onDestroy = destroy;
    __android_log_print(ANDROID_LOG_INFO, TAG, "Native activity created");
}
