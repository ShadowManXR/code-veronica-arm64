#include "theater.h"

#include <jni.h>
#include <GLES3/gl3.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "host/vr_view.h"

#include <android/log.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCREEN_W = 1024, SCREEN_H = 768, ROOM_VERTICES = 4096 };
enum { BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_MENU, BUTTON_LCLICK, BUTTON_RCLICK,
       STICK_LEFT, STICK_RIGHT, TRIGGER_LEFT, TRIGGER_RIGHT, GRIP_LEFT, GRIP_RIGHT, ACTION_COUNT };

typedef struct {
    XrSwapchain handle;
    XrSwapchainImageOpenGLESKHR *images;
    uint32_t count, width, height;
} TheaterChain;

typedef struct {
    float position[3], color[3];
} RoomVertex;

struct RxTheater {
    XrInstance instance;
    XrSession session;
    XrSpace space;
    XrActionSet actions;
    XrAction action[ACTION_COUNT];
    XrSessionState state;
    TheaterChain screen, eye[2];
    XrView views[2];
    XrFrameState frame;
    XrTime previous_time;
    int running, begun, exiting, logged_frame;
    GLuint game_fbo, game_texture, game_depth, eye_fbo;
    GLuint room_program, copy_program, room_vao, room_buffer;
    GLint room_matrix;
    unsigned room_count;
    uint16_t previous_buttons, blocked_buttons;
};

static int xr_ok(XrResult result, const char *operation)
{
    if (XR_FAILED(result))
        __android_log_print(ANDROID_LOG_ERROR, "RECVX-ARM64", "Theater %s failed (%d)", operation, result);
    return XR_SUCCEEDED(result);
}

static int create_chain(RxTheater *t, TheaterChain *chain, uint32_t width, uint32_t height)
{
    XrSwapchainCreateInfo create = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                        XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    create.format = GL_SRGB8_ALPHA8;
    create.width = width;
    create.height = height;
    create.sampleCount = create.faceCount = create.arraySize = create.mipCount = 1;
    if (!xr_ok(xrCreateSwapchain(t->session, &create, &chain->handle), "create swapchain") ||
        !xr_ok(xrEnumerateSwapchainImages(chain->handle, 0, &chain->count, NULL), "count images") ||
        !chain->count) return 0;
    chain->images = calloc(chain->count, sizeof(*chain->images));
    if (!chain->images) return 0;
    for (unsigned i = 0; i < chain->count; ++i)
        chain->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    chain->width = width;
    chain->height = height;
    return xr_ok(xrEnumerateSwapchainImages(chain->handle, chain->count, &chain->count,
                 (XrSwapchainImageBaseHeader *)chain->images), "enumerate images");
}

static int create_actions(RxTheater *t)
{
    static const char *names[ACTION_COUNT] = {
        "button_a", "button_b", "button_x", "button_y", "menu", "left_click", "right_click",
        "left_stick", "right_stick", "left_trigger", "right_trigger", "left_grip", "right_grip"
    };
    static const char *bindings[ACTION_COUNT] = {
        "/user/hand/right/input/a/click", "/user/hand/right/input/b/click",
        "/user/hand/left/input/x/click", "/user/hand/left/input/y/click",
        "/user/hand/left/input/menu/click", "/user/hand/left/input/thumbstick/click",
        "/user/hand/right/input/thumbstick/click", "/user/hand/left/input/thumbstick",
        "/user/hand/right/input/thumbstick", "/user/hand/left/input/trigger/value",
        "/user/hand/right/input/trigger/value", "/user/hand/left/input/squeeze/value",
        "/user/hand/right/input/squeeze/value"
    };
    XrActionSetCreateInfo set = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(set.actionSetName, "game_controls");
    strcpy(set.localizedActionSetName, "Game controls");
    if (!xr_ok(xrCreateActionSet(t->instance, &set, &t->actions), "create action set")) return 0;
    XrActionSuggestedBinding suggested[ACTION_COUNT];
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        XrActionCreateInfo create = {XR_TYPE_ACTION_CREATE_INFO};
        create.actionType = i < STICK_LEFT ? XR_ACTION_TYPE_BOOLEAN_INPUT :
                            i <= STICK_RIGHT ? XR_ACTION_TYPE_VECTOR2F_INPUT : XR_ACTION_TYPE_FLOAT_INPUT;
        strcpy(create.actionName, names[i]);
        strcpy(create.localizedActionName, names[i]);
        if (!xr_ok(xrCreateAction(t->actions, &create, &t->action[i]), "create action")) return 0;
        suggested[i].action = t->action[i];
        if (!xr_ok(xrStringToPath(t->instance, bindings[i], &suggested[i].binding), "action path")) return 0;
    }
    XrInteractionProfileSuggestedBinding profile = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    if (!xr_ok(xrStringToPath(t->instance, "/interaction_profiles/oculus/touch_controller",
                              &profile.interactionProfile), "Touch profile")) return 0;
    profile.countSuggestedBindings = ACTION_COUNT;
    profile.suggestedBindings = suggested;
    if (!xr_ok(xrSuggestInteractionProfileBindings(t->instance, &profile), "bind Touch")) return 0;
    XrSessionActionSetsAttachInfo attach = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &t->actions;
    return xr_ok(xrAttachSessionActionSets(t->session, &attach), "attach actions");
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success) return shader;
    glDeleteShader(shader);
    return 0;
}

