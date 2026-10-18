/* Catch: a small NativeActivity game. Drag to move the paddle, catch the falling squares, miss three
 * and it is over (tap to restart). Uses EGL + OpenGL ES 2 for drawing, touch input, AAudio for
 * sound effects and AAssetManager for assets/game.cfg. */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "catch", __VA_ARGS__)
#define MAX_ITEMS 10
#define LIVES 3

/* ---- sound: a decaying sine, set from the game thread, rendered in the audio callback ---- */

static volatile float g_freq, g_gain;
static float g_phase;
static AAudioStream *g_stream;

static aaudio_data_callback_result_t audio_cb(AAudioStream *stream, void *user, void *data, int32_t frames) {
    float *out = data;
    (void)user;
    int channels = AAudioStream_getChannelCount(stream);
    float rate = (float)AAudioStream_getSampleRate(stream);
    for (int i = 0; i < frames; i++) {
        float v = sinf(g_phase) * g_gain;
        g_phase += 6.2831853f * g_freq / rate;
        if (g_phase > 6.2831853f) g_phase -= 6.2831853f;
        g_gain *= 0.9996f;
        for (int c = 0; c < channels; c++) *out++ = v;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void audio_start(void) {
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    if (AAudioStreamBuilder_openStream(b, &g_stream) == AAUDIO_OK) {
        AAudioStream_requestStart(g_stream);
        LOG("audio started");
    }
    AAudioStreamBuilder_delete(b);
}

static void beep(float freq) {
    g_freq = freq;
    g_gain = 0.25f;
}

/* ---- GL ---- */

static const char *VS = "attribute vec2 p; uniform vec4 r; uniform vec2 res;\n"
                        "void main(){ vec2 px = r.xy + p * r.zw; gl_Position = vec4(px.x / res.x * 2.0 - 1.0, 1.0 - px.y / res.y * 2.0, 0.0, 1.0); }";
static const char *FS = "precision mediump float; uniform vec4 c; void main(){ gl_FragColor = c; }";

static GLuint g_prog, g_vbo;
static GLint u_r, u_res, u_c;

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOG("shader error: %s", log);
    }
    return s;
}

