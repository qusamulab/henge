/* Native methods of the core classes (vm/core/src): Object, Class, String, Math, System, Double, Float. */
#include "vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NATIVE(name) static uint64_t name(Thread *t, uint32_t *a)
#define UNUSED (void)t; (void)a

static Object *obj(Thread *t, Ref r) { return vm_obj(t->vm, r); }
static float u2f(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
static uint32_t f2u(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static double u2d(const uint32_t *w) { uint64_t v = vm_wide(w); double d; memcpy(&d, &v, 8); return d; }
static uint64_t d2u(double d) { uint64_t v; memcpy(&v, &d, 8); return v; }

static int npe(Thread *t) { vm_throw(t, "Ljava/lang/NullPointerException;", NULL); return 0; }

/* ---- Object ---- */

NATIVE(object_get_class) { UNUSED; return vm_class_object(t, obj(t, a[0])->cls); }

NATIVE(object_hash_code)
{
    Object *o = obj(t, a[0]);
    if (o->kind == OBJ_STRING) {
        uint32_t h = 0;
        for (uint32_t i = 0; i < o->length; i++) h = 31 * h + o->u.chars[i];
        return h;
    }
    if (!o->hash) o->hash = (a[0] * 0x9E3779B1u >> 1) | 1;
    return o->hash;
}

NATIVE(object_clone)
{
    Object *o = obj(t, a[0]);
    if (o->kind == OBJ_ARRAY) {
        Ref r = vm_new_array(t, o->cls, o->length);
        if (t->exception) return 0;
        Object *n = obj(t, r);
        o = obj(t, a[0]); /* the table may have moved nothing, but be safe */
        char k = o->cls->array_kind;
        size_t es = (k == 'Z' || k == 'B') ? 1 : (k == 'S' || k == 'C') ? 2 : (k == 'J' || k == 'D') ? 8 : 4;
        memcpy(n->u.bytes, o->u.bytes, (size_t)o->length * es);
        return r;
    }
    Class *cl = vm_find_class(t->vm, "Ljava/lang/Cloneable;");
    if (!cl || !vm_instance_of(o->cls, cl)) {
        vm_throw(t, "Ljava/lang/CloneNotSupportedException;", o->cls->name);
        return 0;
    }
    Ref r = vm_new_object(t, o->cls);
    o = obj(t, a[0]);
    memcpy(obj(t, r)->u.words, o->u.words, (size_t)o->cls->inst_words * 4);
    return r;
}

NATIVE(object_notify) { UNUSED; return 0; }

NATIVE(object_wait)
{
    uint64_t ms = vm_wide(&a[1]);
    if (ms > 0) usleep((useconds_t)(ms > 1000000 ? 1000000000 : ms * 1000));
    (void)t;
    return 0;
}

/* ---- Class ---- */

static Class *mirror_class(Thread *t, Ref r)
{
    Object *o = obj(t, r);
    uint64_t p = (uint64_t)o->u.words[0] | (uint64_t)o->u.words[1] << 32;
    return (Class *)(uintptr_t)p;
}

static const char *primitive_name(char d)
{
    switch (d) {
    case 'I': return "int"; case 'J': return "long"; case 'S': return "short"; case 'B': return "byte";
    case 'C': return "char"; case 'Z': return "boolean"; case 'F': return "float"; case 'D': return "double";
    default: return "void";
    }
}

NATIVE(class_get_name)
{
    Class *c = mirror_class(t, a[0]);
    char buf[512];
    const char *n = c->name;
    if (n[1] == 0) {
        snprintf(buf, sizeof buf, "%s", primitive_name(n[0]));
    } else if (n[0] == 'L') {
        snprintf(buf, sizeof buf, "%s", n + 1);
        buf[strlen(buf) - 1] = 0;
    } else {
        snprintf(buf, sizeof buf, "%s", n);
    }
    for (char *p = buf; *p; p++) if (*p == '/') *p = '.';
    return vm_new_string_utf8(t, buf);
}

NATIVE(class_is_instance)
{
    if (!a[1]) return 0;
    return vm_instance_of(obj(t, a[1])->cls, mirror_class(t, a[0]));
}

NATIVE(class_is_array) { return mirror_class(t, a[0])->array_kind != 0; }
NATIVE(class_is_interface) { return (mirror_class(t, a[0])->access & ACC_INTERFACE) != 0; }

NATIVE(class_get_superclass)
{
    Class *s = mirror_class(t, a[0])->super;
    return s ? vm_class_object(t, s) : 0;
}

NATIVE(class_get_component_type)
{
    Class *c = mirror_class(t, a[0]);
    return c->component ? vm_class_object(t, c->component) : 0;
}

NATIVE(class_hash_code) { return a[0] * 0x9E3779B1u >> 1; (void)t; }

NATIVE(class_for_name)
{
    if (!a[0]) { npe(t); return 0; }
    char *n = vm_string_utf8(t->vm, a[0]);
    char desc[512];
    for (char *p = n; *p; p++) if (*p == '.') *p = '/';
    if (n[0] == '[') snprintf(desc, sizeof desc, "%s", n);
    else snprintf(desc, sizeof desc, "L%s;", n);
    Class *c = vm_find_class(t->vm, desc);
    if (!c) {
        vm_throw(t, "Ljava/lang/ClassNotFoundException;", n);
        free(n);
        return 0;
    }
    free(n);
    return vm_class_object(t, c);
}

NATIVE(class_get_primitive)
{
    char *n = vm_string_utf8(t->vm, a[0]);
    char d = !strcmp(n, "int") ? 'I' : !strcmp(n, "long") ? 'J' : !strcmp(n, "short") ? 'S' : !strcmp(n, "byte") ? 'B' :
             !strcmp(n, "char") ? 'C' : !strcmp(n, "boolean") ? 'Z' : !strcmp(n, "float") ? 'F' : !strcmp(n, "double") ? 'D' : 'V';
    free(n);
    return vm_class_object(t, vm_primitive_class(t->vm, d));
}

/* ---- java.lang.reflect.Array ---- */

static Ref make_multi(Thread *t, Class *array_cls, const uint32_t *dims, uint32_t ndims)
{
    if ((int32_t)dims[0] < 0) {
        vm_throw(t, "Ljava/lang/NegativeArraySizeException;", NULL);
        return 0;
    }
    Ref r = vm_new_array(t, array_cls, dims[0]);
    if (t->exception || ndims == 1)
        return r;
    for (uint32_t i = 0; i < dims[0]; i++) {
        Ref sub = make_multi(t, array_cls->component, dims + 1, ndims - 1);
        if (t->exception) return 0;
        ((uint32_t *)vm_obj(t->vm, r)->u.bytes)[i] = sub;
    }
    return r;
}

static uint64_t new_array_dims(Thread *t, Ref component, const uint32_t *dims, uint32_t ndims)
{
    Class *c = mirror_class(t, component);
    char desc[512];
    uint32_t k = 0;
    for (uint32_t i = 0; i < ndims; i++) desc[k++] = '[';
    snprintf(desc + k, sizeof desc - k, "%s", c->name);
    Class *ac = vm_find_class(t->vm, desc);
    if (!ac) { vm_throw(t, "Ljava/lang/NoClassDefFoundError;", desc); return 0; }
    return make_multi(t, ac, dims, ndims);
}

NATIVE(array_new_instance1) { uint32_t d[1] = {a[1]}; return new_array_dims(t, a[0], d, 1); }

NATIVE(array_new_instance_n)
{
    if (!a[1]) return npe(t);
    Object *dims = obj(t, a[1]);
    if (dims->length == 0) { vm_throw(t, "Ljava/lang/IllegalArgumentException;", "no dimensions"); return 0; }
    uint32_t copy[16];
    uint32_t n = dims->length > 16 ? 16 : dims->length;
    memcpy(copy, dims->u.bytes, (size_t)n * 4);
    return new_array_dims(t, a[0], copy, n);
}

NATIVE(array_get_length)
{
    if (!a[0]) return npe(t);
    return obj(t, a[0])->length;
}

/* ---- String ---- */

static Object *S(Thread *t, Ref r) { return obj(t, r); }

static uint64_t string_ref(Thread *t, const uint16_t *chars, uint32_t n) { return vm_new_string_utf16(t, chars, n); }

static int sioobe(Thread *t, int idx, uint32_t len)
{
    char msg[80];
    snprintf(msg, sizeof msg, "index=%d length=%u", idx, len);
    vm_throw(t, "Ljava/lang/StringIndexOutOfBoundsException;", msg);
    return 0;
}

NATIVE(string_length) { UNUSED; return S(t, a[0])->length; }
NATIVE(string_is_empty) { return S(t, a[0])->length == 0; (void)t; }

NATIVE(string_char_at)
{
    Object *s = S(t, a[0]);
    if (a[1] >= s->length) return sioobe(t, (int)a[1], s->length);
    return s->u.chars[a[1]];
}

NATIVE(string_substring1)
{
    Object *s = S(t, a[0]);
    if ((int32_t)a[1] < 0 || a[1] > s->length) return sioobe(t, (int)a[1], s->length);
    return string_ref(t, s->u.chars + a[1], s->length - a[1]);
}

NATIVE(string_substring2)
{
    Object *s = S(t, a[0]);
    int32_t b = (int32_t)a[1], e = (int32_t)a[2];
    if (b < 0 || e > (int32_t)s->length || b > e) return sioobe(t, b < 0 ? b : e, s->length);
    return string_ref(t, s->u.chars + b, (uint32_t)(e - b));
}

static int index_of_char(Object *s, uint32_t ch, int32_t from)
{
    if (from < 0) from = 0;
    for (uint32_t i = (uint32_t)from; i < s->length; i++)
        if (s->u.chars[i] == ch) return (int)i;
    return -1;
}

NATIVE(string_index_of_c) { return (uint32_t)index_of_char(S(t, a[0]), a[1], 0); }
NATIVE(string_index_of_c_from) { return (uint32_t)index_of_char(S(t, a[0]), a[1], (int32_t)a[2]); }

static int index_of_str(Object *s, Object *n, int32_t from)
{
    if (from < 0) from = 0;
    if (n->length == 0) return (uint32_t)from <= s->length ? from : (int)s->length;
    for (uint32_t i = (uint32_t)from; i + n->length <= s->length; i++)
        if (memcmp(s->u.chars + i, n->u.chars, (size_t)n->length * 2) == 0) return (int)i;
    return -1;
}

NATIVE(string_index_of_s) { if (!a[1]) return npe(t); return (uint32_t)index_of_str(S(t, a[0]), S(t, a[1]), 0); }
NATIVE(string_index_of_s_from) { if (!a[1]) return npe(t); return (uint32_t)index_of_str(S(t, a[0]), S(t, a[1]), (int32_t)a[2]); }

NATIVE(string_last_index_of_c)
{
    Object *s = S(t, a[0]);
    for (int i = (int)s->length - 1; i >= 0; i--) if (s->u.chars[i] == a[1]) return (uint32_t)i;
    return (uint32_t)-1;
}

NATIVE(string_last_index_of_c_from)
{
    Object *s = S(t, a[0]);
    int from = (int32_t)a[2];
    if (from >= (int)s->length) from = (int)s->length - 1;
    for (int i = from; i >= 0; i--) if (s->u.chars[i] == a[1]) return (uint32_t)i;
    return (uint32_t)-1;
}

NATIVE(string_last_index_of_s)
{
    if (!a[1]) return npe(t);
    Object *s = S(t, a[0]), *n = S(t, a[1]);
    for (int i = (int)s->length - (int)n->length; i >= 0; i--)
        if (memcmp(s->u.chars + i, n->u.chars, (size_t)n->length * 2) == 0) return (uint32_t)i;
    return (uint32_t)-1;
}

static int str_equal(Object *x, Object *y)
{
    return x->length == y->length && memcmp(x->u.chars, y->u.chars, (size_t)x->length * 2) == 0;
}

NATIVE(string_equals)
{
    if (!a[1]) return 0;
    Object *o = obj(t, a[1]);
    return o->kind == OBJ_STRING && str_equal(S(t, a[0]), o);
}

static uint16_t fold(uint16_t c) { return c >= 'A' && c <= 'Z' ? (uint16_t)(c + 32) : c; }

NATIVE(string_equals_ignore_case)
{
    if (!a[1]) return 0;
    Object *x = S(t, a[0]), *y = S(t, a[1]);
    if (x->length != y->length) return 0;
    for (uint32_t i = 0; i < x->length; i++) if (fold(x->u.chars[i]) != fold(y->u.chars[i])) return 0;
    return 1;
}

NATIVE(string_compare_to)
{
    if (!a[1]) return npe(t);
    Object *x = S(t, a[0]), *y = S(t, a[1]);
    uint32_t n = x->length < y->length ? x->length : y->length;
    for (uint32_t i = 0; i < n; i++)
        if (x->u.chars[i] != y->u.chars[i]) return (uint32_t)((int)x->u.chars[i] - (int)y->u.chars[i]);
    return (uint32_t)((int)x->length - (int)y->length);
}

NATIVE(string_compare_to_ic)
{
    if (!a[1]) return npe(t);
    Object *x = S(t, a[0]), *y = S(t, a[1]);
    uint32_t n = x->length < y->length ? x->length : y->length;
    for (uint32_t i = 0; i < n; i++)
        if (fold(x->u.chars[i]) != fold(y->u.chars[i])) return (uint32_t)((int)fold(x->u.chars[i]) - (int)fold(y->u.chars[i]));
    return (uint32_t)((int)x->length - (int)y->length);
}

static int starts_at(Object *s, Object *p, int32_t off)
{
    if (off < 0 || (uint32_t)off + p->length > s->length) return 0;
    return memcmp(s->u.chars + off, p->u.chars, (size_t)p->length * 2) == 0;
}

NATIVE(string_starts_with) { if (!a[1]) return npe(t); return starts_at(S(t, a[0]), S(t, a[1]), 0); }
NATIVE(string_starts_with_off) { if (!a[1]) return npe(t); return starts_at(S(t, a[0]), S(t, a[1]), (int32_t)a[2]); }

NATIVE(string_ends_with)
{
    if (!a[1]) return npe(t);
    Object *s = S(t, a[0]), *p = S(t, a[1]);
    return s->length >= p->length && starts_at(s, p, (int32_t)(s->length - p->length));
}

NATIVE(string_replace_c)
{
    Object *s = S(t, a[0]);
    uint16_t *buf = malloc((size_t)s->length * 2 + 2);
    for (uint32_t i = 0; i < s->length; i++) buf[i] = s->u.chars[i] == a[1] ? (uint16_t)a[2] : s->u.chars[i];
    Ref r = vm_new_string_utf16(t, buf, s->length);
    free(buf);
    return r;
}

NATIVE(string_replace_s)
{
    if (!a[1] || !a[2]) return npe(t);
    /* a[1], a[2] are CharSequence: only String arguments are supported natively */
    Object *s = S(t, a[0]), *f = S(t, a[1]), *r = S(t, a[2]);
    if (f->kind != OBJ_STRING || r->kind != OBJ_STRING) { vm_throw(t, "Ljava/lang/UnsupportedOperationException;", "replace(CharSequence)"); return 0; }
    if (f->length == 0) return a[0];
    uint16_t *buf = malloc(((size_t)s->length * (r->length > f->length ? r->length : 1) + 1) * 2 + 8);
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->length;) {
        if (starts_at(s, f, (int32_t)i)) {
            memcpy(buf + n, r->u.chars, (size_t)r->length * 2);
            n += r->length;
            i += f->length;
        } else {
            buf[n++] = s->u.chars[i++];
        }
    }
    Ref res = vm_new_string_utf16(t, buf, n);
    free(buf);
    return res;
}

NATIVE(string_trim)
{
    Object *s = S(t, a[0]);
    uint32_t b = 0, e = s->length;
    while (b < e && s->u.chars[b] <= ' ') b++;
    while (e > b && s->u.chars[e - 1] <= ' ') e--;
    if (b == 0 && e == s->length) return a[0];
    return string_ref(t, s->u.chars + b, e - b);
}

static uint64_t map_case(Thread *t, Ref r, int upper)
{
    Object *s = S(t, r);
    uint16_t *buf = malloc((size_t)s->length * 2 + 2);
    for (uint32_t i = 0; i < s->length; i++) {
        uint16_t c = s->u.chars[i];
        buf[i] = upper ? (c >= 'a' && c <= 'z' ? (uint16_t)(c - 32) : c) : fold(c);
    }
    Ref res = vm_new_string_utf16(t, buf, s->length);
    free(buf);
    return res;
}

NATIVE(string_to_lower) { return map_case(t, a[0], 0); }
NATIVE(string_to_upper) { return map_case(t, a[0], 1); }

NATIVE(string_concat)
{
    if (!a[1]) return npe(t);
    Object *x = S(t, a[0]), *y = S(t, a[1]);
    uint16_t *buf = malloc(((size_t)x->length + y->length) * 2 + 2);
    memcpy(buf, x->u.chars, (size_t)x->length * 2);
    memcpy(buf + x->length, y->u.chars, (size_t)y->length * 2);
    Ref r = vm_new_string_utf16(t, buf, x->length + y->length);
    free(buf);
    return r;
}

NATIVE(string_intern) { return vm_intern_string(t, a[0]); }
NATIVE(string_to_string) { UNUSED; return a[0]; }

NATIVE(string_to_char_array)
{
    Object *s = S(t, a[0]);
    Class *ac = vm_array_class(t->vm, "[C");
    Ref r = vm_new_array(t, ac, s->length);
    if (t->exception) return 0;
    memcpy(obj(t, r)->u.bytes, S(t, a[0])->u.chars, (size_t)s->length * 2);
    return r;
}

NATIVE(string_get_chars)
{
    Object *s = S(t, a[0]);
    int32_t sb = (int32_t)a[1], se = (int32_t)a[2], db = (int32_t)a[4];
    if (!a[3]) return npe(t);
    Object *d = obj(t, a[3]);
    if (sb < 0 || se > (int32_t)s->length || sb > se || db < 0 || (uint32_t)(db + (se - sb)) > d->length)
        return sioobe(t, sb, s->length);
    memcpy((uint16_t *)d->u.bytes + db, s->u.chars + sb, (size_t)(se - sb) * 2);
    return 0;
}

NATIVE(string_get_bytes)
{
    char *u = vm_string_utf8(t->vm, a[0]);
    size_t n = strlen(u);
    Ref r = vm_new_array(t, vm_array_class(t->vm, "[B"), (uint32_t)n);
    if (!t->exception) memcpy(obj(t, r)->u.bytes, u, n);
    free(u);
    return r;
}

NATIVE(string_value_of_obj)
{
    if (!a[0]) return vm_new_string_utf8(t, "null");
    Object *o = obj(t, a[0]);
    if (o->kind == OBJ_STRING) return a[0];
    Method *m = vm_lookup_virtual(o->cls, vm_intern(t->vm, "toString()Ljava/lang/String;"));
    if (!m) return vm_new_string_utf8(t, "?");
    return vm_invoke(t, m, (uint32_t[]){a[0]});
}

NATIVE(string_value_of_chars)
{
    if (!a[0]) return npe(t);
    Object *c = obj(t, a[0]);
    return string_ref(t, (const uint16_t *)c->u.bytes, c->length);
}

/* constructors replace the placeholder String the VM allocated for `new-instance` */
NATIVE(string_init_empty) { vm_string_set(t->vm, a[0], NULL, 0); return 0; }

NATIVE(string_init_chars)
{
    if (!a[1]) return npe(t);
    Object *c = obj(t, a[1]);
    vm_string_set(t->vm, a[0], (const uint16_t *)c->u.bytes, c->length);
    return 0;
}

NATIVE(string_init_chars_range)
{
    if (!a[1]) return npe(t);
    Object *c = obj(t, a[1]);
    int32_t off = (int32_t)a[2], n = (int32_t)a[3];
    if (off < 0 || n < 0 || (uint32_t)(off + n) > c->length) return sioobe(t, off, c->length);
    vm_string_set(t->vm, a[0], (const uint16_t *)c->u.bytes + off, (uint32_t)n);
    return 0;
}

NATIVE(string_init_string)
{
    if (!a[1]) return npe(t);
    Object *s = obj(t, a[1]);
    vm_string_set(t->vm, a[0], s->u.chars, s->length);
    return 0;
}

static uint64_t init_from_bytes(Thread *t, Ref self, Ref bytes, int32_t off, int32_t n)
{
    Object *b = obj(t, bytes);
    if (off < 0 || n < 0 || (uint32_t)(off + n) > b->length) return sioobe(t, off, b->length);
    char *tmp = malloc((size_t)n + 1);
    memcpy(tmp, b->u.bytes + off, (size_t)n);
    tmp[n] = 0;
    Ref s = vm_new_string_utf8(t, tmp);
    free(tmp);
    Object *so = obj(t, s);
    vm_string_set(t->vm, self, so->u.chars, so->length);
    return 0;
}

NATIVE(string_init_bytes) { if (!a[1]) return npe(t); return init_from_bytes(t, a[0], a[1], 0, (int32_t)obj(t, a[1])->length); }
NATIVE(string_init_bytes_range) { if (!a[1]) return npe(t); return init_from_bytes(t, a[0], a[1], (int32_t)a[2], (int32_t)a[3]); }

/* String.format: %d %i %s %f %x %X %c %b %n %% with flags, width and precision */
static double box_double(Thread *t, Ref r, int *ok)
{
    Object *o = obj(t, r);
    *ok = 1;
    if (!o) { *ok = 0; return 0; }
    const char *n = o->cls->name;
    if (!strcmp(n, "Ljava/lang/Double;")) return u2d(o->u.words);
    if (!strcmp(n, "Ljava/lang/Float;")) return (double)u2f(o->u.words[0]);
    if (!strcmp(n, "Ljava/lang/Integer;") || !strcmp(n, "Ljava/lang/Short;") || !strcmp(n, "Ljava/lang/Byte;")) return (double)(int32_t)o->u.words[0];
    if (!strcmp(n, "Ljava/lang/Long;")) return (double)(int64_t)vm_wide(o->u.words);
    *ok = 0;
    return 0;
}

static int64_t box_long(Thread *t, Ref r, int *ok)
{
    Object *o = obj(t, r);
    *ok = 1;
    if (!o) { *ok = 0; return 0; }
    const char *n = o->cls->name;
    if (!strcmp(n, "Ljava/lang/Long;")) return (int64_t)vm_wide(o->u.words);
    if (!strcmp(n, "Ljava/lang/Integer;") || !strcmp(n, "Ljava/lang/Short;") || !strcmp(n, "Ljava/lang/Byte;")) return (int32_t)o->u.words[0];
    if (!strcmp(n, "Ljava/lang/Character;")) return (int64_t)o->u.words[0];
    *ok = 0;
    return 0;
}

NATIVE(string_format)
{
    if (!a[0]) return npe(t);
    char *fmt = vm_string_utf8(t->vm, a[0]);
    Object *args = a[1] ? obj(t, a[1]) : NULL;
    uint32_t nargs = args ? args->length : 0, ai = 0;
    char out[4096];
    size_t k = 0;
    for (const char *p = fmt; *p && k < sizeof out - 64; p++) {
        if (*p != '%') { out[k++] = *p; continue; }
        char spec[32];
        size_t sl = 0;
        spec[sl++] = *p++;
        while (*p && strchr("-+ #0123456789.,", *p) && sl < 20) { if (*p != ',') spec[sl++] = *p; p++; }
        char conv = *p;
        if (!conv) break;
        if (conv == '%') { out[k++] = '%'; continue; }
        if (conv == 'n') { out[k++] = '\n'; continue; }
        Ref arg = ai < nargs ? ((uint32_t *)args->u.bytes)[ai++] : 0;
        char piece[512];
        int ok;
        if (conv == 'd' || conv == 'x' || conv == 'X' || conv == 'o') {
            int64_t v = box_long(t, arg, &ok);
            spec[sl++] = 'l'; spec[sl++] = 'l'; spec[sl++] = conv; spec[sl] = 0;
            snprintf(piece, sizeof piece, spec, (long long)v);
        } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E') {
            double v = box_double(t, arg, &ok);
            spec[sl++] = conv; spec[sl] = 0;
            snprintf(piece, sizeof piece, spec, v);
        } else if (conv == 'c') {
            int64_t v = box_long(t, arg, &ok);
            snprintf(piece, sizeof piece, "%c", (int)v);
        } else if (conv == 'b') {
            Object *bo = obj(t, arg);
            snprintf(piece, sizeof piece, "%s", !bo ? "false" : (bo->cls->name[1] == 'j' && !strcmp(bo->cls->name, "Ljava/lang/Boolean;") ? (bo->u.words[0] ? "true" : "false") : "true"));
        } else { /* %s and anything else: String.valueOf(arg) */
            Ref sr = (Ref)string_value_of_obj(t, (uint32_t[]){arg});
            char *u = vm_string_utf8(t->vm, sr);
            spec[sl++] = 's'; spec[sl] = 0;
            snprintf(piece, sizeof piece, spec, u);
            free(u);
        }
        size_t n = strlen(piece);
        memcpy(out + k, piece, n);
        k += n;
    }
    out[k] = 0;
    free(fmt);
    return vm_new_string_utf8(t, out);
}

