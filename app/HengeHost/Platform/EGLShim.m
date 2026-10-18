/* EGL for Android code, on top of EAGL (OpenGL ES on iOS).
 *
 * One display, one config (RGBA8, 24-bit depth + 8-bit stencil). A window surface is a framebuffer
 * whose color renderbuffer is backed by the window's CAEAGLLayer; "framebuffer 0" in Android code is
 * mapped to it (glBindFramebuffer and friends below). All other GLES calls go straight to the
 * OpenGLES framework through the runtime's gl* fallback.
 */
#import <Foundation/Foundation.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <UIKit/UIKit.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "henge/henge.h"
#include "runtime/android.h"

typedef void *EGLHandle;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;

enum {
    EGL_SUCCESS = 0x3000, EGL_NOT_INITIALIZED = 0x3001, EGL_BAD_ALLOC = 0x3003, EGL_BAD_ATTRIBUTE = 0x3004,
    EGL_BAD_CONFIG = 0x3005, EGL_BAD_CONTEXT = 0x3006, EGL_BAD_MATCH = 0x3009, EGL_BAD_NATIVE_WINDOW = 0x300B,
    EGL_BAD_PARAMETER = 0x300C, EGL_BAD_SURFACE = 0x300D,
    EGL_BUFFER_SIZE = 0x3020, EGL_ALPHA_SIZE = 0x3021, EGL_BLUE_SIZE = 0x3022, EGL_GREEN_SIZE = 0x3023,
    EGL_RED_SIZE = 0x3024, EGL_DEPTH_SIZE = 0x3025, EGL_STENCIL_SIZE = 0x3026, EGL_CONFIG_CAVEAT = 0x3027,
    EGL_CONFIG_ID = 0x3028, EGL_LEVEL = 0x3029, EGL_NATIVE_RENDERABLE = 0x302D, EGL_NATIVE_VISUAL_ID = 0x302E,
    EGL_SAMPLES = 0x3031, EGL_SAMPLE_BUFFERS = 0x3032, EGL_SURFACE_TYPE = 0x3033, EGL_NONE = 0x3038,
    EGL_RENDERABLE_TYPE = 0x3040, EGL_CONFORMANT = 0x3042, EGL_HEIGHT = 0x3056, EGL_WIDTH = 0x3057,
    EGL_VENDOR = 0x3053, EGL_VERSION = 0x3054, EGL_EXTENSIONS = 0x3055, EGL_CLIENT_APIS = 0x308D,
    EGL_RENDER_BUFFER = 0x3086, EGL_BACK_BUFFER = 0x3084, EGL_CONTEXT_CLIENT_VERSION = 0x3098,
    EGL_OPENGL_ES_API = 0x30A0, EGL_WINDOW_BIT = 0x4, EGL_PBUFFER_BIT = 0x1, EGL_OPENGL_ES2_BIT = 0x4,
    EGL_OPENGL_ES3_BIT = 0x40,
};

#define HE_DISPLAY ((EGLHandle)1)
#define HE_CONFIG ((EGLHandle)1)

typedef struct {
    CFTypeRef ctx; /* EAGLContext, retained */
    int version;
} HEContext;

typedef struct {
    CFTypeRef layer; /* CAEAGLLayer, retained; NULL for a pbuffer */
    GLuint fbo, color, depth;
    int w, h;
    BOOL ready;
} HESurface;

static _Thread_local EGLint t_error = EGL_SUCCESS;
static _Thread_local HEContext *t_context;
static _Thread_local HESurface *t_surface;

static EGLBoolean fail(EGLint e)
{
    t_error = e;
    return 0;
}

static EGLBoolean ok(void)
{
    t_error = EGL_SUCCESS;
    return 1;
}

/* ---- display / config ---- */

static EGLHandle he_eglGetDisplay(EGLHandle native) { (void)native; return HE_DISPLAY; }

static EGLBoolean he_eglInitialize(EGLHandle dpy, EGLint *major, EGLint *minor)
{
    (void)dpy;
    if (major) *major = 1;
    if (minor) *minor = 4;
    return ok();
}

