/* JNI 1.6 over VM handles. No executable memory is allocated: jni_entry.S is the only
 * variable-signature call boundary. All VM data is protected by vm_enter/vm_leave. */
#include "vm.h"
#include "henge/henge.h"
#include "henge/jni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct registration { Method *method; void *previous; struct registration *next; };
struct local_env {
    const struct JNINativeInterface *functions; /* JNIEnv* is a pointer to this first field */
    Thread *thread;
    uint64_t serial, frames[128];
    unsigned nframes;
    struct loaded_library *loading;
};
struct reference {
    Ref ref;
    uint32_t generation;
    jobjectRefType kind;
    Thread *owner;
    uint64_t serial;
    size_t pin;
};
struct lease { const void *ptr; void *allocated; Ref ref; size_t pin; struct lease *next; };
struct loaded_library {
    henge_image *image; struct loaded_library *next;
    int status; /* 0 loading, 1 ready, -1 failed (images cannot be unloaded) */
    Thread *owner;
    struct registration *registrations;
};
struct jni_state {
    const struct JNIInvokeInterface *functions;
    VM *vm;
    struct reference *refs;
    size_t nrefs, cap;
    struct lease *leases;
    struct loaded_library *libraries;
};
static const struct JNINativeInterface functions;
static const struct JNIInvokeInterface invoke_functions;
static struct jni_state *state(VM *vm) { return vm->jni; }

static void fatal(const char *message) { fprintf(stderr, "henge JNI: %s\n", message); abort(); }
static Thread *enter(JNIEnv *env)
{
    struct local_env *e = (void *)env;
    if (!e || e->thread->jni_detached || !pthread_equal(e->thread->pthread, pthread_self())) fatal("JNIEnv used on the wrong thread");
    vm_enter(e->thread);
    return e->thread;
}
struct guard { Thread *t; };
static void leave(struct guard *g) { vm_leave(g->t); }
#define ENTER Thread *t = enter(env); struct guard guard __attribute__((cleanup(leave))) = {t}

static size_t pin(VM *vm, Ref r)
{
    for (size_t i = 0; i < vm->npins; i++) if (!vm->pins[i]) { vm->pins[i] = r; return i; }
    size_t i = vm->npins;
    vm_pin(vm, r);
    return i;
}
static struct reference *ref_cell(Thread *t, jobject object)
{
    if (!object) return NULL;
    uintptr_t token = (uintptr_t)object;
    size_t i = (uint32_t)token;
    struct jni_state *s = state(t->vm);
    if (!i || i > s->nrefs) fatal("invalid object reference");
    struct reference *r = &s->refs[i - 1];
    if (r->kind == JNIInvalidRefType || r->generation != token >> 32 ||
        (r->kind == JNILocalRefType && r->owner != t)) fatal("stale or foreign local reference");
    return r;
}
static Ref ref(Thread *t, jobject object) { struct reference *r = ref_cell(t, object); return r ? r->ref : 0; }
static jobject new_ref(Thread *t, Ref object, jobjectRefType kind)
{
    if (!object) return NULL;
    struct jni_state *s = state(t->vm);
    size_t i = 0;
    while (i < s->nrefs && s->refs[i].kind != JNIInvalidRefType) i++;
    if (i == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64;
        struct reference *next = realloc(s->refs, cap * sizeof *next);
        if (!next) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", "JNI references"); return NULL; }
        memset(next + s->cap, 0, (cap - s->cap) * sizeof *next);
        s->refs = next; s->cap = cap;
    }
    if (i == s->nrefs) s->nrefs++;
    struct reference *r = &s->refs[i];
    if (!++r->generation) ++r->generation;
    r->kind = kind; r->ref = object; r->owner = t;
    r->serial = ++((struct local_env *)t->jni_env)->serial;
    r->pin = kind == JNIWeakGlobalRefType ? SIZE_MAX : pin(t->vm, object);
    return (jobject)(uintptr_t)(((uint64_t)r->generation << 32) | (i + 1));
}
static jobject local(Thread *t, Ref object) { return new_ref(t, object, JNILocalRefType); }
static void delete_ref(Thread *t, jobject object, jobjectRefType kind)
{
    struct reference *r = ref_cell(t, object);
    if (!r) return;
    if (r->kind != kind) fatal("wrong Delete*Ref function");
    if (r->pin != SIZE_MAX) t->vm->pins[r->pin] = 0;
    r->kind = JNIInvalidRefType;
}
static Class *mirror(Thread *t, jclass clazz)
{
    Object *o = vm_obj(t->vm, ref(t, clazz));
    if (!o || o->cls != t->vm->cls_class) { vm_throw(t, "Ljava/lang/IllegalArgumentException;", "not a Class"); return NULL; }
    return (Class *)(uintptr_t)vm_wide(o->u.words);
}
static Object *object(Thread *t, jobject value)
{
    Object *o = vm_obj(t->vm, ref(t, value));
    if (!o) vm_throw(t, "Ljava/lang/NullPointerException;", NULL);
    return o;
}
static int range(Thread *t, Object *o, jsize start, jsize count)
{
    if (!o) return 0;
    if (start < 0 || count < 0 || (uint32_t)start > o->length || (uint32_t)count > o->length - (uint32_t)start) {
        vm_throw(t, "Ljava/lang/ArrayIndexOutOfBoundsException;", "JNI region"); return 0;
    }
    return 1;
}
static jint push_frame(Thread *t, jint capacity)
{
    struct local_env *e = t->jni_env;
    if (capacity < 0 || e->nframes == 128) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", "JNI local frame"); return JNI_ERR; }
    e->frames[e->nframes++] = e->serial;
    return JNI_OK;
}
static Ref pop_frame(Thread *t, Ref result)
{
    struct local_env *e = t->jni_env;
    if (!e->nframes) fatal("PopLocalFrame without PushLocalFrame");
    uint64_t mark = e->frames[--e->nframes];
    struct jni_state *s = state(t->vm);
    for (size_t i = 0; i < s->nrefs; i++) {
        struct reference *r = &s->refs[i];
        if (r->kind == JNILocalRefType && r->owner == t && r->serial > mark) {
            t->vm->pins[r->pin] = 0; r->kind = JNIInvalidRefType;
        }
    }
    return result;
}