/* ---- Float / Double ---- */

static size_t shortest(double d, int is_float, char *digits, int *exp10)
{
    char buf[48];
    for (int p = 0; p < 17; p++) {
        snprintf(buf, sizeof buf, "%.*e", p, d);
        if (is_float ? (strtof(buf, NULL) == (float)d) : (strtod(buf, NULL) == d)) break;
    }
    /* buf = d.ddde[+-]xx */
    char *e = strchr(buf, 'e');
    *exp10 = atoi(e + 1);
    size_t n = 0;
    for (char *c = buf; c < e; c++) if (*c >= '0' && *c <= '9') digits[n++] = *c;
    while (n > 1 && digits[n - 1] == '0') n--;
    digits[n] = 0;
    return n;
}

static void format_floating(double d, int is_float, char *out, size_t cap)
{
    if (d != d) { snprintf(out, cap, "NaN"); return; }
    if (isinf(d)) { snprintf(out, cap, d > 0 ? "Infinity" : "-Infinity"); return; }
    if (d == 0) { snprintf(out, cap, signbit(d) ? "-0.0" : "0.0"); return; }
    char digits[32];
    int e;
    size_t n = shortest(fabs(d), is_float, digits, &e);
    char *o = out;
    if (d < 0) *o++ = '-';
    if (e >= -3 && e < 7) {
        if (e < 0) {
            *o++ = '0'; *o++ = '.';
            for (int i = 0; i < -e - 1; i++) *o++ = '0';
            for (size_t i = 0; i < n; i++) *o++ = digits[i];
        } else {
            for (int i = 0; i <= e; i++) *o++ = (size_t)i < n ? digits[i] : '0';
            *o++ = '.';
            if ((size_t)e + 1 < n) for (size_t i = (size_t)e + 1; i < n; i++) *o++ = digits[i];
            else *o++ = '0';
        }
        *o = 0;
    } else {
        *o++ = digits[0];
        *o++ = '.';
        if (n > 1) for (size_t i = 1; i < n; i++) *o++ = digits[i];
        else *o++ = '0';
        snprintf(o, cap - (size_t)(o - out), "E%d", e);
    }
}

