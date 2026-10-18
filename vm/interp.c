/* The bytecode interpreter: every Dalvik opcode a normal app compiler emits, except
 * invoke-polymorphic / invoke-custom / method handles. */
#include "vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 1000 /* the VM runs on a thread with a large C stack */

static inline uint64_t rw(const uint32_t *r) { uint64_t v; memcpy(&v, r, 8); return v; }
static inline void ww(uint32_t *r, uint64_t v) { memcpy(r, &v, 8); }
static inline double u2d(uint64_t v) { double d; memcpy(&d, &v, 8); return d; }

static int32_t f2i(float f) { return f != f ? 0 : f >= 2147483648.0f ? INT32_MAX : f <= -2147483648.0f ? INT32_MIN : (int32_t)f; }
static int32_t d2i(double d) { return d != d ? 0 : d >= 2147483648.0 ? INT32_MAX : d <= -2147483649.0 ? INT32_MIN : (int32_t)d; }
static int64_t f2l(float f) { return f != f ? 0 : f >= 9223372036854775808.0f ? INT64_MAX : f <= -9223372036854775808.0f ? INT64_MIN : (int64_t)f; }
static int64_t d2l(double d) { return d != d ? 0 : d >= 9223372036854775808.0 ? INT64_MAX : d <= -9223372036854775808.0 ? INT64_MIN : (int64_t)d; }

/* Finds the handler for an exception thrown at dex_pc; returns the handler address or -1. */
static int32_t find_handler(Thread *t, Method *m, uint32_t dex_pc, Object *exc)
{
    if (!m->tries)
        return -1;
    const uint8_t *tries = m->tries_data;
    const uint8_t *handlers = tries + (size_t)m->tries * 8;
    for (uint16_t i = 0; i < m->tries; i++) {
        const uint8_t *ti = tries + (size_t)i * 8;
        uint32_t start = (uint32_t)ti[0] | (uint32_t)ti[1] << 8 | (uint32_t)ti[2] << 16 | (uint32_t)ti[3] << 24;
        uint32_t count = (uint32_t)(ti[4] | ti[5] << 8);
        uint32_t off = (uint32_t)(ti[6] | ti[7] << 8);
        if (dex_pc < start || dex_pc >= start + count)
            continue;
        const uint8_t *h = handlers + off;
        int32_t size = dex_sleb(&h);
        int32_t n = size < 0 ? -size : size;
        for (int32_t k = 0; k < n; k++) {
            uint32_t type_idx = dex_uleb(&h);
            uint32_t addr = dex_uleb(&h);
            Class *c = m->dex->classes_by_type[type_idx];
            if (!c) {
                c = vm_find_class(t->vm, dex_type(m->dex, type_idx));
                m->dex->classes_by_type[type_idx] = c;
            }
            if (c && vm_instance_of(exc->cls, c))
                return (int32_t)addr;
        }
        if (size <= 0)
            return (int32_t)dex_uleb(&h); /* catch-all */
    }
    return -1;
}