static EGLBoolean he_eglTerminate(EGLHandle dpy) { (void)dpy; return ok(); }
static EGLBoolean he_eglReleaseThread(void) { return ok(); }
static EGLBoolean he_eglBindAPI(EGLint api) { return api == EGL_OPENGL_ES_API ? ok() : fail(EGL_BAD_PARAMETER); }
static EGLint he_eglQueryAPI(void) { return EGL_OPENGL_ES_API; }
static EGLint he_eglGetError(void) { EGLint e = t_error; t_error = EGL_SUCCESS; return e; }
static EGLBoolean he_eglWait(void) { return ok(); }

static EGLBoolean he_eglGetConfigs(EGLHandle dpy, EGLHandle *configs, EGLint size, EGLint *num)
{
    (void)dpy;
    if (num) *num = 1;
    if (configs && size >= 1) configs[0] = HE_CONFIG;
    return ok();
}

static EGLBoolean he_eglChooseConfig(EGLHandle dpy, const EGLint *attribs, EGLHandle *configs, EGLint size, EGLint *num)
{
    (void)attribs; /* the one config is as good as it gets */
    return he_eglGetConfigs(dpy, configs, size, num);
}

static EGLBoolean he_eglGetConfigAttrib(EGLHandle dpy, EGLHandle config, EGLint attr, EGLint *value)
{
    (void)dpy;
    if (config != HE_CONFIG) return fail(EGL_BAD_CONFIG);
    switch (attr) {
    case EGL_BUFFER_SIZE: *value = 32; break;
    case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE: *value = 8; break;
    case EGL_DEPTH_SIZE: *value = 24; break;
    case EGL_STENCIL_SIZE: *value = 8; break;
    case EGL_CONFIG_ID: *value = 1; break;
    case EGL_NATIVE_VISUAL_ID: *value = 1; break; /* WINDOW_FORMAT_RGBA_8888 */
    case EGL_SURFACE_TYPE: *value = EGL_WINDOW_BIT | EGL_PBUFFER_BIT; break;
    case EGL_RENDERABLE_TYPE: case EGL_CONFORMANT: *value = EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT; break;
    default: *value = 0; break; /* caveat none, level 0, samples 0, ... */
    }
    return ok();
}

static const char *he_eglQueryString(EGLHandle dpy, EGLint name)
{
    (void)dpy;
    switch (name) {
    case EGL_VENDOR: return "henge";
    case EGL_VERSION: return "1.4 henge (EAGL)";
    case EGL_CLIENT_APIS: return "OpenGL_ES";
    case EGL_EXTENSIONS: return "";
    default: fail(EGL_BAD_PARAMETER); return NULL;
    }
}

/* ---- context ---- */

static EGLHandle he_eglCreateContext(EGLHandle dpy, EGLHandle config, EGLHandle share, const EGLint *attribs)
{
    (void)dpy; (void)config; (void)share;
    int version = 1;
    for (; attribs && *attribs != EGL_NONE; attribs += 2)
        if (attribs[0] == EGL_CONTEXT_CLIENT_VERSION) version = attribs[1];
    EAGLContext *ctx = version >= 3 ? [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3] : nil;
    if (ctx) {
        version = 3;
    } else {
        ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        version = 2;
    }
    if (!ctx) { fail(EGL_BAD_ALLOC); return NULL; }
    HEContext *c = calloc(1, sizeof *c);
    c->ctx = CFBridgingRetain(ctx);
    c->version = version;
    ok();
    return c;
}

static EGLBoolean he_eglDestroyContext(EGLHandle dpy, EGLHandle context)
{
    (void)dpy;
    HEContext *c = context;
    if (!c) return fail(EGL_BAD_CONTEXT);
    if (t_context == c) { [EAGLContext setCurrentContext:nil]; t_context = NULL; t_surface = NULL; }
    CFRelease(c->ctx);
    free(c);
    return ok();
}

/* ---- surfaces ---- */

static EGLHandle he_eglCreateWindowSurface(EGLHandle dpy, EGLHandle config, struct ANativeWindow *win, const EGLint *attribs)
{
    (void)dpy; (void)config; (void)attribs;
    if (!win || !win->layer) { fail(EGL_BAD_NATIVE_WINDOW); return NULL; }
    HESurface *s = calloc(1, sizeof *s);
    s->layer = CFRetain(win->layer);
    s->w = win->width;
    s->h = win->height;
    ok();
    return s;
}