NATIVE(double_to_string) { char b[64]; format_floating(u2d(a), 0, b, sizeof b); return vm_new_string_utf8(t, b); }
NATIVE(float_to_string) { char b[64]; format_floating((double)u2f(a[0]), 1, b, sizeof b); return vm_new_string_utf8(t, b); }
NATIVE(double_to_raw_bits) { UNUSED; return vm_wide(a); }
NATIVE(double_from_bits) { UNUSED; return vm_wide(a); }
NATIVE(float_to_raw_bits) { UNUSED; return a[0]; }
NATIVE(float_from_bits) { UNUSED; return a[0]; }

NATIVE(double_parse)
{
    if (!a[0]) return npe(t);
    char *s = vm_string_utf8(t->vm, a[0]);
    char *end;
    double d = strtod(s, &end);
    int bad = end == s || *end;
    if (bad) vm_throw(t, "Ljava/lang/NumberFormatException;", s);
    free(s);
    return bad ? 0 : d2u(d);
}

NATIVE(float_parse)
{
    uint64_t r = double_parse(t, a);
    if (t->exception) return 0;
    double d; memcpy(&d, &r, 8);
    return f2u((float)d);
}

/* ---- Math ---- */

#define MATH1(fn, cfn) NATIVE(fn) { UNUSED; return d2u(cfn(u2d(a))); }
#define MATH2(fn, cfn) NATIVE(fn) { UNUSED; return d2u(cfn(u2d(a), u2d(a + 2))); }
MATH1(math_sin, sin) MATH1(math_cos, cos) MATH1(math_tan, tan) MATH1(math_asin, asin) MATH1(math_acos, acos)
MATH1(math_atan, atan) MATH1(math_sqrt, sqrt) MATH1(math_exp, exp) MATH1(math_log, log) MATH1(math_log10, log10)
MATH1(math_floor, floor) MATH1(math_ceil, ceil) MATH1(math_rint, rint) MATH1(math_cbrt, cbrt) MATH1(math_sinh, sinh)
MATH1(math_cosh, cosh) MATH1(math_tanh, tanh)
MATH2(math_atan2, atan2) MATH2(math_pow, pow) MATH2(math_hypot, hypot)

