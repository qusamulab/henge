/* String encoding, hashing and interning. Object allocation stays in heap.c. */
#include "vm.h"

#include <stdlib.h>
#include <string.h>

/* MUTF-8 / UTF-8 to UTF-16. */
Ref vm_new_string_utf8(Thread *t, const char *s)
{
    size_t len = strlen(s);
    uint16_t *buf = malloc((len + 1) * 2);
    uint32_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = *p;
        if (c < 0x80) { p += 1; }
        else if ((c >> 5) == 6 && p[1]) { c = (c & 0x1f) << 6 | (p[1] & 0x3f); p += 2; }
        else if ((c >> 4) == 14 && p[1] && p[2]) { c = (c & 0x0f) << 12 | (p[1] & 0x3f) << 6 | (p[2] & 0x3f); p += 3; }
        else if ((c >> 3) == 30 && p[1] && p[2] && p[3]) {
            c = (c & 7) << 18 | (p[1] & 0x3f) << 12 | (p[2] & 0x3f) << 6 | (p[3] & 0x3f);
            p += 4;
            c -= 0x10000;
            buf[n++] = (uint16_t)(0xd800 + (c >> 10));
            c = 0xdc00 + (c & 0x3ff);
        } else { p += 1; c = '?'; }
        buf[n++] = (uint16_t)c;
    }
    Ref r = vm_new_string_utf16(t, buf, n);
    free(buf);
    return r;
}

char *vm_string_utf8(VM *vm, Ref r)
{
    Object *o = vm_obj(vm, r);
    if (!o || o->kind != OBJ_STRING)
        return strdup("null");
    char *out = malloc((size_t)o->length * 3 + 1);
    size_t k = 0;
    for (uint32_t i = 0; i < o->length; i++) {
        uint32_t c = o->u.chars[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < o->length && o->u.chars[i + 1] >= 0xdc00 && o->u.chars[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (o->u.chars[++i] - 0xdc00);
            out[k++] = (char)(0xf0 | c >> 18); out[k++] = (char)(0x80 | (c >> 12 & 0x3f));
            out[k++] = (char)(0x80 | (c >> 6 & 0x3f)); out[k++] = (char)(0x80 | (c & 0x3f));
        } else if (c < 0x80) out[k++] = (char)c;
        else if (c < 0x800) { out[k++] = (char)(0xc0 | c >> 6); out[k++] = (char)(0x80 | (c & 0x3f)); }
        else { out[k++] = (char)(0xe0 | c >> 12); out[k++] = (char)(0x80 | (c >> 6 & 0x3f)); out[k++] = (char)(0x80 | (c & 0x3f)); }
    }
    out[k] = 0;
    return out;
}

/* Like Java, 0 means "not computed yet", so a string whose hash is 0 is rehashed each time. */
uint32_t vm_string_hash(Object *s)
{
    uint32_t h = s->hash;
    if (!h) {
        for (uint32_t i = 0; i < s->length; i++) h = 31 * h + s->u.chars[i];
        s->hash = h;
    }
    return h;
}

/* Slot in string_index for contents equal to o: the matching Ref, or an empty (0) slot. */
static Ref *string_slot(VM *vm, Object *o, uint32_t h)
{
    size_t mask = vm->cap_string_index - 1;
    for (size_t k = h & mask;; k = (k + 1) & mask) {
        Ref *slot = &vm->string_index[k];
        Object *p = vm_obj(vm, *slot);
        if (!p || (p->length == o->length && vm_string_hash(p) == h && memcmp(p->u.chars, o->u.chars, (size_t)o->length * 2) == 0))
            return slot;
    }
}

/* Interns a String: equal contents give the same Ref. Interned strings stay rooted in string_pool,
 * so string_index never holds a dead handle. */
Ref vm_intern_string(Thread *t, Ref s)
{
    VM *vm = t->vm;
    if ((vm->nstring_pool + 1) * 2 > vm->cap_string_index) {
        size_t cap = vm->cap_string_index ? vm->cap_string_index * 2 : 1024;
        free(vm->string_index);
        vm->string_index = calloc(cap, sizeof *vm->string_index);
        if (!vm->string_index)
            abort();
        vm->cap_string_index = cap;
        for (size_t i = 0; i < vm->nstring_pool; i++) {
            Object *p = vm_obj(vm, vm->string_pool[i]);
            *string_slot(vm, p, vm_string_hash(p)) = vm->string_pool[i];
        }
    }
    Object *o = vm_obj(vm, s);
    Ref *slot = string_slot(vm, o, vm_string_hash(o));
    if (*slot)
        return *slot;
    *slot = s;
    if (vm->nstring_pool == vm->cap_string_pool) {
        vm->cap_string_pool = vm->cap_string_pool ? vm->cap_string_pool * 2 : 256;
        vm->string_pool = realloc(vm->string_pool, vm->cap_string_pool * sizeof *vm->string_pool);
    }
    vm->string_pool[vm->nstring_pool++] = s;
    return s;
}