static GLuint create_program(const char *vs, const char *fs)
{
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vs), fragment = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint program = 0;
    if (vertex && fragment) {
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    return program;
}

static void room_quad(RoomVertex *vertices, unsigned *count, const float corners[4][3],
                       float r, float g, float b)
{
    static const unsigned indices[] = {0, 1, 2, 0, 2, 3};
    for (unsigned i = 0; i < 6; ++i) {
        RoomVertex *vertex = &vertices[(*count)++];
        memcpy(vertex->position, corners[indices[i]], sizeof(vertex->position));
        vertex->color[0] = r;
        vertex->color[1] = g;
        vertex->color[2] = b;
    }
}

static int create_room(RxTheater *t)
{
    const char *vs = "#version 300 es\n"
        "layout(location=0) in vec3 position; layout(location=1) in vec3 color;"
        "uniform mat4 matrix; out vec3 shade;"
        "void main(){gl_Position=matrix*vec4(position,1.0);shade=color;}";
    const char *fs = "#version 300 es\nprecision mediump float;"
        "in vec3 shade; out vec4 color; void main(){color=vec4(shade,1.0);}";
    t->room_program = create_program(vs, fs);
    if (!t->room_program) return 0;
    t->room_matrix = glGetUniformLocation(t->room_program, "matrix");

    RoomVertex *vertices = calloc(ROOM_VERTICES, sizeof(*vertices));
    if (!vertices) return 0;
    // Local space begins at the viewer's eye height. Keep the room and screen
    // stationary as the viewer looks around or leans.
    const float walls[][4][3] = {
        {{-4,-1.6f,-5},{4,-1.6f,-5},{4,2.8f,-5},{-4,2.8f,-5}},
        {{-4,-1.6f,3},{-4,-1.6f,-5},{-4,2.8f,-5},{-4,2.8f,3}},
        {{4,-1.6f,-5},{4,-1.6f,3},{4,2.8f,3},{4,2.8f,-5}},
        {{4,-1.6f,3},{-4,-1.6f,3},{-4,2.8f,3},{4,2.8f,3}},
        {{-4,2.8f,-5},{4,2.8f,-5},{4,2.8f,3},{-4,2.8f,3}}
    };
    for (unsigned i = 0; i < 5; ++i)
        room_quad(vertices, &t->room_count, walls[i], 0.045f + i*0.006f, 0.055f + i*0.006f, 0.075f + i*0.006f);
    for (int z = -5; z < 3; ++z) {
        for (int x = -4; x < 4; ++x) {
            float tint = ((x + z) & 1) ? 0.065f : 0.078f;
            const float tile[4][3] = {{x,-1.6f,z},{x+1,-1.6f,z},{x+1,-1.6f,z+1},{x,-1.6f,z+1}};
            room_quad(vertices, &t->room_count, tile, tint, tint, tint*1.1f);
        }
    }
    const float surround[4][3] = {{-1.73f,-1.28f,-3.52f},{1.73f,-1.28f,-3.52f},
                                  {1.73f,1.28f,-3.52f},{-1.73f,1.28f,-3.52f}};
    room_quad(vertices, &t->room_count, surround, 0.016f, 0.018f, 0.022f);
    // Warm wall strips give the room a visible scale without competing with the picture.
    for (int side = -1; side <= 1; side += 2) {
        float x = 2.6f * side;
        const float strip[4][3] = {{x-.025f,-1.3f,-4.98f},{x+.025f,-1.3f,-4.98f},
                                   {x+.025f,2.3f,-4.98f},{x-.025f,2.3f,-4.98f}};
        room_quad(vertices, &t->room_count, strip, 0.46f, 0.25f, 0.10f);
    }
    glGenVertexArrays(1, &t->room_vao);
    glBindVertexArray(t->room_vao);
    glGenBuffers(1, &t->room_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, t->room_buffer);
    glBufferData(GL_ARRAY_BUFFER, t->room_count * sizeof(*vertices), vertices, GL_STATIC_DRAW);
    free(vertices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RoomVertex), NULL);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RoomVertex), (void *)(3*sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return glGetError() == GL_NO_ERROR;
}