/* ---- System ---- */

NATIVE(system_arraycopy)
{
    if (!a[0] || !a[2]) return npe(t);
    Object *s = obj(t, a[0]), *d = obj(t, a[2]);
    int32_t sp = (int32_t)a[1], dp = (int32_t)a[3], n = (int32_t)a[4];
    if (s->kind != OBJ_ARRAY || d->kind != OBJ_ARRAY) { vm_throw(t, "Ljava/lang/ArrayStoreException;", "not an array"); return 0; }
    if (s->cls->array_kind != d->cls->array_kind) { vm_throw(t, "Ljava/lang/ArrayStoreException;", "type mismatch"); return 0; }
    if (sp < 0 || dp < 0 || n < 0 || (uint32_t)(sp + n) > s->length || (uint32_t)(dp + n) > d->length) {
        vm_throw(t, "Ljava/lang/ArrayIndexOutOfBoundsException;", "arraycopy");
        return 0;
    }
    char k = s->cls->array_kind;
    size_t es = (k == 'Z' || k == 'B') ? 1 : (k == 'S' || k == 'C') ? 2 : (k == 'J' || k == 'D') ? 8 : 4;
    memmove(d->u.bytes + (size_t)dp * es, s->u.bytes + (size_t)sp * es, (size_t)n * es);
    return 0;
}