static EGLHandle he_eglCreatePbufferSurface(EGLHandle dpy, EGLHandle config, const EGLint *attribs)
{
    (void)dpy; (void)config;
    HESurface *s = calloc(1, sizeof *s);
    s->w = s->h = 1;
    for (; attribs && *attribs != EGL_NONE; attribs += 2) {
        if (attribs[0] == EGL_WIDTH) s->w = attribs[1];
        if (attribs[0] == EGL_HEIGHT) s->h = attribs[1];
    }
    ok();
    return s;
}

static EGLBoolean he_eglDestroySurface(EGLHandle dpy, EGLHandle surface)
{
    (void)dpy;
    HESurface *s = surface;
    if (!s) return fail(EGL_BAD_SURFACE);
    if (s->ready && [EAGLContext currentContext]) {
        glDeleteFramebuffers(1, &s->fbo);
        glDeleteRenderbuffers(1, &s->color);
        glDeleteRenderbuffers(1, &s->depth);
    }
    if (t_surface == s) t_surface = NULL;
    if (s->layer) CFRelease(s->layer);
    free(s);
    return ok();
}

/* (Re)creates the color and depth/stencil storage at the layer's current size. A context must be current. */
static BOOL surface_allocate(HESurface *s)
{
    EAGLContext *ctx = [EAGLContext currentContext];
    if (!s->fbo) {
        glGenFramebuffers(1, &s->fbo);
        glGenRenderbuffers(1, &s->color);
        glGenRenderbuffers(1, &s->depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s->color);
    if (s->layer) {
        if (![ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:(__bridge CAEAGLLayer *)s->layer]) return NO;
    } else {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, s->w, s->h);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->color);
    GLint w = 0, h = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
    glBindRenderbuffer(GL_RENDERBUFFER, s->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s->depth);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s->depth);
    s->w = w;
    s->h = h;
    s->ready = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    return s->ready;
}

static EGLBoolean he_eglMakeCurrent(EGLHandle dpy, EGLHandle draw, EGLHandle read, EGLHandle context)
{
    (void)dpy; (void)read;
    HEContext *c = context;
    HESurface *s = draw;
    if (!c) {
        [EAGLContext setCurrentContext:nil];
        t_context = NULL;
        t_surface = NULL;
        return ok();
    }
    if (![EAGLContext setCurrentContext:(__bridge EAGLContext *)c->ctx]) return fail(EGL_BAD_CONTEXT);
    t_context = c;
    t_surface = s;
    if (s) {
        if (!s->ready && !surface_allocate(s)) return fail(EGL_BAD_ALLOC);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        glViewport(0, 0, s->w, s->h);
    }
    return ok();
}

static EGLBoolean he_eglSwapBuffers(EGLHandle dpy, EGLHandle surface)
{
    (void)dpy;
    HESurface *s = surface;
    if (!s || !t_context) return fail(EGL_BAD_SURFACE);
    EAGLContext *ctx = (__bridge EAGLContext *)t_context->ctx;
    if (!s->layer) return ok();
    GLint prev_fb = 0, prev_rb = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, s->color);
    BOOL presented = [ctx presentRenderbuffer:GL_RENDERBUFFER];
    /* The layer changed size (rotation, layout): reallocate before the next frame. */
    CAEAGLLayer *layer = (__bridge CAEAGLLayer *)s->layer;
    int pw = (int)(layer.bounds.size.width * layer.contentsScale), ph = (int)(layer.bounds.size.height * layer.contentsScale);
    if (pw > 0 && ph > 0 && (pw != s->w || ph != s->h)) surface_allocate(s);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
    return presented ? ok() : fail(EGL_BAD_SURFACE);
}

static EGLBoolean he_eglQuerySurface(EGLHandle dpy, EGLHandle surface, EGLint attr, EGLint *value)
{
    (void)dpy;
    HESurface *s = surface;
    if (!s) return fail(EGL_BAD_SURFACE);
    switch (attr) {
    case EGL_WIDTH: *value = s->w; break;
    case EGL_HEIGHT: *value = s->h; break;
    case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
    default: *value = 0; break;
    }
    return ok();
}