static jint j_GetVersion(JNIEnv *env) { (void)env; return JNI_VERSION_1_6; }
static jclass j_FindClass(JNIEnv *env, const char *name)
{
    ENTER;
    if (!name) { vm_throw(t, "Ljava/lang/NullPointerException;", "class name"); return NULL; }
    size_t n = strlen(name);
    char *desc = malloc(n + 3);
    if (!desc) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return NULL; }
    if (name[0] == '[') strcpy(desc, name); else sprintf(desc, "L%s;", name);
    Class *c = vm_find_class(t->vm, desc);
    if (!c) vm_throw(t, "Ljava/lang/NoClassDefFoundError;", desc);
    free(desc);
    return c && vm_init_class(t, c) == 0 ? (jclass)local(t, vm_class_object(t, c)) : NULL;
}
static jclass j_GetSuperclass(JNIEnv *env, jclass clazz)
{ ENTER; Class *c = mirror(t, clazz); return c && c->super && !(c->access & ACC_INTERFACE) ? (jclass)local(t, vm_class_object(t, c->super)) : NULL; }
static jboolean j_IsAssignableFrom(JNIEnv *env, jclass from, jclass to)
{ ENTER; Class *a = mirror(t, from), *b = mirror(t, to); return a && b && vm_instance_of(a, b); }
static jclass j_GetObjectClass(JNIEnv *env, jobject value)
{ ENTER; Object *o = object(t, value); return o ? (jclass)local(t, vm_class_object(t, o->cls)) : NULL; }
static jboolean j_IsInstanceOf(JNIEnv *env, jobject value, jclass clazz)
{ ENTER; Class *c = mirror(t, clazz); Ref r = ref(t, value); return c && (!r || vm_instance_of(vm_obj(t->vm, r)->cls, c)); }
static jobject j_AllocObject(JNIEnv *env, jclass clazz)
{ ENTER; Class *c = mirror(t, clazz); if (!c || vm_init_class(t, c)) return NULL; if (c->access & (ACC_ABSTRACT | ACC_INTERFACE)) { vm_throw(t, "Ljava/lang/InstantiationError;", c->name); return NULL; } return local(t, vm_new_object(t, c)); }
static jint j_Throw(JNIEnv *env, jthrowable exception)
{ ENTER; Ref r = ref(t, exception); if (!r) return JNI_ERR; t->exception = r; return JNI_OK; }
static jint j_ThrowNew(JNIEnv *env, jclass clazz, const char *message)
{ ENTER; Class *c = mirror(t, clazz); if (!c) return JNI_ERR; vm_throw(t, c->name, message); return t->exception ? JNI_OK : JNI_ERR; }
static jthrowable j_ExceptionOccurred(JNIEnv *env) { ENTER; return (jthrowable)local(t, t->exception); }
static void j_ExceptionClear(JNIEnv *env) { ENTER; t->exception = 0; }
static jboolean j_ExceptionCheck(JNIEnv *env) { ENTER; return t->exception != 0; }
static void j_ExceptionDescribe(JNIEnv *env)
{ ENTER; Object *o = vm_obj(t->vm, t->exception); if (o) fprintf(stderr, "henge JNI exception: %s\n", o->cls->name); t->exception = 0; }
static void j_FatalError(JNIEnv *env, const char *message) { (void)env; fatal(message); }
static jint j_PushLocalFrame(JNIEnv *env, jint capacity) { ENTER; return push_frame(t, capacity); }
static jobject j_PopLocalFrame(JNIEnv *env, jobject result) { ENTER; Ref r = ref(t, result); return local(t, pop_frame(t, r)); }
static jobject j_NewLocalRef(JNIEnv *env, jobject value) { ENTER; return local(t, ref(t, value)); }
static jobject j_NewGlobalRef(JNIEnv *env, jobject value) { ENTER; return new_ref(t, ref(t, value), JNIGlobalRefType); }
static jweak j_NewWeakGlobalRef(JNIEnv *env, jobject value) { ENTER; return new_ref(t, ref(t, value), JNIWeakGlobalRefType); }
static void j_DeleteLocalRef(JNIEnv *env, jobject value) { ENTER; delete_ref(t, value, JNILocalRefType); }
static void j_DeleteGlobalRef(JNIEnv *env, jobject value) { ENTER; delete_ref(t, value, JNIGlobalRefType); }
static void j_DeleteWeakGlobalRef(JNIEnv *env, jweak value) { ENTER; delete_ref(t, value, JNIWeakGlobalRefType); }
static jboolean j_IsSameObject(JNIEnv *env, jobject a, jobject b) { ENTER; return ref(t, a) == ref(t, b); }
static jobjectRefType j_GetObjectRefType(JNIEnv *env, jobject value) { ENTER; struct reference *r = ref_cell(t, value); return r ? r->kind : JNIInvalidRefType; }
static jint j_EnsureLocalCapacity(JNIEnv *env, jint capacity)
{ ENTER; if (capacity < 0 || capacity > (1 << 20)) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", "JNI local capacity"); return JNI_ERR; } return JNI_OK; }

static jmethodID method_id(Thread *t, jclass clazz, const char *name, const char *sig, int is_static)
{
    Class *c = mirror(t, clazz);
    if (!c || vm_init_class(t, c)) return NULL;
    Method *m = name && sig ? vm_find_method(c, name, sig) : NULL;
    if (!m || !!(m->access & ACC_STATIC) != is_static) { vm_throw(t, "Ljava/lang/NoSuchMethodError;", name); return NULL; }
    return (jmethodID)m;
}
static jmethodID j_GetMethodID(JNIEnv *env, jclass c, const char *name, const char *sig) { ENTER; return method_id(t, c, name, sig, 0); }
static jmethodID j_GetStaticMethodID(JNIEnv *env, jclass c, const char *name, const char *sig) { ENTER; return method_id(t, c, name, sig, 1); }
static jfieldID field_id(Thread *t, jclass clazz, const char *name, const char *sig, int is_static)
{
    Class *c = mirror(t, clazz);
    if (!c || vm_init_class(t, c)) return NULL;
    for (; c; c = c->super) for (unsigned i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        if (!!(f->access & ACC_STATIC) == is_static && name && sig && !strcmp(f->name, name) && !strcmp(f->type_desc, sig)) return (jfieldID)f;
    }
    vm_throw(t, "Ljava/lang/NoSuchFieldError;", name);
    return NULL;
}
static jfieldID j_GetFieldID(JNIEnv *env, jclass c, const char *name, const char *sig) { ENTER; return field_id(t, c, name, sig, 0); }
static jfieldID j_GetStaticFieldID(JNIEnv *env, jclass c, const char *name, const char *sig) { ENTER; return field_id(t, c, name, sig, 1); }
static uint32_t *field_address(Thread *t, jobject value, jfieldID id, int is_static)
{
    Field *f = (Field *)id;
    if (!f || !!(f->access & ACC_STATIC) != is_static) fatal("invalid field ID");
    if (is_static) return f->cls->statics + f->offset;
    Object *o = object(t, value);
    if (o && !vm_instance_of(o->cls, f->cls)) fatal("field ID does not belong to object");
    return o ? o->u.words + f->offset : NULL;
}

/* AAPCS64 va_list; a V entry receives a pointer to this >16-byte aggregate. */
struct android_va { uint8_t *stack, *gr_top, *vr_top; int32_t gr_offs, vr_offs; };
static uint64_t va_next(struct android_va *v, int fp)
{
    uint8_t *p;
    int32_t *offset = fp ? &v->vr_offs : &v->gr_offs;
    if (*offset < 0) { p = (fp ? v->vr_top : v->gr_top) + *offset; *offset += fp ? 16 : 8; }
    else { p = v->stack; v->stack += 8; }
    uint64_t bits; memcpy(&bits, p, 8); return bits;
}
static const char *next_type(const char *p)
{ while (*p == '[') p++; if (*p == 'L') { const char *end = strchr(p, ';'); return end ? end + 1 : p + strlen(p); } return p + 1; }
static uint64_t call_method(Thread *t, jobject receiver, jmethodID id, const jvalue *values, struct android_va *va, int mode)
{
    Method *m = (Method *)id;
    if (!m || t->exception) return 0;
    int is_static = (m->access & ACC_STATIC) != 0;
    if (is_static != (mode == 2)) fatal("static/instance method mismatch");
    if (vm_init_class(t, m->cls)) return 0;
    uint32_t *words = calloc(m->arg_words ? m->arg_words : 1, sizeof *words);
    if (!words) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return 0; }
    size_t word = 0, n = 0;
    if (!is_static) {
        Ref r = ref(t, receiver);
        if (!r) { free(words); vm_throw(t, "Ljava/lang/NullPointerException;", NULL); return 0; }
        words[word++] = r;
        if (mode == 0) { Method *v = vm_lookup_virtual(vm_obj(t->vm, r)->cls, m->sig); if (v) m = v; }
    }
    for (const char *p = strchr(m->sig, '(') + 1; *p != ')'; p = next_type(p), n++) {
        char type = *p;
        uint64_t bits = 0;
        if (va) {
            bits = va_next(va, type == 'F' || type == 'D');
            if (type == 'F') { double d; memcpy(&d, &bits, 8); bits = f2u((float)d); }
        } else if (values) {
            if (type == 'L' || type == '[') bits = (uintptr_t)values[n].l;
            else if (type == 'D' || type == 'J') memcpy(&bits, &values[n], 8);
            else memcpy(&bits, &values[n], 4);
        }
        if (type == 'L' || type == '[') bits = ref(t, (jobject)(uintptr_t)bits);
        words[word++] = (uint32_t)bits;
        if (type == 'D' || type == 'J') words[word++] = (uint32_t)(bits >> 32);
    }
    uint64_t result = vm_invoke(t, m, words);
    free(words);
    return result;
}