static void gl_init(void) {
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glBindAttribLocation(g_prog, 0, "p");
    glLinkProgram(g_prog);
    u_r = glGetUniformLocation(g_prog, "r");
    u_res = glGetUniformLocation(g_prog, "res");
    u_c = glGetUniformLocation(g_prog, "c");
    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    LOG("GL_VERSION %s | GL_RENDERER %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
}

static void rect(float x, float y, float w, float h, float r, float g, float b) {
    glUniform4f(u_r, x, y, w, h);
    glUniform4f(u_c, r, g, b, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* seven-segment digits: a b c d e f g */
static const unsigned char SEG[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};

static void digit(int d, float x, float y, float s, float r, float g, float b) {
    float t = s * 0.18f;
    unsigned m = SEG[d];
    if (m & 1)  rect(x, y, s, t, r, g, b);
    if (m & 2)  rect(x + s - t, y, t, s, r, g, b);
    if (m & 4)  rect(x + s - t, y + s, t, s, r, g, b);
    if (m & 8)  rect(x, y + 2 * s - t, s, t, r, g, b);
    if (m & 16) rect(x, y + s, t, s, r, g, b);
    if (m & 32) rect(x, y, t, s, r, g, b);
    if (m & 64) rect(x, y + s - t / 2, s, t, r, g, b);
}

static void number(int n, float x, float y, float s) {
    char buf[16];
    int len = snprintf(buf, sizeof buf, "%d", n);
    for (int i = 0; i < len; i++) digit(buf[i] - '0', x + i * s * 1.5f, y, s, 1, 1, 1);
}

/* ---- game ---- */

struct item { float x, y, v; int live; float r, g, b; };
static struct {
    EGLDisplay dpy; EGLSurface surf; EGLContext ctx;
    int w, h, have_gl;
    float bg[3], speed;
    float paddle_x, target_x;
    struct item items[MAX_ITEMS];
    int score, lives;
    float spawn_timer, flash;
    double last_touch; /* attract mode: the paddle plays itself when nobody touches the screen */
    int attract;
} G;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void load_config(struct android_app *app) {
    G.bg[0] = 0.05f; G.bg[1] = 0.08f; G.bg[2] = 0.18f;
    G.speed = 380;
    AAsset *a = AAssetManager_open(app->activity->assetManager, "game.cfg", AASSET_MODE_BUFFER);
    if (!a) { LOG("assets/game.cfg not found"); return; }
    char buf[256] = {0};
    AAsset_read(a, buf, sizeof buf - 1);
    AAsset_close(a);
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        sscanf(line, "bg=%f %f %f", &G.bg[0], &G.bg[1], &G.bg[2]);
        sscanf(line, "speed=%f", &G.speed);
    }
    LOG("game.cfg: speed=%.0f bg=%.2f %.2f %.2f", G.speed, G.bg[0], G.bg[1], G.bg[2]);
}

static void reset_game(void) {
    memset(G.items, 0, sizeof G.items);
    G.score = 0; G.lives = LIVES; G.spawn_timer = 0; G.flash = 0;
}

static int egl_init(struct android_app *app) {
    G.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(G.dpy, NULL, NULL);
    const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    eglChooseConfig(G.dpy, cfg_attr, &cfg, 1, &n);
    EGLint fmt;
    eglGetConfigAttrib(G.dpy, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, fmt);
    G.surf = eglCreateWindowSurface(G.dpy, cfg, app->window, NULL);
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    G.ctx = eglCreateContext(G.dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(G.dpy, G.surf, G.surf, G.ctx)) { LOG("eglMakeCurrent failed: 0x%x", eglGetError()); return -1; }
    eglQuerySurface(G.dpy, G.surf, EGL_WIDTH, &G.w);
    eglQuerySurface(G.dpy, G.surf, EGL_HEIGHT, &G.h);
    gl_init();
    G.paddle_x = G.target_x = G.w / 2.0f;
    LOG("surface %dx%d", G.w, G.h);
    G.have_gl = 1;
    return 0;
}

static void egl_term(void) {
    if (G.dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (G.ctx != EGL_NO_CONTEXT) eglDestroyContext(G.dpy, G.ctx);
        if (G.surf != EGL_NO_SURFACE) eglDestroySurface(G.dpy, G.surf);
    }
    G.dpy = EGL_NO_DISPLAY; G.ctx = EGL_NO_CONTEXT; G.surf = EGL_NO_SURFACE;
    G.have_gl = 0;
}

static double now(void);

static void update(float dt) {
    G.attract = now() - G.last_touch > 5.0;
    if (G.lives <= 0) {
        if (G.attract && now() - G.last_touch > 8.0) { reset_game(); G.last_touch = now() - 6.0; }
        return;
    }
    if (G.attract) { /* chase the lowest item */
        struct item *best = NULL;
        for (int i = 0; i < MAX_ITEMS; i++)
            if (G.items[i].live && (!best || G.items[i].y > best->y)) best = &G.items[i];
        if (best) G.target_x = best->x;
    }
    G.paddle_x += (G.target_x - G.paddle_x) * fminf(1.0f, dt * 18.0f);
    float size = G.w * 0.07f, pw = G.w * 0.24f, ph = G.w * 0.035f, py = G.h * 0.88f;
    G.spawn_timer -= dt;
    if (G.spawn_timer <= 0) {
        G.spawn_timer = fmaxf(0.35f, 0.9f - G.score * 0.02f);
        for (int i = 0; i < MAX_ITEMS; i++) if (!G.items[i].live) {
            G.items[i] = (struct item){size / 2 + frand() * (G.w - size), -size, G.speed * (0.8f + frand() * 0.5f + G.score * 0.01f), 1,
                                       0.5f + frand() * 0.5f, 0.4f + frand() * 0.6f, 0.3f + frand() * 0.7f};
            break;
        }
    }
    for (int i = 0; i < MAX_ITEMS; i++) {
        struct item *it = &G.items[i];
        if (!it->live) continue;
        it->y += it->v * dt;
        if (it->y + size >= py && it->y <= py + ph && fabsf(it->x - G.paddle_x) < (pw + size) / 2) {
            it->live = 0; G.score++; beep(660 + (G.score % 5) * 110);
        } else if (it->y > G.h) {
            it->live = 0; G.lives--; G.flash = 0.25f; beep(180);
            if (G.lives <= 0) LOG("game over, score %d", G.score);
        }
    }
    if (G.flash > 0) G.flash -= dt;
}

static void draw(void) {
    glViewport(0, 0, G.w, G.h);
    float f = G.flash > 0 ? 0.35f : 0;
    glClearColor(G.bg[0] + f, G.bg[1], G.bg[2], 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_prog);
    glUniform2f(u_res, (float)G.w, (float)G.h);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    float size = G.w * 0.07f, pw = G.w * 0.24f, ph = G.w * 0.035f, py = G.h * 0.88f;
    for (int i = 0; i < MAX_ITEMS; i++)
        if (G.items[i].live) rect(G.items[i].x - size / 2, G.items[i].y, size, size, G.items[i].r, G.items[i].g, G.items[i].b);
    rect(G.paddle_x - pw / 2, py, pw, ph, 0.95f, 0.55f, 0.2f);
    number(G.score, G.w * 0.05f, G.h * 0.14f, G.w * 0.045f);
    for (int i = 0; i < G.lives; i++) rect(G.w * 0.95f - (i + 1) * G.w * 0.06f, G.h * 0.145f, G.w * 0.045f, G.w * 0.045f, 0.9f, 0.2f, 0.25f);
    if (G.lives <= 0) {
        rect(G.w * 0.2f, G.h * 0.45f, G.w * 0.6f, G.h * 0.1f, 0.9f, 0.2f, 0.25f);
        rect(G.w * 0.23f, G.h * 0.47f, G.w * 0.54f, G.h * 0.06f, 0.05f, 0.05f, 0.1f);
    }
    eglSwapBuffers(G.dpy, G.surf);
}

static double now(void);
static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    int action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(ev, 0);
    G.target_x = x;
    G.last_touch = now();
    if (action == AMOTION_EVENT_ACTION_DOWN && G.lives <= 0) { reset_game(); beep(523); }
    return 1;
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window && egl_init(app) == 0) LOG("window ready");
        break;
    case APP_CMD_TERM_WINDOW: egl_term(); break;
    case APP_CMD_WINDOW_RESIZED:
        if (G.have_gl) { eglQuerySurface(G.dpy, G.surf, EGL_WIDTH, &G.w); eglQuerySurface(G.dpy, G.surf, EGL_HEIGHT, &G.h); }
        break;
    }
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void android_main(struct android_app *app) {
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    G.dpy = EGL_NO_DISPLAY; G.ctx = EGL_NO_CONTEXT; G.surf = EGL_NO_SURFACE;
    srand((unsigned)time(NULL));
    LOG("android_main");
    load_config(app);
    reset_game();
    G.last_touch = now();
    audio_start();
    double last = now(), stat = last;
    int frames = 0;
    for (;;) {
        int events;
        struct android_poll_source *src;
        while (ALooper_pollOnce(G.have_gl ? 0 : -1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) {
                if (g_stream) { AAudioStream_requestStop(g_stream); AAudioStream_close(g_stream); }
                egl_term();
                return;
            }
        }
        if (!G.have_gl) continue;
        double t = now();
        update((float)fminf(0.05, t - last));
        last = t;
        draw();
        frames++;
        if (t - stat >= 1.0) { LOG("fps %d score %d lives %d", frames, G.score, G.lives); frames = 0; stat = t; }
        usleep(8000);
    }
}