static EGLBoolean he_eglSwapInterval(EGLHandle dpy, EGLint interval) { (void)dpy; (void)interval; return ok(); }
static EGLBoolean he_eglSurfaceAttrib(EGLHandle d, EGLHandle s, EGLint a, EGLint v) { (void)d; (void)s; (void)a; (void)v; return ok(); }
static EGLHandle he_eglGetCurrentContext(void) { return t_context; }
static EGLHandle he_eglGetCurrentSurface(EGLint which) { (void)which; return t_surface; }
static EGLHandle he_eglGetCurrentDisplay(void) { return t_context ? HE_DISPLAY : NULL; }

static void *he_eglGetProcAddress(const char *name)
{
    void *p = NULL;
    /* the runtime's index first (our overrides), then the OpenGLES framework */
    if (!strncmp(name, "gl", 2)) p = dlsym(RTLD_DEFAULT, name);
    extern void *henge_shim_lookup(const char *);
    void *own = henge_shim_lookup(name);
    return own ? own : p;
}

/* ---- GLES calls whose meaning changes because Android has a default framebuffer ---- */

static GLuint default_fbo(void) { return t_surface ? t_surface->fbo : 0; }

static void he_glBindFramebuffer(GLenum target, GLuint fb) { glBindFramebuffer(target, fb ? fb : default_fbo()); }

static void he_glGetIntegerv(GLenum pname, GLint *data)
{
    glGetIntegerv(pname, data);
    if ((pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING) && default_fbo() && (GLuint)*data == default_fbo())
        *data = 0;
}

/* GL_COLOR / GL_DEPTH / GL_STENCIL name the default framebuffer's attachments. */
static void he_glInvalidateFramebuffer(GLenum target, GLsizei n, const GLenum *attachments)
{
    GLenum mapped[8];
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    if (n > 8 || (GLuint)bound != default_fbo()) { glInvalidateFramebuffer(target, n, attachments); return; }
    for (GLsizei i = 0; i < n; i++)
        mapped[i] = attachments[i] == 0x1800 ? GL_COLOR_ATTACHMENT0
                    : attachments[i] == 0x1801 ? GL_DEPTH_ATTACHMENT
                    : attachments[i] == 0x1802 ? GL_STENCIL_ATTACHMENT : attachments[i];
    glInvalidateFramebuffer(target, n, mapped);
}

#define E(n, f) {n, (void *)(f)}
static const struct henge_shim_entry k_egl[] = {
    E("eglGetDisplay", he_eglGetDisplay), E("eglInitialize", he_eglInitialize), E("eglTerminate", he_eglTerminate),
    E("eglReleaseThread", he_eglReleaseThread), E("eglBindAPI", he_eglBindAPI), E("eglQueryAPI", he_eglQueryAPI),
    E("eglGetError", he_eglGetError), E("eglWaitGL", he_eglWait), E("eglWaitNative", he_eglWait),
    E("eglGetConfigs", he_eglGetConfigs), E("eglChooseConfig", he_eglChooseConfig),
    E("eglGetConfigAttrib", he_eglGetConfigAttrib), E("eglQueryString", he_eglQueryString),
    E("eglCreateContext", he_eglCreateContext), E("eglDestroyContext", he_eglDestroyContext),
    E("eglCreateWindowSurface", he_eglCreateWindowSurface), E("eglCreatePbufferSurface", he_eglCreatePbufferSurface),
    E("eglDestroySurface", he_eglDestroySurface), E("eglMakeCurrent", he_eglMakeCurrent),
    E("eglSwapBuffers", he_eglSwapBuffers), E("eglQuerySurface", he_eglQuerySurface),
    E("eglSwapInterval", he_eglSwapInterval), E("eglSurfaceAttrib", he_eglSurfaceAttrib),
    E("eglGetCurrentContext", he_eglGetCurrentContext), E("eglGetCurrentSurface", he_eglGetCurrentSurface),
    E("eglGetCurrentDisplay", he_eglGetCurrentDisplay), E("eglGetProcAddress", he_eglGetProcAddress),
    E("glBindFramebuffer", he_glBindFramebuffer), E("glGetIntegerv", he_glGetIntegerv),
    E("glInvalidateFramebuffer", he_glInvalidateFramebuffer), E("glDiscardFramebufferEXT", he_glInvalidateFramebuffer),
};
#undef E

void henge_register_egl_shims(void)
{
    henge_shim_register(k_egl, sizeof k_egl / sizeof k_egl[0]);
}
