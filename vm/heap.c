/* Object heap: a handle table plus mark-and-sweep. Registers are scanned conservatively (any word
 * that is a live handle keeps its object); everything else is traced exactly. Collections only
 * happen at interpreter safepoints, so C code never holds an unrooted Ref across one. */
#include "vm.h"

#include <stdlib.h>
#include <string.h>

#define GC_MIN_THRESHOLD (16u << 20)

static int is_free_slot(Object *p) { return ((uintptr_t)p & 1) != 0; }

static Ref table_add(VM *vm, Object *o)
{
    if (vm->free_head) {
        Ref r = vm->free_head;
        vm->free_head = (uint32_t)((uintptr_t)vm->table[r] >> 1);
        vm->table[r] = o;
        return r;
    }
    if (vm->table_size == vm->table_cap) {
        uint32_t cap = vm->table_cap ? vm->table_cap * 2 : 4096;
        Object **t = realloc(vm->table, cap * sizeof *t);
        if (!t)
            abort();
        vm->table = t;
        vm->table_cap = cap;
        if (vm->table_size == 0) {
            vm->table[0] = (Object *)1; /* handle 0 is null */
            vm->table_size = 1;
        }
    }
    vm->table[vm->table_size] = o;
    return vm->table_size++;
}

static Object *alloc(VM *vm, size_t bytes)
{
    Object *o = calloc(1, bytes);
    if (!o)
        abort();
    vm->live_bytes += (uint32_t)bytes;
    if (vm->live_bytes > vm->gc_threshold)
        vm->gc_wanted = 1;
    return o;
}

Ref vm_new_object(Thread *t, Class *cls)
{
    VM *vm = t->vm;
    Object *o = alloc(vm, offsetof(Object, u) + (size_t)cls->inst_words * 4 + 4);
    o->cls = cls;
    o->kind = OBJ_INSTANCE;
    return table_add(vm, o);
}

Ref vm_new_array(Thread *t, Class *array_cls, uint32_t length)
{
    VM *vm = t->vm;
    uint64_t bytes = (uint64_t)length * vm_elem_size(array_cls->array_kind);
    if (bytes > (1u << 30)) {
        vm_throw(t, "Ljava/lang/OutOfMemoryError;", "array too large");
        return 0;
    }
    Object *o = alloc(vm, offsetof(Object, u) + (size_t)bytes + 8);
    o->cls = array_cls;
    o->kind = OBJ_ARRAY;
    o->length = length;
    return table_add(vm, o);
}

Ref vm_new_string_utf16(Thread *t, const uint16_t *chars, uint32_t n)
{
    VM *vm = t->vm;
    Object *o = alloc(vm, offsetof(Object, u) + (size_t)n * 2 + 4);
    o->cls = vm->cls_string;
    o->kind = OBJ_STRING;
    o->length = n;
    if (n) memcpy(o->u.chars, chars, (size_t)n * 2);
    return table_add(vm, o);
}

void vm_string_set(VM *vm, Ref r, const uint16_t *chars, uint32_t n)
{
    Object *old = vm_obj(vm, r);
    Object *o = alloc(vm, offsetof(Object, u) + (size_t)n * 2 + 4);
    o->cls = vm->cls_string;
    o->kind = OBJ_STRING;
    o->length = n;
    if (n) memcpy(o->u.chars, chars, (size_t)n * 2);
    vm->table[r] = o;
    free(old);
}

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

void vm_pin(VM *vm, Ref r)
{
    if (vm->npins == vm->cap_pins) {
        vm->cap_pins = vm->cap_pins ? vm->cap_pins * 2 : 64;
        vm->pins = realloc(vm->pins, vm->cap_pins * sizeof *vm->pins);
    }
    vm->pins[vm->npins++] = r;
}

/* ---- collection ---- */

struct gray {
    Ref *items;
    size_t n, cap;
};

static void mark_ref(VM *vm, struct gray *g, Ref r)
{
    Object *o = vm_obj(vm, r);
    if (!o || o->mark)
        return;
    o->mark = 1;
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 1024;
        g->items = realloc(g->items, g->cap * sizeof *g->items);
    }
    g->items[g->n++] = r;
}

static void trace(VM *vm, struct gray *g, Object *o)
{
    if (o->kind == OBJ_ARRAY) {
        if (o->cls->array_kind == 'L' || o->cls->array_kind == '[') {
            const uint32_t *e = (const uint32_t *)o->u.bytes;
            for (uint32_t i = 0; i < o->length; i++)
                if (e[i]) mark_ref(vm, g, e[i]);
        }
    } else if (o->kind == OBJ_INSTANCE) {
        for (uint16_t i = 0; i < o->cls->nref; i++)
            mark_ref(vm, g, o->u.words[o->cls->ref_offsets[i]]);
    }
}

void vm_gc(Thread *self)
{
    VM *vm = self->vm;
    struct gray g = {0};
    vm->gc_wanted = 0;
    for (Thread *t = vm->threads; t; t = t->next) {
        for (uint32_t *p = t->stack; p < t->sp; p++)
            if (*p && *p < vm->table_size) mark_ref(vm, &g, *p); /* conservative */
        mark_ref(vm, &g, t->exception);
        mark_ref(vm, &g, t->thread_obj);
    }
    for (size_t c = 0; c < vm->nclasses; c++) {
        Class *k = vm->classes[c];
        mark_ref(vm, &g, k->class_obj);
        for (uint16_t i = 0; i < k->nfields; i++) {
            Field *f = &k->fields[i];
            if ((f->access & ACC_STATIC) && (f->type == 'L' || f->type == '['))
                mark_ref(vm, &g, k->statics[f->offset]);
        }
    }
    for (size_t i = 0; i < vm->npins; i++) mark_ref(vm, &g, vm->pins[i]);
    /* also roots every dex res_strings entry: those all come from vm_intern_string */
    for (size_t i = 0; i < vm->nstring_pool; i++) mark_ref(vm, &g, vm->string_pool[i]);
    while (g.n) {
        Object *o = vm_obj(vm, g.items[--g.n]);
        if (o) trace(vm, &g, o);
    }
    free(g.items);

    uint64_t live = 0;
    for (uint32_t r = 1; r < vm->table_size; r++) {
        Object *o = vm->table[r];
        if (is_free_slot(o))
            continue;
        if (o->mark) {
            o->mark = 0;
            live += offsetof(Object, u) + (o->kind == OBJ_ARRAY ? (uint64_t)o->length * vm_elem_size(o->cls->array_kind)
                                           : o->kind == OBJ_STRING ? (uint64_t)o->length * 2 : (uint64_t)o->cls->inst_words * 4);
        } else {
            free(o);
            vm->table[r] = (Object *)(uintptr_t)((uintptr_t)vm->free_head << 1 | 1);
            vm->free_head = r;
        }
    }
    vm->live_bytes = (uint32_t)(live > UINT32_MAX ? UINT32_MAX : live);
    vm->gc_threshold = vm->live_bytes * 2 > GC_MIN_THRESHOLD ? vm->live_bytes * 2 : GC_MIN_THRESHOLD;
}
