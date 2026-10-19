/* Object heap: a handle table plus mark-and-sweep. Registers are scanned conservatively (any word
 * that is a live handle keeps its object); everything else is traced exactly. Collections only
 * happen at interpreter safepoints, so C code never holds an unrooted Ref across one. */
#include "vm.h"

#include <stdlib.h>
#include <string.h>

#define GC_MIN_THRESHOLD (16u << 20)

static int is_free_slot(Object *p) { return ((uintptr_t)p & 1) != 0; }

static size_t object_size(const Object *o)
{
    size_t payload = o->kind == OBJ_ARRAY ? (size_t)o->length * vm_elem_size(o->cls->array_kind) + 8
                   : o->kind == OBJ_STRING ? (size_t)o->length * 2 + 4
                   : (size_t)o->cls->inst_words * 4 + 4;
    return offsetof(Object, u) + payload;
}

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
    vm->live_bytes += bytes;
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
    vm->live_bytes -= object_size(old);
    free(old);
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

    size_t live = 0;
    for (uint32_t r = 1; r < vm->table_size; r++) {
        Object *o = vm->table[r];
        if (is_free_slot(o))
            continue;
        if (o->mark) {
            o->mark = 0;
            live += object_size(o);
        } else {
            free(o);
            vm->table[r] = (Object *)(uintptr_t)((uintptr_t)vm->free_head << 1 | 1);
            vm->free_head = r;
        }
    }
    vm->live_bytes = live;
    vm->gc_threshold = live > SIZE_MAX / 2 ? SIZE_MAX
                     : live * 2 > GC_MIN_THRESHOLD ? live * 2 : GC_MIN_THRESHOLD;
}