static int create_game_target(RxTheater *t)
{
    const char *vs = "#version 300 es\n"
        "out vec2 uv; void main(){uv=vec2(float(gl_VertexID&1),float(gl_VertexID>>1));"
        "gl_Position=vec4(uv*2.0-1.0,0.0,1.0);}";
    const char *fs = "#version 300 es\nprecision highp float;"
        "uniform sampler2D picture; in vec2 uv; out vec4 color;"
        "void main(){vec3 c=texture(picture,uv).rgb;"
        "vec3 linear=mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));"
        "color=vec4(linear,1.0);}";
    t->copy_program = create_program(vs, fs);
    if (!t->copy_program) return 0;
    glGenFramebuffers(1, &t->game_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->game_fbo);
    glGenTextures(1, &t->game_texture);
    glBindTexture(GL_TEXTURE_2D, t->game_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, SCREEN_W, SCREEN_H);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->game_texture, 0);
    glGenRenderbuffers(1, &t->game_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t->game_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, SCREEN_W, SCREEN_H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->game_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glGenFramebuffers(1, &t->eye_fbo);
    return glGetError() == GL_NO_ERROR;
}

void rx_theater_close(RxTheater *t)
{
    if (!t) return;
    glDeleteFramebuffers(1, &t->game_fbo);
    glDeleteFramebuffers(1, &t->eye_fbo);
    glDeleteRenderbuffers(1, &t->game_depth);
    glDeleteTextures(1, &t->game_texture);
    glDeleteBuffers(1, &t->room_buffer);
    glDeleteVertexArrays(1, &t->room_vao);
    glDeleteProgram(t->room_program);
    glDeleteProgram(t->copy_program);
    TheaterChain *chains[] = {&t->screen, &t->eye[0], &t->eye[1]};
    for (unsigned i = 0; i < 3; ++i) {
        if (chains[i]->handle) xrDestroySwapchain(chains[i]->handle);
        free(chains[i]->images);
    }
    if (t->space) xrDestroySpace(t->space);
    if (t->session) xrDestroySession(t->session);
    if (t->actions) xrDestroyActionSet(t->actions);
    if (t->instance) xrDestroyInstance(t->instance);
    free(t);
}