uint64_t vm_invoke(Thread *t, Method *m, uint32_t *args)
{
    VM *vm = t->vm;
    if (m->native)
        return m->native(t, args);
    if (!m->insns) {
        char msg[512];
        snprintf(msg, sizeof msg, "%s.%s", m->cls->name, m->sig);
        vm_throw(t, (m->access & ACC_ABSTRACT) ? "Ljava/lang/AbstractMethodError;" : "Ljava/lang/UnsatisfiedLinkError;", msg);
        return 0;
    }
    if (t->depth >= MAX_DEPTH - 3 && getenv("HENGE_VM_TRACE")) fprintf(stderr, "deep call: %s.%s\n", m->cls->name, m->sig);
    if (t->depth >= MAX_DEPTH || t->sp + m->registers + 8 > t->stack_end) {
        vm_throw(t, "Ljava/lang/StackOverflowError;", NULL);
        return 0;
    }
    t->depth++;
    uint32_t *regs = t->sp;
    uint32_t *frame_base = regs;
    t->sp += m->registers;
    memset(regs, 0, (size_t)m->registers * 4);
    for (uint16_t i = 0; i < m->ins; i++)
        regs[m->registers - m->ins + i] = args[i];

    Dex *dex = m->dex;
    const uint16_t *insns = m->insns;
    const uint16_t *pc = insns;
    uint64_t result = 0;
    Ref caught = 0;

#define VA ((inst >> 8) & 0xff)
#define NA ((inst >> 8) & 0xf)
#define NB ((inst >> 12) & 0xf)
#define S16(i) ((int16_t)pc[i])
#define S32(i) ((int32_t)((uint32_t)pc[i] | (uint32_t)pc[(i) + 1] << 16))
#define NEXT(n) do { pc += (n); goto next; } while (0)
#define THROW_NEW(cls, msg) do { vm_throw(t, cls, msg); goto exception; } while (0)
#define NULLCHK(r) do { if ((r) == 0) { vm_throw(t, "Ljava/lang/NullPointerException;", NULL); goto exception; } } while (0)
#define BRANCH(off) do { int32_t o_ = (off); pc += o_; if (o_ <= 0 && vm->gc_wanted) vm_gc(t); goto next; } while (0)

next:;
    uint16_t inst = *pc;
    switch (inst & 0xff) {
    case 0x00: { /* nop, or a payload pseudo-instruction that is never executed */ NEXT(1); }
    case 0x01: regs[NA] = regs[NB]; NEXT(1);
    case 0x02: regs[VA] = regs[pc[1]]; NEXT(2);
    case 0x03: regs[pc[1]] = regs[pc[2]]; NEXT(3);
    case 0x04: ww(&regs[NA], rw(&regs[NB])); NEXT(1);
    case 0x05: ww(&regs[VA], rw(&regs[pc[1]])); NEXT(2);
    case 0x06: ww(&regs[pc[1]], rw(&regs[pc[2]])); NEXT(3);
    case 0x07: regs[NA] = regs[NB]; NEXT(1);
    case 0x08: regs[VA] = regs[pc[1]]; NEXT(2);
    case 0x09: regs[pc[1]] = regs[pc[2]]; NEXT(3);
    case 0x0a: regs[VA] = (uint32_t)result; NEXT(1);
    case 0x0b: ww(&regs[VA], result); NEXT(1);
    case 0x0c: regs[VA] = (uint32_t)result; NEXT(1);
    case 0x0d: regs[VA] = caught; caught = 0; NEXT(1);
    case 0x0e: result = 0; goto done;
    case 0x0f: result = regs[VA]; goto done;
    case 0x10: result = rw(&regs[VA]); goto done;
    case 0x11: result = regs[VA]; goto done;
    case 0x12: regs[NA] = (uint32_t)((int32_t)(int8_t)(NB << 4) >> 4); NEXT(1);
    case 0x13: regs[VA] = (uint32_t)(int32_t)S16(1); NEXT(2);
    case 0x14: regs[VA] = (uint32_t)S32(1); NEXT(3);
    case 0x15: regs[VA] = (uint32_t)pc[1] << 16; NEXT(2);
    case 0x16: ww(&regs[VA], (uint64_t)(int64_t)S16(1)); NEXT(2);
    case 0x17: ww(&regs[VA], (uint64_t)(int64_t)S32(1)); NEXT(3);
    case 0x18: ww(&regs[VA], (uint64_t)(uint32_t)S32(1) | (uint64_t)(uint32_t)S32(3) << 32); NEXT(5);
    case 0x19: ww(&regs[VA], (uint64_t)pc[1] << 48); NEXT(2);
    case 0x1a: case 0x1b: {
        uint32_t sidx = (inst & 0xff) == 0x1a ? pc[1] : (uint32_t)S32(1);
        Ref s = dex->res_strings[sidx];
        if (!s) {
            s = vm_intern_string(t, vm_new_string_utf8(t, dex_string(dex, sidx)));
            dex->res_strings[sidx] = s;
        }
        regs[VA] = s;
        NEXT((inst & 0xff) == 0x1a ? 2 : 3);
    }
    case 0x1c: {
        Class *c = vm_resolve_type(t, dex, pc[1]);
        if (!c) goto exception;
        regs[VA] = vm_class_object(t, c);
        NEXT(2);
    }
    case 0x1d: NULLCHK(regs[VA]); NEXT(1);
    case 0x1e: NULLCHK(regs[VA]); NEXT(1);
    case 0x1f: {
        Ref r = regs[VA];
        if (r) {
            Class *c = vm_resolve_type(t, dex, pc[1]);
            if (!c) goto exception;
            if (!vm_instance_of(vm_obj(vm, r)->cls, c)) {
                char msg[256];
                snprintf(msg, sizeof msg, "%s cannot be cast to %s", vm_obj(vm, r)->cls->name, c->name);
                THROW_NEW("Ljava/lang/ClassCastException;", msg);
            }
        }
        NEXT(2);
    }
    case 0x20: {
        Ref r = regs[NB];
        Class *c = vm_resolve_type(t, dex, pc[1]);
        if (!c) goto exception;
        regs[NA] = r && vm_instance_of(vm_obj(vm, r)->cls, c);
        NEXT(2);
    }
    case 0x21: {
        Ref r = regs[NB];
        NULLCHK(r);
        regs[NA] = vm_obj(vm, r)->length;
        NEXT(1);
    }
    case 0x22: {
        Class *c = vm_resolve_type(t, dex, pc[1]);
        if (!c || vm_init_class(t, c) != 0) goto exception;
        if (c->access & (ACC_ABSTRACT | ACC_INTERFACE)) THROW_NEW("Ljava/lang/InstantiationError;", c->name);
        regs[VA] = c == vm->cls_string ? vm_new_string_utf16(t, NULL, 0) : vm_new_object(t, c);
        NEXT(2);
    }
    case 0x23: {
        Class *c = vm_resolve_type(t, dex, pc[1]);
        if (!c) goto exception;
        int32_t n = (int32_t)regs[NB];
        if (n < 0) { char msg[32]; snprintf(msg, sizeof msg, "%d", n); THROW_NEW("Ljava/lang/NegativeArraySizeException;", msg); }
        Ref a = vm_new_array(t, c, (uint32_t)n);
        if (t->exception) goto exception;
        regs[NA] = a;
        NEXT(2);
    }
    case 0x24: case 0x25: {
        Class *c = vm_resolve_type(t, dex, pc[1]);
        if (!c) goto exception;
        uint32_t count, first = 0;
        uint32_t list[5];
        if ((inst & 0xff) == 0x24) {
            count = inst >> 12;
            list[0] = pc[2] & 0xf; list[1] = (pc[2] >> 4) & 0xf; list[2] = (pc[2] >> 8) & 0xf; list[3] = (pc[2] >> 12) & 0xf;
            list[4] = (inst >> 8) & 0xf;
        } else {
            count = (inst >> 8) & 0xff;
            first = pc[2];
        }
        Ref a = vm_new_array(t, c, count);
        if (t->exception) goto exception;
        Object *ao = vm_obj(vm, a);
        uint32_t es = vm_elem_size(c->array_kind);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t v = regs[(inst & 0xff) == 0x24 ? list[i] : first + i];
            if (es == 1) ao->u.bytes[i] = (uint8_t)v;
            else if (es == 2) ((uint16_t *)ao->u.bytes)[i] = (uint16_t)v;
            else ((uint32_t *)ao->u.bytes)[i] = v;
        }
        result = a;
        NEXT(3);
    }
    case 0x26: {
        Ref r = regs[VA];
        NULLCHK(r);
        Object *ao = vm_obj(vm, r);
        const uint16_t *pay = pc + S32(1);
        uint32_t width = pay[1], size = (uint32_t)pay[2] | (uint32_t)pay[3] << 16;
        if (size > ao->length) THROW_NEW("Ljava/lang/ArrayIndexOutOfBoundsException;", "fill-array-data");
        memcpy(ao->u.bytes, pay + 4, (size_t)size * width);
        NEXT(3);
    }
    case 0x27: {
        Ref r = regs[VA];
        NULLCHK(r);
        t->exception = r;
        goto exception;
    }
    case 0x28: BRANCH((int8_t)(inst >> 8));
    case 0x29: BRANCH(S16(1));
    case 0x2a: BRANCH(S32(1));
    case 0x2b: {
        int32_t v = (int32_t)regs[VA];
        const uint16_t *pay = pc + S32(1);
        uint16_t size = pay[1];
        int32_t first = (int32_t)((uint32_t)pay[2] | (uint32_t)pay[3] << 16);
        if (v >= first && (int64_t)v - first < size) {
            const uint16_t *tgt = pay + 4 + 2 * (size_t)(v - first);
            BRANCH((int32_t)((uint32_t)tgt[0] | (uint32_t)tgt[1] << 16));
        }
        NEXT(3);
    }
    case 0x2c: {
        int32_t v = (int32_t)regs[VA];
        const uint16_t *pay = pc + S32(1);
        uint16_t size = pay[1];
        const uint16_t *keys = pay + 2, *tgts = keys + 2 * (size_t)size;
        for (uint16_t i = 0; i < size; i++) {
            int32_t k = (int32_t)((uint32_t)keys[2 * i] | (uint32_t)keys[2 * i + 1] << 16);
            if (k == v) BRANCH((int32_t)((uint32_t)tgts[2 * i] | (uint32_t)tgts[2 * i + 1] << 16));
        }
        NEXT(3);
    }
    case 0x2d: case 0x2e: {
        float a = u2f(regs[pc[1] & 0xff]), b = u2f(regs[pc[1] >> 8]);
        regs[VA] = (uint32_t)(a != a || b != b ? ((inst & 0xff) == 0x2d ? -1 : 1) : a == b ? 0 : a < b ? -1 : 1);
        NEXT(2);
    }
    case 0x2f: case 0x30: {
        double a = u2d(rw(&regs[pc[1] & 0xff])), b = u2d(rw(&regs[pc[1] >> 8]));
        regs[VA] = (uint32_t)(a != a || b != b ? ((inst & 0xff) == 0x2f ? -1 : 1) : a == b ? 0 : a < b ? -1 : 1);
        NEXT(2);
    }
    case 0x31: {
        int64_t a = (int64_t)rw(&regs[pc[1] & 0xff]), b = (int64_t)rw(&regs[pc[1] >> 8]);
        regs[VA] = (uint32_t)(a == b ? 0 : a < b ? -1 : 1);
        NEXT(2);
    }
#define IF2(op, cond) case op: if ((int32_t)regs[NA] cond (int32_t)regs[NB]) BRANCH(S16(1)); NEXT(2);
    IF2(0x32, ==) IF2(0x33, !=) IF2(0x34, <) IF2(0x35, >=) IF2(0x36, >) IF2(0x37, <=)
#define IFZ(op, cond) case op: if ((int32_t)regs[VA] cond 0) BRANCH(S16(1)); NEXT(2);
    IFZ(0x38, ==) IFZ(0x39, !=) IFZ(0x3a, <) IFZ(0x3b, >=) IFZ(0x3c, >) IFZ(0x3d, <=)

    /* aget / aput */
#define AGET_PREP \
    Ref ar = regs[pc[1] & 0xff]; uint32_t ix = regs[pc[1] >> 8]; \
    NULLCHK(ar); Object *ao = vm_obj(vm, ar); \
    if (ix >= ao->length) { char msg[64]; snprintf(msg, sizeof msg, "index=%d length=%u", (int)ix, ao->length); THROW_NEW("Ljava/lang/ArrayIndexOutOfBoundsException;", msg); }
    case 0x44: { AGET_PREP regs[VA] = ((uint32_t *)ao->u.bytes)[ix]; NEXT(2); }
    case 0x45: { AGET_PREP ww(&regs[VA], ((uint64_t *)ao->u.bytes)[ix]); NEXT(2); }
    case 0x46: { AGET_PREP regs[VA] = ((uint32_t *)ao->u.bytes)[ix]; NEXT(2); }
    case 0x47: { AGET_PREP regs[VA] = ao->u.bytes[ix]; NEXT(2); }
    case 0x48: { AGET_PREP regs[VA] = (uint32_t)(int32_t)(int8_t)ao->u.bytes[ix]; NEXT(2); }
    case 0x49: { AGET_PREP regs[VA] = ((uint16_t *)ao->u.bytes)[ix]; NEXT(2); }
    case 0x4a: { AGET_PREP regs[VA] = (uint32_t)(int32_t)((int16_t *)ao->u.bytes)[ix]; NEXT(2); }
    case 0x4b: { AGET_PREP ((uint32_t *)ao->u.bytes)[ix] = regs[VA]; NEXT(2); }
    case 0x4c: { AGET_PREP ((uint64_t *)ao->u.bytes)[ix] = rw(&regs[VA]); NEXT(2); }
    case 0x4d: { AGET_PREP ((uint32_t *)ao->u.bytes)[ix] = regs[VA]; NEXT(2); }
    case 0x4e: case 0x4f: { AGET_PREP ao->u.bytes[ix] = (uint8_t)regs[VA]; NEXT(2); }
    case 0x50: case 0x51: { AGET_PREP ((uint16_t *)ao->u.bytes)[ix] = (uint16_t)regs[VA]; NEXT(2); }

    /* iget / iput */
#define IFIELD_PREP \
    Ref ir = regs[NB]; NULLCHK(ir); \
    Field *f = vm_resolve_field(t, dex, pc[1]); if (!f) goto exception; \
    Object *io = vm_obj(vm, ir);
    case 0x52: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: {
        IFIELD_PREP
        uint32_t w = io->u.words[f->offset];
        uint8_t op = inst & 0xff;
        regs[NA] = op == 0x55 ? (w & 0xff) : op == 0x56 ? (uint32_t)(int32_t)(int8_t)w : op == 0x57 ? (w & 0xffff) : op == 0x58 ? (uint32_t)(int32_t)(int16_t)w : w;
        NEXT(2);
    }
    case 0x53: { IFIELD_PREP ww(&regs[NA], (uint64_t)io->u.words[f->offset] | (uint64_t)io->u.words[f->offset + 1] << 32); NEXT(2); }
    case 0x59: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: { IFIELD_PREP io->u.words[f->offset] = regs[NA]; NEXT(2); }
    case 0x5a: { IFIELD_PREP io->u.words[f->offset] = regs[NA]; io->u.words[f->offset + 1] = regs[NA + 1]; NEXT(2); }

    /* sget / sput */
#define SFIELD_PREP \
    Field *f = vm_resolve_field(t, dex, pc[1]); if (!f) goto exception; \
    if (f->cls->state != 3 && vm_init_class(t, f->cls) != 0) goto exception;
    case 0x60: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: {
        SFIELD_PREP
        uint32_t w = f->cls->statics[f->offset];
        uint8_t op = inst & 0xff;
        regs[VA] = op == 0x63 ? (w & 0xff) : op == 0x64 ? (uint32_t)(int32_t)(int8_t)w : op == 0x65 ? (w & 0xffff) : op == 0x66 ? (uint32_t)(int32_t)(int16_t)w : w;
        NEXT(2);
    }
    case 0x61: { SFIELD_PREP ww(&regs[VA], (uint64_t)f->cls->statics[f->offset] | (uint64_t)f->cls->statics[f->offset + 1] << 32); NEXT(2); }
    case 0x67: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: { SFIELD_PREP f->cls->statics[f->offset] = regs[VA]; NEXT(2); }
    case 0x68: { SFIELD_PREP f->cls->statics[f->offset] = regs[VA]; f->cls->statics[f->offset + 1] = regs[VA + 1]; NEXT(2); }

    /* invoke */
    case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x72:
    case 0x74: case 0x75: case 0x76: case 0x77: case 0x78: {
        uint8_t op = inst & 0xff;
        int range = op >= 0x74;
        uint8_t kind = range ? op - 0x74 : op - 0x6e; /* 0 virtual 1 super 2 direct 3 static 4 interface */
        uint32_t midx = pc[1];
        uint32_t count = range ? (inst >> 8) & 0xff : inst >> 12;
        uint32_t argv[count ? count : 1];
        if (range) {
            for (uint32_t i = 0; i < count; i++) argv[i] = regs[pc[2] + i];
        } else {
            uint32_t l[5] = {pc[2] & 0xfu, (pc[2] >> 4) & 0xfu, (pc[2] >> 8) & 0xfu, (pc[2] >> 12) & 0xfu, (inst >> 8) & 0xfu};
            for (uint32_t i = 0; i < count; i++) argv[i] = regs[l[i]];
        }
        Method *target = vm_resolve_method(t, dex, midx);
        if (!target) goto exception;
        if (kind == 3) {
            if (target->cls->state != 3 && vm_init_class(t, target->cls) != 0) goto exception;
        } else {
            if (count == 0 || argv[0] == 0) THROW_NEW("Ljava/lang/NullPointerException;", target->sig);
            if (kind == 0 || kind == 4) {
                Class *rc = vm_obj(vm, argv[0])->cls;
                Method *v = vm_lookup_virtual(rc, target->sig);
                if (!v) THROW_NEW("Ljava/lang/AbstractMethodError;", target->sig);
                target = v;
            } else if (kind == 1) {
                Method *v = m->cls->super ? vm_lookup_virtual(m->cls->super, target->sig) : NULL;
                if (v) target = v;
            }
        }
        if (vm->gc_wanted) vm_gc(t);
        result = vm_invoke(t, target, argv);
        if (t->exception) goto exception;
        NEXT(3);
    }

    /* unary */
    case 0x7b: regs[NA] = (uint32_t)(-(int32_t)regs[NB]); NEXT(1);
    case 0x7c: regs[NA] = ~regs[NB]; NEXT(1);
    case 0x7d: ww(&regs[NA], (uint64_t)(-(int64_t)rw(&regs[NB]))); NEXT(1);
    case 0x7e: ww(&regs[NA], ~rw(&regs[NB])); NEXT(1);
    case 0x7f: regs[NA] = f2u(-u2f(regs[NB])); NEXT(1);
    case 0x80: ww(&regs[NA], d2u(-u2d(rw(&regs[NB])))); NEXT(1);
    case 0x81: ww(&regs[NA], (uint64_t)(int64_t)(int32_t)regs[NB]); NEXT(1);
    case 0x82: regs[NA] = f2u((float)(int32_t)regs[NB]); NEXT(1);
    case 0x83: ww(&regs[NA], d2u((double)(int32_t)regs[NB])); NEXT(1);
    case 0x84: regs[NA] = (uint32_t)rw(&regs[NB]); NEXT(1);
    case 0x85: regs[NA] = f2u((float)(int64_t)rw(&regs[NB])); NEXT(1);
    case 0x86: ww(&regs[NA], d2u((double)(int64_t)rw(&regs[NB]))); NEXT(1);
    case 0x87: regs[NA] = (uint32_t)f2i(u2f(regs[NB])); NEXT(1);
    case 0x88: ww(&regs[NA], (uint64_t)f2l(u2f(regs[NB]))); NEXT(1);
    case 0x89: ww(&regs[NA], d2u((double)u2f(regs[NB]))); NEXT(1);
    case 0x8a: regs[NA] = (uint32_t)d2i(u2d(rw(&regs[NB]))); NEXT(1);
    case 0x8b: ww(&regs[NA], (uint64_t)d2l(u2d(rw(&regs[NB])))); NEXT(1);
    case 0x8c: regs[NA] = f2u((float)u2d(rw(&regs[NB]))); NEXT(1);
    case 0x8d: regs[NA] = (uint32_t)(int32_t)(int8_t)regs[NB]; NEXT(1);
    case 0x8e: regs[NA] = (uint16_t)regs[NB]; NEXT(1);
    case 0x8f: regs[NA] = (uint32_t)(int32_t)(int16_t)regs[NB]; NEXT(1);

    default: break;
    }

    /* binary operations: 0x90-0xaf (three registers), 0xb0-0xcf (two address), 0xd0-0xe2 (literal) */
    {
        uint8_t op = inst & 0xff;
        if (op >= 0x90 && op <= 0xaf) {
            uint32_t d = VA, a = pc[1] & 0xff, b = pc[1] >> 8;
            uint8_t k = op - 0x90;
            if (k <= 0x0a) { /* int */
                int32_t x = (int32_t)regs[a], y = (int32_t)regs[b];
                switch (k) {
                case 0: regs[d] = (uint32_t)(x + y); break;
                case 1: regs[d] = (uint32_t)(x - y); break;
                case 2: regs[d] = (uint32_t)((uint32_t)x * (uint32_t)y); break;
                case 3: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); regs[d] = (uint32_t)(y == -1 ? -(uint32_t)x : (uint32_t)(x / y)); break;
                case 4: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); regs[d] = (uint32_t)(y == -1 ? 0 : x % y); break;
                case 5: regs[d] = (uint32_t)(x & y); break;
                case 6: regs[d] = (uint32_t)(x | y); break;
                case 7: regs[d] = (uint32_t)(x ^ y); break;
                case 8: regs[d] = (uint32_t)x << (y & 31); break;
                case 9: regs[d] = (uint32_t)(x >> (y & 31)); break;
                default: regs[d] = (uint32_t)x >> (y & 31); break;
                }
            } else if (k <= 0x15) { /* long */
                int64_t x = (int64_t)rw(&regs[a]);
                int64_t y = k >= 0x13 ? (int64_t)regs[b] : (int64_t)rw(&regs[b]);
                uint64_t r;
                switch (k - 0x0b) {
                case 0: r = (uint64_t)(x + y); break;
                case 1: r = (uint64_t)(x - y); break;
                case 2: r = (uint64_t)x * (uint64_t)y; break;
                case 3: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); r = y == -1 ? -(uint64_t)x : (uint64_t)(x / y); break;
                case 4: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); r = y == -1 ? 0 : (uint64_t)(x % y); break;
                case 5: r = (uint64_t)(x & y); break;
                case 6: r = (uint64_t)(x | y); break;
                case 7: r = (uint64_t)(x ^ y); break;
                case 8: r = (uint64_t)x << (y & 63); break;
                case 9: r = (uint64_t)(x >> (y & 63)); break;
                default: r = (uint64_t)x >> (y & 63); break;
                }
                ww(&regs[d], r);
            } else if (k <= 0x1a) { /* float */
                float x = u2f(regs[a]), y = u2f(regs[b]), r;
                switch (k - 0x16) { case 0: r = x + y; break; case 1: r = x - y; break; case 2: r = x * y; break; case 3: r = x / y; break; default: r = fmodf(x, y); break; }
                regs[d] = f2u(r);
            } else { /* double */
                double x = u2d(rw(&regs[a])), y = u2d(rw(&regs[b])), r;
                switch (k - 0x1b) { case 0: r = x + y; break; case 1: r = x - y; break; case 2: r = x * y; break; case 3: r = x / y; break; default: r = fmod(x, y); break; }
                ww(&regs[d], d2u(r));
            }
            NEXT(2);
        }
        if (op >= 0xb0 && op <= 0xcf) {
            uint32_t d = NA, b = NB;
            uint8_t k = op - 0xb0;
            if (k <= 0x0a) {
                int32_t x = (int32_t)regs[d], y = (int32_t)regs[b];
                switch (k) {
                case 0: regs[d] = (uint32_t)(x + y); break;
                case 1: regs[d] = (uint32_t)(x - y); break;
                case 2: regs[d] = (uint32_t)((uint32_t)x * (uint32_t)y); break;
                case 3: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); regs[d] = (uint32_t)(y == -1 ? -(uint32_t)x : (uint32_t)(x / y)); break;
                case 4: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); regs[d] = (uint32_t)(y == -1 ? 0 : x % y); break;
                case 5: regs[d] = (uint32_t)(x & y); break;
                case 6: regs[d] = (uint32_t)(x | y); break;
                case 7: regs[d] = (uint32_t)(x ^ y); break;
                case 8: regs[d] = (uint32_t)x << (y & 31); break;
                case 9: regs[d] = (uint32_t)(x >> (y & 31)); break;
                default: regs[d] = (uint32_t)x >> (y & 31); break;
                }
            } else if (k <= 0x15) {
                int64_t x = (int64_t)rw(&regs[d]);
                int64_t y = k >= 0x13 ? (int64_t)regs[b] : (int64_t)rw(&regs[b]);
                uint64_t r;
                switch (k - 0x0b) {
                case 0: r = (uint64_t)(x + y); break;
                case 1: r = (uint64_t)(x - y); break;
                case 2: r = (uint64_t)x * (uint64_t)y; break;
                case 3: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); r = y == -1 ? -(uint64_t)x : (uint64_t)(x / y); break;
                case 4: if (!y) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); r = y == -1 ? 0 : (uint64_t)(x % y); break;
                case 5: r = (uint64_t)(x & y); break;
                case 6: r = (uint64_t)(x | y); break;
                case 7: r = (uint64_t)(x ^ y); break;
                case 8: r = (uint64_t)x << (y & 63); break;
                case 9: r = (uint64_t)(x >> (y & 63)); break;
                default: r = (uint64_t)x >> (y & 63); break;
                }
                ww(&regs[d], r);
            } else if (k <= 0x1a) {
                float x = u2f(regs[d]), y = u2f(regs[b]), r;
                switch (k - 0x16) { case 0: r = x + y; break; case 1: r = x - y; break; case 2: r = x * y; break; case 3: r = x / y; break; default: r = fmodf(x, y); break; }
                regs[d] = f2u(r);
            } else {
                double x = u2d(rw(&regs[d])), y = u2d(rw(&regs[b])), r;
                switch (k - 0x1b) { case 0: r = x + y; break; case 1: r = x - y; break; case 2: r = x * y; break; case 3: r = x / y; break; default: r = fmod(x, y); break; }
                ww(&regs[d], d2u(r));
            }
            NEXT(1);
        }
        if (op >= 0xd0 && op <= 0xe2) {
            int is16 = op <= 0xd7;
            uint32_t d = NA, b = NB;
            int32_t lit, x;
            if (is16) { lit = S16(1); x = (int32_t)regs[b]; }
            else { d = VA; b = pc[1] & 0xff; lit = (int8_t)(pc[1] >> 8); x = (int32_t)regs[b]; }
            uint8_t k = is16 ? op - 0xd0 : op - 0xd8;
            switch (k) {
            case 0: regs[d] = (uint32_t)(x + lit); break;
            case 1: regs[d] = (uint32_t)(lit - x); break; /* rsub */
            case 2: regs[d] = (uint32_t)((uint32_t)x * (uint32_t)lit); break;
            case 3: if (!lit) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); regs[d] = (uint32_t)(lit == -1 ? -(uint32_t)x : (uint32_t)(x / lit)); break;
            case 4: if (!lit) THROW_NEW("Ljava/lang/ArithmeticException;", "divide by zero"); regs[d] = (uint32_t)(lit == -1 ? 0 : x % lit); break;
            case 5: regs[d] = (uint32_t)(x & lit); break;
            case 6: regs[d] = (uint32_t)(x | lit); break;
            case 7: regs[d] = (uint32_t)(x ^ lit); break;
            case 8: regs[d] = (uint32_t)x << (lit & 31); break;
            case 9: regs[d] = (uint32_t)(x >> (lit & 31)); break;
            default: regs[d] = (uint32_t)x >> (lit & 31); break;
            }
            NEXT(2);
        }
        char msg[64];
        snprintf(msg, sizeof msg, "opcode 0x%02x in %s", op, m->sig);
        vm_throw(t, "Ljava/lang/UnsupportedOperationException;", msg);
    }

exception: {
        Ref er = t->exception;
        Object *eo = vm_obj(vm, er);
        int32_t h = eo ? find_handler(t, m, (uint32_t)(pc - insns), eo) : -1;
        if (h >= 0) {
            caught = er;
            t->exception = 0;
            pc = insns + h;
            goto next;
        }
        t->sp = frame_base;
        t->depth--;
        return 0;
    }

done:
    t->sp = frame_base;
    t->depth--;
    return result;
}