NATIVE(system_millis) { UNUSED; struct timeval tv; gettimeofday(&tv, NULL); return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000; }
NATIVE(system_nanos) { UNUSED; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
NATIVE(system_identity_hash) { if (!a[0]) return 0; return object_hash_code(t, a); }
NATIVE(system_exit) { UNUSED; exit((int)a[0]); }

/* ---- PrintStream ---- */

void (*vm_log_sink)(int fd, const char *text);

NATIVE(print_write)
{
    if (!a[1]) return npe(t);
    char *u = vm_string_utf8(t->vm, a[1]);
    if (vm_log_sink) {
        vm_log_sink((int)a[0], u);
    } else {
        FILE *f = a[0] == 2 ? stderr : stdout;
        fputs(u, f);
        fflush(f);
    }
    free(u);
    return 0;
}

void vm_register_core_natives(VM *vm)
{
#define R(cls, name, proto, fn) vm_register_native(vm, cls, name, proto, fn)
    R("Ljava/lang/Object;", "<init>", "()V", object_notify);
    R("Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;", object_get_class);
    R("Ljava/lang/Object;", "hashCode", "()I", object_hash_code);
    R("Ljava/lang/Object;", "clone", "()Ljava/lang/Object;", object_clone);
    R("Ljava/lang/Object;", "notify", "()V", object_notify);
    R("Ljava/lang/Object;", "notifyAll", "()V", object_notify);
    R("Ljava/lang/Object;", "wait", "(J)V", object_wait);
    R("Ljava/lang/Class;", "getName", "()Ljava/lang/String;", class_get_name);
    R("Ljava/lang/Class;", "isInstance", "(Ljava/lang/Object;)Z", class_is_instance);
    R("Ljava/lang/Class;", "isArray", "()Z", class_is_array);
    R("Ljava/lang/Class;", "isInterface", "()Z", class_is_interface);
    R("Ljava/lang/Class;", "getSuperclass", "()Ljava/lang/Class;", class_get_superclass);
    R("Ljava/lang/Class;", "getComponentType", "()Ljava/lang/Class;", class_get_component_type);
    R("Ljava/lang/Class;", "hashCode", "()I", class_hash_code);
    R("Ljava/lang/Class;", "getPrimitiveClass", "(Ljava/lang/String;)Ljava/lang/Class;", class_get_primitive);
    R("Ljava/lang/reflect/Array;", "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;", array_new_instance1);
    R("Ljava/lang/reflect/Array;", "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;", array_new_instance_n);
    R("Ljava/lang/reflect/Array;", "getLength", "(Ljava/lang/Object;)I", array_get_length);
    R("Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", class_for_name);
    {
        const char *S = "Ljava/lang/String;";
        R(S, "<init>", "()V", string_init_empty);
        R(S, "<init>", "([C)V", string_init_chars);
        R(S, "<init>", "([CII)V", string_init_chars_range);
        R(S, "<init>", "(Ljava/lang/String;)V", string_init_string);
        R(S, "<init>", "([B)V", string_init_bytes);
        R(S, "<init>", "([BII)V", string_init_bytes_range);
        R(S, "length", "()I", string_length);
        R(S, "charAt", "(I)C", string_char_at);
        R(S, "isEmpty", "()Z", string_is_empty);
        R(S, "substring", "(I)Ljava/lang/String;", string_substring1);
        R(S, "substring", "(II)Ljava/lang/String;", string_substring2);
        R(S, "indexOf", "(I)I", string_index_of_c);
        R(S, "indexOf", "(II)I", string_index_of_c_from);
        R(S, "indexOf", "(Ljava/lang/String;)I", string_index_of_s);
        R(S, "indexOf", "(Ljava/lang/String;I)I", string_index_of_s_from);
        R(S, "lastIndexOf", "(I)I", string_last_index_of_c);
        R(S, "lastIndexOf", "(II)I", string_last_index_of_c_from);
        R(S, "lastIndexOf", "(Ljava/lang/String;)I", string_last_index_of_s);
        R(S, "equals", "(Ljava/lang/Object;)Z", string_equals);
        R(S, "equalsIgnoreCase", "(Ljava/lang/String;)Z", string_equals_ignore_case);
        R(S, "hashCode", "()I", object_hash_code);
        R(S, "compareTo", "(Ljava/lang/String;)I", string_compare_to);
        R(S, "compareTo", "(Ljava/lang/Object;)I", string_compare_to);
        R(S, "compareToIgnoreCase", "(Ljava/lang/String;)I", string_compare_to_ic);
        R(S, "startsWith", "(Ljava/lang/String;)Z", string_starts_with);
        R(S, "startsWith", "(Ljava/lang/String;I)Z", string_starts_with_off);
        R(S, "endsWith", "(Ljava/lang/String;)Z", string_ends_with);
        R(S, "replace", "(CC)Ljava/lang/String;", string_replace_c);
        R(S, "replace", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;", string_replace_s);
        R(S, "trim", "()Ljava/lang/String;", string_trim);
        R(S, "toLowerCase", "()Ljava/lang/String;", string_to_lower);
        R(S, "toUpperCase", "()Ljava/lang/String;", string_to_upper);
        R(S, "concat", "(Ljava/lang/String;)Ljava/lang/String;", string_concat);
        R(S, "intern", "()Ljava/lang/String;", string_intern);
        R(S, "toString", "()Ljava/lang/String;", string_to_string);
        R(S, "toCharArray", "()[C", string_to_char_array);
        R(S, "getChars", "(II[CI)V", string_get_chars);
        R(S, "getBytes", "()[B", string_get_bytes);
        R(S, "valueOf", "(Ljava/lang/Object;)Ljava/lang/String;", string_value_of_obj);
        R(S, "valueOf", "([C)Ljava/lang/String;", string_value_of_chars);
        R(S, "format", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;", string_format);
    }
    R("Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;", double_to_string);
    R("Ljava/lang/Double;", "doubleToRawLongBits", "(D)J", double_to_raw_bits);
    R("Ljava/lang/Double;", "longBitsToDouble", "(J)D", double_from_bits);
    R("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", double_parse);
    R("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", float_to_string);
    R("Ljava/lang/Float;", "floatToRawIntBits", "(F)I", float_to_raw_bits);
    R("Ljava/lang/Float;", "intBitsToFloat", "(I)F", float_from_bits);
    R("Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F", float_parse);
#define M(name, fn) R("Ljava/lang/Math;", name, "(D)D", fn)
    M("sin", math_sin); M("cos", math_cos); M("tan", math_tan); M("asin", math_asin); M("acos", math_acos);
    M("atan", math_atan); M("sqrt", math_sqrt); M("exp", math_exp); M("log", math_log); M("log10", math_log10);
    M("floor", math_floor); M("ceil", math_ceil); M("rint", math_rint); M("cbrt", math_cbrt); M("sinh", math_sinh);
    M("cosh", math_cosh); M("tanh", math_tanh);
    R("Ljava/lang/Math;", "atan2", "(DD)D", math_atan2);
    R("Ljava/lang/Math;", "pow", "(DD)D", math_pow);
    R("Ljava/lang/Math;", "hypot", "(DD)D", math_hypot);
    R("Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", system_arraycopy);
    R("Ljava/lang/System;", "currentTimeMillis", "()J", system_millis);
    R("Ljava/lang/System;", "nanoTime", "()J", system_nanos);
    R("Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I", system_identity_hash);
    R("Ljava/lang/System;", "exit", "(I)V", system_exit);
    R("Ljava/io/PrintStream;", "write", "(ILjava/lang/String;)V", print_write);
}