static jint j_RegisterNatives(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint count)
{
    ENTER;
    Class *c = mirror(t, clazz);
    if (!c || count < 0 || (!methods && count)) return JNI_ERR;
    /* Validate the whole batch before mutating any method. */
    for (jint i = 0; i < count; i++) {
        Method *m = methods[i].name && methods[i].signature ? vm_find_method(c, methods[i].name, methods[i].signature) : NULL;
        if (!m || !(m->access & ACC_NATIVE) || !methods[i].fnPtr) { vm_throw(t, "Ljava/lang/NoSuchMethodError;", methods[i].name); return JNI_ERR; }
    }
    struct loaded_library *loading = ((struct local_env *)t->jni_env)->loading;
    struct registration *changes = NULL;
    for (jint i = 0; i < count; i++) {
        Method *m = vm_find_method(c, methods[i].name, methods[i].signature);
        struct registration *r = malloc(sizeof *r);
        if (!r) {
            while (changes) { r = changes; changes = r->next; free(r); }
            vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return JNI_ERR;
        }
        *r = (struct registration){m, m->jni_native, changes}; changes = r;
    }
    for (jint i = 0; i < count; i++) vm_find_method(c, methods[i].name, methods[i].signature)->jni_native = methods[i].fnPtr;
    while (changes) {
        struct registration *r = changes; changes = r->next;
        if (loading) { r->next = loading->registrations; loading->registrations = r; }
        else free(r);
    }
    return JNI_OK;
}
static jint j_UnregisterNatives(JNIEnv *env, jclass clazz)
{ ENTER; Class *c = mirror(t, clazz); if (!c) return JNI_ERR; for (unsigned i = 0; i < c->nmethods; i++) c->methods[i].jni_native = NULL; return JNI_OK; }
static jint j_GetJavaVM(JNIEnv *env, JavaVM **out)
{ ENTER; if (!out) return JNI_ERR; *out = (JavaVM *)state(t->vm); return JNI_OK; }