const char *rx_theater_open(RxTheater **out, ANativeActivity *activity, const RxEgl *egl)
{
    RxTheater *t = calloc(1, sizeof(*t));
    if (!t) return "Could not allocate virtual room";
    const char *error = "Could not initialize OpenXR";
    PFN_xrInitializeLoaderKHR initialize = NULL;
    if (!xr_ok(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
               (PFN_xrVoidFunction *)&initialize), "get Android loader") || !initialize) goto fail;
    XrLoaderInitInfoAndroidKHR loader = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM = activity->vm;
    loader.applicationContext = activity->clazz;
    if (!xr_ok(initialize((const XrLoaderInitInfoBaseHeaderKHR *)&loader), "initialize Android loader")) goto fail;
    XrInstanceCreateInfoAndroidKHR android = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM = activity->vm;
    android.applicationActivity = activity->clazz;
    const char *extensions[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo create = {XR_TYPE_INSTANCE_CREATE_INFO};
    create.next = &android;
    strcpy(create.applicationInfo.applicationName, "Code Veronica X");
    create.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,0);
    create.enabledExtensionCount = 2;
    create.enabledExtensionNames = extensions;
    if (!xr_ok(xrCreateInstance(&create, &t->instance), "create instance")) goto fail;
    XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system;
    if (!xr_ok(xrGetSystem(t->instance, &system_info, &system), "get headset")) goto fail;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements = NULL;
    XrGraphicsRequirementsOpenGLESKHR graphics = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!xr_ok(xrGetInstanceProcAddr(t->instance, "xrGetOpenGLESGraphicsRequirementsKHR",
               (PFN_xrVoidFunction *)&requirements), "get GLES requirements") || !requirements ||
        !xr_ok(requirements(t->instance, system, &graphics), "GLES requirements")) goto fail;
    XrGraphicsBindingOpenGLESAndroidKHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = egl->display;
    binding.context = egl->context;
    binding.config = egl->config;
    XrSessionCreateInfo session = {XR_TYPE_SESSION_CREATE_INFO};
    session.next = &binding;
    session.systemId = system;
    if (!xr_ok(xrCreateSession(t->instance, &session, &t->session), "create session")) goto fail;
    XrReferenceSpaceCreateInfo space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space.poseInReferenceSpace.orientation.w = 1;
    if (!xr_ok(xrCreateReferenceSpace(t->session, &space, &t->space), "create local space") ||
        !create_actions(t)) goto fail;
    error = "Could not create virtual room graphics";
    XrViewConfigurationView views[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t count;
    if (!xr_ok(xrEnumerateViewConfigurationViews(t->instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                2, &count, views), "stereo views") || count != 2 ||
        !create_chain(t, &t->screen, SCREEN_W, SCREEN_H)) goto fail;
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t w = views[i].recommendedImageRectWidth, h = views[i].recommendedImageRectHeight;
        // The environment is simple geometry. Keep room rendering inexpensive.
        if (w > 1440) w = 1440;
        if (h > 1440) h = 1440;
        if (!create_chain(t, &t->eye[i], w, h)) goto fail;
        t->views[i].type = XR_TYPE_VIEW;
    }
    if (!create_room(t) || !create_game_target(t)) goto fail;
    *out = t;
    __android_log_print(ANDROID_LOG_INFO,"RECVX-ARM64","OpenXR virtual room ready; Touch actions attached");
    return NULL;
fail:
    rx_theater_close(t);
    return error;
}

void rx_theater_target(RxTheater *t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t->game_fbo);
}

static unsigned char stick_byte(float value)
{
    if (fabsf(value) < 0.15f) return 128;
    return (unsigned char)(128 + fmaxf(-1, fminf(1, value))*127);
}

static int read_controls(RxTheater *t, RxPadInput *pad)
{
    XrActiveActionSet active = {t->actions, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (!xr_ok(xrSyncActions(t->session, &sync), "sync Touch")) return 0;
    uint16_t buttons = 0;
    static const uint16_t bits[] = {RX_PAD_CONFIRM, RX_PAD_CANCEL, RX_PAD_SQUARE, RX_PAD_TRIANGLE,
                                    RX_PAD_START, RX_PAD_L3, RX_PAD_R3};
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = t->action[i];
        if (i < STICK_LEFT) {
            XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};
            if (!xr_ok(xrGetActionStateBoolean(t->session, &get, &state), "Touch button")) return 0;
            if (state.isActive && state.currentState) buttons |= bits[i];
        } else if (i <= STICK_RIGHT) {
            XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
            if (!xr_ok(xrGetActionStateVector2f(t->session, &get, &state), "Touch stick")) return 0;
            unsigned axis = i == STICK_LEFT ? 2 : 0;
            pad->axes[axis] = stick_byte(state.isActive ? state.currentState.x : 0);
            // OpenXR's up is positive; DualShock's up is negative.
            pad->axes[axis+1] = stick_byte(state.isActive ? -state.currentState.y : 0);
        } else {
            XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};
            if (!xr_ok(xrGetActionStateFloat(t->session, &get, &state), "Touch trigger/grip")) return 0;
            float value = state.isActive ? state.currentState : 0;
            uint16_t bit = i == TRIGGER_LEFT ? RX_PAD_L2 : i == TRIGGER_RIGHT ? RX_PAD_R2 :
                           i == GRIP_LEFT ? RX_PAD_L1 : RX_PAD_R1;
            if (value >= (t->previous_buttons & bit ? 0.35f : 0.55f)) buttons |= bit;
        }
    }
    t->blocked_buttons &= buttons;
    buttons &= ~t->blocked_buttons;
    pad->pending |= buttons & ~t->previous_buttons;
    pad->keys = buttons;
    if (buttons != t->previous_buttons)
        __android_log_print(ANDROID_LOG_DEBUG,"RECVX-ARM64","Touch buttons %04x",buttons);
    t->previous_buttons = buttons;
    return 1;
}