static struct lease *lease(Thread *t, Ref r, const void *ptr, void *allocated)
{
    struct lease *l = malloc(sizeof *l);
    if (!l) { free(allocated); vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return NULL; }
    *l = (struct lease){ptr, allocated, r, pin(t->vm, r), state(t->vm)->leases};
    state(t->vm)->leases = l;
    return l;
}
static void release(Thread *t, Ref r, const void *ptr, int keep)
{
    struct lease **link = &state(t->vm)->leases;
    while (*link && ((*link)->ptr != ptr || (*link)->ref != r)) link = &(*link)->next;
    if (!*link) fatal("release of unacquired string/array contents");
    if (keep) return;
    struct lease *l = *link; *link = l->next;
    t->vm->pins[l->pin] = 0; free(l->allocated); free(l);
}
static Object *string(Thread *t, jstring value)
{ Object *o = object(t, value); if (o && o->kind != OBJ_STRING) fatal("expected a string"); return o; }
static size_t mutf8(char *out, const uint16_t *chars, size_t n)
{
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned c = chars[i];
        if (c && c < 128) { if (out) out[k] = (char)c; k++; }
        else if (c < 2048) { if (out) { out[k] = (char)(0xc0 | c >> 6); out[k + 1] = (char)(0x80 | (c & 63)); } k += 2; }
        else { if (out) { out[k] = (char)(0xe0 | c >> 12); out[k + 1] = (char)(0x80 | ((c >> 6) & 63)); out[k + 2] = (char)(0x80 | (c & 63)); } k += 3; }
    }
    return k;
}
static jstring j_NewString(JNIEnv *env, const jchar *chars, jsize n)
{ ENTER; if (n < 0 || (!chars && n)) { vm_throw(t, "Ljava/lang/IllegalArgumentException;", "string length"); return NULL; } return (jstring)local(t, vm_new_string_utf16(t, chars, (uint32_t)n)); }
static jstring j_NewStringUTF(JNIEnv *env, const char *chars)
{ ENTER; return chars ? (jstring)local(t, vm_new_string_utf8(t, chars)) : NULL; }
static jsize j_GetStringLength(JNIEnv *env, jstring value) { ENTER; Object *o = string(t, value); return o ? (jsize)o->length : 0; }
static jsize j_GetStringUTFLength(JNIEnv *env, jstring value) { ENTER; Object *o = string(t, value); return o ? (jsize)mutf8(NULL, o->u.chars, o->length) : 0; }
static const jchar *j_GetStringChars(JNIEnv *env, jstring value, jboolean *copy)
{ ENTER; Object *o = string(t, value); if (!o || !lease(t, ref(t, value), o->u.chars, NULL)) return NULL; if (copy) *copy = JNI_FALSE; return o->u.chars; }
static void j_ReleaseStringChars(JNIEnv *env, jstring value, const jchar *chars) { ENTER; release(t, ref(t, value), chars, 0); }
static const char *j_GetStringUTFChars(JNIEnv *env, jstring value, jboolean *copy)
{
    ENTER; Object *o = string(t, value); if (!o) return NULL;
    char *buf = malloc(mutf8(NULL, o->u.chars, o->length) + 1);
    if (!buf) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return NULL; }
    buf[mutf8(buf, o->u.chars, o->length)] = 0;
    if (!lease(t, ref(t, value), buf, buf)) return NULL;
    if (copy) *copy = JNI_TRUE;
    return buf;
}
static void j_ReleaseStringUTFChars(JNIEnv *env, jstring value, const char *chars) { ENTER; release(t, ref(t, value), chars, 0); }
static void j_GetStringRegion(JNIEnv *env, jstring value, jsize start, jsize n, jchar *out)
{ ENTER; Object *o = string(t, value); if (range(t, o, start, n) && n) memcpy(out, o->u.chars + start, (size_t)n * 2); }
static void j_GetStringUTFRegion(JNIEnv *env, jstring value, jsize start, jsize n, char *out)
{ ENTER; Object *o = string(t, value); if (range(t, o, start, n)) mutf8(out, o->u.chars + start, (size_t)n); }
static Object *array(Thread *t, jarray value, char kind)
{ Object *o = object(t, value); if (o && (o->kind != OBJ_ARRAY || (kind && o->cls->array_kind != kind))) fatal("array element type mismatch"); return o; }
static jsize j_GetArrayLength(JNIEnv *env, jarray value) { ENTER; Object *o = array(t, value, 0); return o ? (jsize)o->length : 0; }
static jobjectArray j_NewObjectArray(JNIEnv *env, jsize n, jclass clazz, jobject initial)
{
    ENTER; Class *c = mirror(t, clazz); if (!c) return NULL;
    if (n < 0) { vm_throw(t, "Ljava/lang/NegativeArraySizeException;", NULL); return NULL; }
    char *desc = malloc(strlen(c->name) + 2); if (!desc) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return NULL; }
    sprintf(desc, "[%s", c->name); Class *ac = vm_array_class(t->vm, desc); free(desc);
    Ref r = vm_new_array(t, ac, (uint32_t)n), value = ref(t, initial);
    Object *o = vm_obj(t->vm, r);
    if (!o) return NULL;
    if (value && !vm_instance_of(vm_obj(t->vm, value)->cls, c)) { vm_throw(t, "Ljava/lang/ArrayStoreException;", NULL); return NULL; }
    for (jsize i = 0; i < n; i++) o->u.words[i] = value;
    return (jobjectArray)local(t, r);
}
static jobject j_GetObjectArrayElement(JNIEnv *env, jobjectArray value, jsize index)
{ ENTER; Object *o = array(t, value, 0); if (o && o->cls->array_kind != 'L' && o->cls->array_kind != '[') fatal("expected object array"); return range(t, o, index, 1) ? local(t, o->u.words[index]) : NULL; }
static void j_SetObjectArrayElement(JNIEnv *env, jobjectArray value, jsize index, jobject item)
{
    ENTER; Object *o = array(t, value, 0); if (!range(t, o, index, 1)) return;
    if (o->cls->array_kind != 'L' && o->cls->array_kind != '[') fatal("expected object array");
    Ref r = ref(t, item);
    if (r && !vm_instance_of(vm_obj(t->vm, r)->cls, o->cls->component)) { vm_throw(t, "Ljava/lang/ArrayStoreException;", NULL); return; }
    o->u.words[index] = r;
}
static void *j_GetPrimitiveArrayCritical(JNIEnv *env, jarray value, jboolean *copy)
{ ENTER; Object *o = array(t, value, 0); if (!o) return NULL; if (o->cls->array_kind == 'L' || o->cls->array_kind == '[') fatal("expected primitive array"); if (!lease(t, ref(t, value), o->u.bytes, NULL)) return NULL; if (copy) *copy = JNI_FALSE; return o->u.bytes; }
static void j_ReleasePrimitiveArrayCritical(JNIEnv *env, jarray value, void *ptr, jint mode)
{ ENTER; release(t, ref(t, value), ptr, mode == JNI_COMMIT); }

/* Generated typed entry points, array/field access, and AAPCS64 variadic declarations. */
#include "jni_generated.inc"

void *vm_jni_env(Thread *t)
{
    if (!t->jni_env) {
        struct local_env *e = calloc(1, sizeof *e);
        if (!e) fatal("out of memory creating JNIEnv");
        e->functions = &functions; e->thread = t; t->jni_env = e;
    }
    return t->jni_env;
}
void *vm_java_vm(VM *vm) { return state(vm); }
static jint v_GetEnv(JavaVM *java_vm, void **out, jint version)
{
    struct jni_state *s = (void *)java_vm;
    if (!out) return JNI_ERR;
    *out = NULL;
    if (version != JNI_VERSION_1_2 && version != JNI_VERSION_1_4 && version != JNI_VERSION_1_6) return JNI_EVERSION;
    pthread_mutex_lock(&s->vm->lock);
    for (Thread *t = s->vm->threads; t; t = t->next) if (!t->jni_detached && pthread_equal(t->pthread, pthread_self())) {
        *out = vm_jni_env(t); pthread_mutex_unlock(&s->vm->lock); return JNI_OK;
    }
    pthread_mutex_unlock(&s->vm->lock);
    return JNI_EDETACHED;
}
static jint v_Attach(JavaVM *java_vm, JNIEnv **out, void *args)
{
    struct jni_state *s = (void *)java_vm;
    JavaVMAttachArgs *a = args;
    if (!out) return JNI_ERR;
    if (a && a->version != JNI_VERSION_1_2 && a->version != JNI_VERSION_1_4 && a->version != JNI_VERSION_1_6) return JNI_EVERSION;
    pthread_mutex_lock(&s->vm->lock);
    Thread *t = vm_attach_thread(s->vm);
    *out = vm_jni_env(t);
    pthread_mutex_unlock(&s->vm->lock);
    return JNI_OK;
}
static jint v_Detach(JavaVM *java_vm)
{
    struct jni_state *s = (void *)java_vm;
    pthread_mutex_lock(&s->vm->lock);
    Thread **link = &s->vm->threads;
    while (*link && ((*link)->jni_detached || !pthread_equal((*link)->pthread, pthread_self()))) link = &(*link)->next;
    Thread *t = *link;
    if (!t) { pthread_mutex_unlock(&s->vm->lock); return JNI_EDETACHED; }
    if (t == s->vm->main_thread || t->depth || t->lock_depth) { pthread_mutex_unlock(&s->vm->lock); return JNI_ERR; }
    for (size_t i = 0; i < s->nrefs; i++) if (s->refs[i].kind == JNILocalRefType && s->refs[i].owner == t) {
        s->vm->pins[s->refs[i].pin] = 0; s->refs[i].kind = JNIInvalidRefType;
    }
    *link = t->next;
    free(t->jni_env); free(t->stack); free(t);
    pthread_mutex_unlock(&s->vm->lock);
    return JNI_OK;
}
static jint v_Destroy(JavaVM *vm) { (void)vm; return JNI_ERR; /* lifetime belongs to vm_host */ }
static const struct JNIInvokeInterface invoke_functions = {
    .DestroyJavaVM = v_Destroy, .AttachCurrentThread = v_Attach, .DetachCurrentThread = v_Detach,
    .GetEnv = v_GetEnv, .AttachCurrentThreadAsDaemon = v_Attach,
};