int rx_theater_begin(RxTheater *t, RxPadInput *pad, int *focused, double *seconds)
{
    *focused = 0;
    *seconds = 0;
    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result;
    while ((result = xrPollEvent(t->instance, &event)) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *change = (const void *)&event;
            t->state = change->state;
            __android_log_print(ANDROID_LOG_INFO,"RECVX-ARM64","Theater session state %d",t->state);
            if (t->state == XR_SESSION_STATE_READY && !t->running) {
                XrSessionBeginInfo begin = {XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (!xr_ok(xrBeginSession(t->session, &begin), "begin session")) return -1;
                t->running = 1;
                t->previous_time = 0;
            } else if (t->state == XR_SESSION_STATE_STOPPING && t->running) {
                if (!xr_ok(xrEndSession(t->session), "end session")) return -1;
                t->running = 0;
            } else if (t->state == XR_SESSION_STATE_EXITING || t->state == XR_SESSION_STATE_LOSS_PENDING) {
                t->exiting = 1;
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            t->exiting = 1;
        }
        event = (XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (result != XR_EVENT_UNAVAILABLE && !xr_ok(result, "poll events")) return -1;
    if (!t->running || t->exiting) return 0;
    XrFrameWaitInfo wait = {XR_TYPE_FRAME_WAIT_INFO};
    t->frame = (XrFrameState){XR_TYPE_FRAME_STATE};
    if (!xr_ok(xrWaitFrame(t->session, &wait, &t->frame), "wait frame")) return -1;
    XrFrameBeginInfo begin = {XR_TYPE_FRAME_BEGIN_INFO};
    if (!xr_ok(xrBeginFrame(t->session, &begin), "begin frame")) return -1;
    t->begun = 1;
    *focused = t->state == XR_SESSION_STATE_FOCUSED;
    if (*focused) {
        if (!read_controls(t, pad)) return -1;
    } else {
        rx_pad_clear(pad);
        t->previous_buttons = 0;
    }
    double elapsed = t->previous_time ? (t->frame.predictedDisplayTime - t->previous_time) * 1e-9 :
                                       t->frame.predictedDisplayPeriod * 1e-9;
    *seconds = fmax(0, fmin(elapsed, 0.1));
    t->previous_time = t->frame.predictedDisplayTime;
    return 1;
}

static int acquire(TheaterChain *chain, uint32_t *index)
{
    XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    return xr_ok(xrAcquireSwapchainImage(chain->handle, &acquire, index), "acquire image") &&
           xr_ok(xrWaitSwapchainImage(chain->handle, &wait), "wait image");
}

static int release(TheaterChain *chain)
{
    XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return xr_ok(xrReleaseSwapchainImage(chain->handle, &release), "release image");
}

static void eye_matrix(const XrView *eye, float matrix[16])
{
    const XrPosef *p = &eye->pose;
    VrPose pose = {p->position.x,p->position.y,p->position.z,
                  p->orientation.x,p->orientation.y,p->orientation.z,p->orientation.w};
    float view[16], projection[16] = {0};
    vr_mat_rigid_inverse_pose(view, &pose);
    float l = tanf(eye->fov.angleLeft), r = tanf(eye->fov.angleRight);
    float d = tanf(eye->fov.angleDown), u = tanf(eye->fov.angleUp);
    const float near = 0.05f, far = 40;
    projection[0] = 2/(r-l);
    projection[5] = 2/(u-d);
    projection[8] = (r+l)/(r-l);
    projection[9] = (u+d)/(u-d);
    projection[10] = -(far+near)/(far-near);
    projection[11] = -1;
    projection[14] = -2*far*near/(far-near);
    vr_mat_mul(matrix, projection, view);
}

static int copy_game(RxTheater *t, GLuint destination)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t->eye_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, destination, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glUseProgram(t->copy_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t->game_texture);
    glBindVertexArray(t->room_vao);
    // The source contains display-encoded game RGB. Decode for the sRGB
    // attachment, whose automatic encoding restores those original bytes.
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return glGetError() == GL_NO_ERROR;
}

static int render_layers(RxTheater *t, XrCompositionLayerProjectionView views[2])
{
    uint32_t image;
    if (!acquire(&t->screen, &image)) return 0;
    int copied = copy_game(t, t->screen.images[image].image);
    glFlush();
    if (!release(&t->screen) || !copied) return 0;

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUseProgram(t->room_program);
    glBindVertexArray(t->room_vao);
    for (unsigned eye = 0; eye < 2; ++eye) {
        TheaterChain *chain = &t->eye[eye];
        if (!acquire(chain, &image)) return 0;
        glBindFramebuffer(GL_FRAMEBUFFER, t->eye_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              chain->images[image].image, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
        glViewport(0, 0, chain->width, chain->height);
        glClearColor(0.025f,0.03f,0.045f,1);
        glClear(GL_COLOR_BUFFER_BIT);
        float matrix[16];
        eye_matrix(&t->views[eye], matrix);
        glUniformMatrix4fv(t->room_matrix, 1, GL_FALSE, matrix);
        glDrawArrays(GL_TRIANGLES, 0, t->room_count);
        glFlush();
        if (!release(chain)) return 0;
        views[eye] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        views[eye].pose = t->views[eye].pose;
        views[eye].fov = t->views[eye].fov;
        views[eye].subImage.swapchain = chain->handle;
        views[eye].subImage.imageRect.extent = (XrExtent2Di){chain->width,chain->height};
    }
    glBindVertexArray(0);
    return glGetError() == GL_NO_ERROR;
}

int rx_theater_end(RxTheater *t)
{
    if (!t->begun) return 1;
    XrCompositionLayerProjectionView views[2];
    XrCompositionLayerProjection room = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    room.space = t->space;
    room.viewCount = 2;
    room.views = views;
    XrCompositionLayerQuad screen = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    screen.space = t->space;
    screen.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    screen.subImage.swapchain = t->screen.handle;
    screen.subImage.imageRect.extent = (XrExtent2Di){SCREEN_W, SCREEN_H};
    screen.pose.orientation.w = 1;
    screen.pose.position.z = -3.5f;
    screen.size = (XrExtent2Df){3.2f,2.4f};
    const XrCompositionLayerBaseHeader *layers[] = {(const void *)&room,(const void *)&screen};
    int rendered = 0, success = 1;
    if (t->frame.shouldRender) {
        XrViewLocateInfo locate = {XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = t->frame.predictedDisplayTime;
        locate.space = t->space;
        XrViewState state = {XR_TYPE_VIEW_STATE};
        uint32_t count;
        success = xr_ok(xrLocateViews(t->session, &locate, &state, 2, &count, t->views), "locate eyes");
        if (success && count == 2 && (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            success = render_layers(t, views);
            rendered = success;
        }
    }
    XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
    end.displayTime = t->frame.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = rendered ? 2 : 0;
    end.layers = rendered ? layers : NULL;
    t->begun = 0;
    int submitted = xr_ok(xrEndFrame(t->session, &end), "submit virtual room");
    if (submitted && rendered && !t->logged_frame) {
        __android_log_print(ANDROID_LOG_INFO,"RECVX-ARM64","Presented stereo virtual room and 4:3 game screen");
        t->logged_frame = 1;
    }
    return submitted && success;
}

int rx_theater_exiting(const RxTheater *t) { return t->exiting; }

void rx_theater_suppress_held_buttons(RxTheater *t)
{
    if (!t) return;
    t->blocked_buttons |= t->previous_buttons;
    t->previous_buttons = 0;
}