int vm_jni_init(VM *vm)
{
    struct jni_state *s = calloc(1, sizeof *s);
    if (!s) return -1;
    s->functions = &invoke_functions; s->vm = vm; vm->jni = s;
    henge_jni_bind(s, vm_jni_env(vm_main_thread(vm)));
    return 0;
}
void vm_jni_sweep_weak(VM *vm)
{
    struct jni_state *s = state(vm); if (!s) return;
    for (size_t i = 0; i < s->nrefs; i++) if (s->refs[i].kind == JNIWeakGlobalRefType) {
        Object *o = vm_obj(vm, s->refs[i].ref);
        if (!o || !o->mark) s->refs[i].ref = 0;
    }
}
void vm_jni_destroy(VM *vm)
{
    struct jni_state *s = state(vm); if (!s) return;
    henge_jni_bind(NULL, NULL);
    while (s->leases) { struct lease *l = s->leases; s->leases = l->next; free(l->allocated); free(l); }
    while (s->libraries) { struct loaded_library *l = s->libraries; s->libraries = l->next; free(l); }
    for (Thread *t = vm->threads; t; t = t->next) { free(t->jni_env); t->jni_env = NULL; }
    free(s->refs); free(s); vm->jni = NULL;
}

struct call_frame { uint64_t gp[8], fp[8], stack[256]; size_t nstack; };
extern uint64_t vm_jni_call(void *function, struct call_frame *frame, int return_type);
static void call_arg(struct call_frame *f, uint64_t value, int fp, unsigned *gp_count, unsigned *fp_count)
{
    unsigned *n = fp ? fp_count : gp_count;
    if (*n < 8) (fp ? f->fp : f->gp)[(*n)++] = value;
    else { if (f->nstack == 256) fatal("native signature exceeds bridge capacity"); f->stack[f->nstack++] = value; }
}
static void jni_name(char *out, size_t cap, const char *text, int descriptor)
{
    size_t n = strlen(out);
    for (const unsigned char *p = (const unsigned char *)text; *p && n + 8 < cap; p++) {
        if (descriptor && (*p == '(' || *p == ')')) { if (*p == ')') break; continue; }
        if (*p == '/' || *p == '.') out[n++] = '_';
        else if (*p == '_') { out[n++] = '_'; out[n++] = '1'; }
        else if (*p == ';') { out[n++] = '_'; out[n++] = '2'; }
        else if (*p == '[') { out[n++] = '_'; out[n++] = '3'; }
        else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) out[n++] = (char)*p;
        else {
            unsigned c = *p;
            if ((c & 0xe0) == 0xc0 && p[1]) { c = ((c & 31) << 6) | (p[1] & 63); p++; }
            else if ((c & 0xf0) == 0xe0 && p[1] && p[2]) { c = ((c & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 2; }
            n += (size_t)snprintf(out + n, cap - n, "_0%04x", c);
        }
    }
    out[n] = 0;
}
static void *find_native(Thread *t, Method *m)
{
    VM *vm = t->vm;
    char name[2048] = "Java_", cls[1024];
    snprintf(cls, sizeof cls, "%.*s", (int)strlen(m->cls->name) - 2, m->cls->name + 1);
    jni_name(name, sizeof name, cls, 0);
    strcat(name, "_"); jni_name(name, sizeof name, m->name, 0);
    for (struct loaded_library *l = state(vm)->libraries; l; l = l->next) {
        if (l->status < 0 || (!l->status && l->owner != t)) continue;
        void *p = henge_dlsym(l->image, name); if (p) return p;
    }
    strcat(name, "__"); jni_name(name, sizeof name, strchr(m->sig, '('), 1);
    for (struct loaded_library *l = state(vm)->libraries; l; l = l->next) {
        if (l->status < 0 || (!l->status && l->owner != t)) continue;
        void *p = henge_dlsym(l->image, name); if (p) return p;
    }
    return NULL;
}
uint64_t vm_jni_invoke(Thread *t, Method *m, uint32_t *args)
{
    if (!m->jni_native) m->jni_native = find_native(t, m);
    if (!m->jni_native) { vm_throw(t, "Ljava/lang/UnsatisfiedLinkError;", m->sig); return 0; }
    JNIEnv *env = vm_jni_env(t);
    if (push_frame(t, m->arg_words + 16)) return 0;
    struct call_frame f = {0}; unsigned ng = 0, nf = 0; size_t w = 0;
    call_arg(&f, (uintptr_t)env, 0, &ng, &nf);
    Ref self = (m->access & ACC_STATIC) ? vm_class_object(t, m->cls) : args[w++];
    call_arg(&f, (uintptr_t)local(t, self), 0, &ng, &nf);
    for (const char *p = strchr(m->sig, '(') + 1; *p != ')'; p = next_type(p)) {
        char type = *p;
        uint64_t bits = args[w++];
        if (type == 'J' || type == 'D') bits |= (uint64_t)args[w++] << 32;
        if (type == 'L' || type == '[') bits = (uintptr_t)local(t, (Ref)bits);
        call_arg(&f, bits, type == 'F' || type == 'D', &ng, &nf);
    }
    char result_type = strchr(m->sig, ')')[1];
    unsigned depth = vm_suspend(t);
    uint64_t result = vm_jni_call(m->jni_native, &f, result_type);
    vm_resume(t, depth);
    if (result_type == 'L' || result_type == '[') result = ref(t, (jobject)(uintptr_t)result);
    else if (result_type == 'Z') result = (uint8_t)result;
    else if (result_type == 'B') result = (uint32_t)(int8_t)result;
    else if (result_type == 'S') result = (uint32_t)(int16_t)result;
    else if (result_type == 'C') result = (uint16_t)result;
    else if (result_type == 'I' || result_type == 'F') result = (uint32_t)result;
    pop_frame(t, (Ref)result);
    return t->exception ? 0 : result;
}
int vm_jni_load(Thread *t, const char *name)
{
    char library[1024], err[512];
    if (!name || !*name) { vm_throw(t, "Ljava/lang/UnsatisfiedLinkError;", "empty library name"); return -1; }
    if (strchr(name, '/')) snprintf(library, sizeof library, "%s", name);
    else snprintf(library, sizeof library, "lib%s.so", name);
    unsigned depth = vm_suspend(t);
    henge_image *image = henge_load(library, err, sizeof err);
    vm_resume(t, depth);
    if (!image) { vm_throw(t, "Ljava/lang/UnsatisfiedLinkError;", err); return -1; }
    struct jni_state *s = state(t->vm);
    for (struct loaded_library *l = s->libraries; l; l = l->next) if (l->image == image) {
        if (l->status == 1 || (!l->status && l->owner == t)) return 0;
        vm_throw(t, "Ljava/lang/UnsatisfiedLinkError;", l->status < 0 ? "previous JNI_OnLoad failed" : "JNI_OnLoad is in progress on another thread");
        return -1;
    }
    struct loaded_library *l = calloc(1, sizeof *l);
    if (!l) { vm_throw(t, "Ljava/lang/OutOfMemoryError;", NULL); return -1; }
    l->image = image; l->next = s->libraries; l->owner = t; s->libraries = l;
    jint (*onload)(JavaVM *, void *) = (void *)henge_dlsym(image, "JNI_OnLoad");
    if (onload) {
        struct local_env *env = vm_jni_env(t);
        if (push_frame(t, 16) != JNI_OK) { l->status = -1; return -1; }
        struct loaded_library *previous = env->loading; env->loading = l;
        depth = vm_suspend(t);
        jint version = onload((JavaVM *)s, NULL);
        vm_resume(t, depth);
        env->loading = previous;
        pop_frame(t, 0);
        if (!t->exception && version != JNI_VERSION_1_2 && version != JNI_VERSION_1_4 && version != JNI_VERSION_1_6)
            vm_throw(t, "Ljava/lang/UnsatisfiedLinkError;", "JNI_OnLoad returned an unsupported version");
    }
    l->status = t->exception ? -1 : 1;
    while (l->registrations) {
        struct registration *r = l->registrations; l->registrations = r->next;
        if (l->status < 0) r->method->jni_native = r->previous;
        free(r);
    }
    return t->exception ? -1 : 0;
}

int vm_jni_has_libraries(VM *vm) { return state(vm) && state(vm)->libraries != NULL; }
