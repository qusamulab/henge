/* Class loading, linking and initialization, method lookup, interning, the native registry and
 * VM creation. */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VM *g_vm; /* one VM per process */

/* ---- interning ---- */

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

const char *vm_intern(VM *vm, const char *s)
{
    if (vm->ninterned * 2 >= vm->cap_interned) {
        size_t cap = vm->cap_interned ? vm->cap_interned * 2 : 4096;
        char **n = calloc(cap, sizeof *n);
        for (size_t i = 0; i < vm->cap_interned; i++) {
            if (!vm->interned[i]) continue;
            size_t k = hash_str(vm->interned[i]) & (cap - 1);
            while (n[k]) k = (k + 1) & (cap - 1);
            n[k] = vm->interned[i];
        }
        free(vm->interned);
        vm->interned = n;
        vm->cap_interned = cap;
    }
    size_t k = hash_str(s) & (vm->cap_interned - 1);
    while (vm->interned[k]) {
        if (strcmp(vm->interned[k], s) == 0)
            return vm->interned[k];
        k = (k + 1) & (vm->cap_interned - 1);
    }
    vm->interned[k] = strdup(s);
    vm->ninterned++;
    return vm->interned[k];
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---- the class index: descriptor -> (dex, class_def, loaded class) ---- */

struct class_entry {
    const char *name; /* interned descriptor */
    Dex *dex;
    const uint8_t *def;
    Class *cls;
};
static struct { struct class_entry *e; size_t n, cap; } g_index;

/* The returned pointer is only valid until the next index_find that creates an entry. */
static struct class_entry *index_find(const char *name, int create)
{
    if (create && g_index.n * 2 >= g_index.cap) {
        size_t cap = g_index.cap ? g_index.cap * 2 : 2048;
        struct class_entry *n = calloc(cap, sizeof *n);
        for (size_t i = 0; i < g_index.cap; i++) {
            if (!g_index.e[i].name) continue;
            size_t k = hash_str(g_index.e[i].name) & (cap - 1);
            while (n[k].name) k = (k + 1) & (cap - 1);
            n[k] = g_index.e[i];
        }
        free(g_index.e);
        g_index.e = n;
        g_index.cap = cap;
    }
    if (!g_index.cap)
        return NULL;
    size_t k = hash_str(name) & (g_index.cap - 1);
    while (g_index.e[k].name) {
        if (strcmp(g_index.e[k].name, name) == 0)
            return &g_index.e[k];
        k = (k + 1) & (g_index.cap - 1);
    }
    if (!create)
        return NULL;
    g_index.n++;
    return &g_index.e[k];
}

int vm_add_dex(VM *vm, const uint8_t *data, size_t size, char *err, size_t errlen)
{
    Dex *dex = calloc(1, sizeof *dex);
    if (!dex) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    if (dex_open(dex, data, size, err, errlen) != 0) {
        free(dex);
        return -1;
    }
    Dex **dexes = realloc(vm->dexes, (vm->ndex + 1) * sizeof *vm->dexes);
    if (!dexes) {
        dex_close(dex);
        free(dex);
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    vm->dexes = dexes;
    vm->dexes[vm->ndex++] = dex;
    for (uint32_t i = 0; i < dex->nclasses; i++) {
        const uint8_t *def = dex->class_defs + i * 32;
        const char *name = vm_intern(vm, dex_type(dex, rd32(def)));
        struct class_entry *e = index_find(name, 1);
        if (!e->name) { /* the first definition wins, as on a classpath */
            e->name = name;
            e->dex = dex;
            e->def = def;
        }
    }
    return 0;
}

/* java.* (and the other platform packages henge provides itself) live under henge/rt/ in the core dex. */
static const char *remap(VM *vm, const char *desc)
{
    static const char *const prefixes[] = {"Ldalvik/", "Llibcore/", "Lsun/"}; /* none of these exist in the core: kept as a hook */
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        if (strncmp(desc, prefixes[i], strlen(prefixes[i])) == 0) {
            char buf[512];
            snprintf(buf, sizeof buf, "Lhenge/rt/%s", desc + 1); /* unused: no such classes */
            return vm_intern(vm, buf);
        }
    }
    return vm_intern(vm, desc);
}

/* ---- signatures ---- */

/* Rewrites every java.* type in a method signature ("name(params)ret") to its henge/rt/ name, so a
 * signature compares equal however the class that declares it spells its types. */
static const char *canon_sig(VM *vm, const char *raw)
{
    char out[1536];
    size_t k = 0;
    const char *p = strchr(raw, '(');
    if (!p) return vm_intern(vm, raw);
    memcpy(out, raw, (size_t)(p - raw));
    k = (size_t)(p - raw);
    while (*p && k < sizeof out - 520) {
        if (*p == 'L') {
            const char *e = strchr(p, ';');
            if (!e) break;
            char one[512];
            size_t n = (size_t)(e - p) + 1;
            if (n >= sizeof one) n = sizeof one - 1;
            memcpy(one, p, n);
            one[n] = 0;
            const char *r = remap(vm, one);
            size_t rl = strlen(r);
            memcpy(out + k, r, rl);
            k += rl;
            p = e + 1;
        } else {
            out[k++] = *p++;
        }
    }
    out[k] = 0;
    return vm_intern(vm, out);
}

static const char *build_sig(VM *vm, Dex *dex, const char *name, uint32_t proto_idx)
{
    const uint8_t *proto = dex->proto_ids + proto_idx * 12;
    char buf[1024];
    int n = snprintf(buf, sizeof buf, "%s(", name);
    uint32_t params_off = rd32(proto + 8);
    if (params_off) {
        const uint8_t *tl = dex->data + params_off;
        uint32_t cnt = rd32(tl);
        for (uint32_t i = 0; i < cnt; i++)
            n += snprintf(buf + n, sizeof buf - (size_t)n, "%s", dex_type(dex, rd16(tl + 4 + i * 2)));
    }
    snprintf(buf + n, sizeof buf - (size_t)n, ")%s", dex_type(dex, rd32(proto + 4)));
    return canon_sig(vm, buf);
}

uint16_t vm_sig_arg_words(const char *sig, int is_static)
{
    uint16_t w = is_static ? 0 : 1;
    const char *p = strchr(sig, '(') + 1;
    while (*p != ')') {
        if (*p == 'J' || *p == 'D') { w += 2; p++; }
        else if (*p == 'L') { w++; p = strchr(p, ';') + 1; }
        else if (*p == '[') { w++; while (*p == '[') p++; if (*p == 'L') p = strchr(p, ';') + 1; else p++; }
        else { w++; p++; }
    }
    return w;
}

/* ---- native registry ---- */

struct native_entry { char *key; NativeFn fn; };
struct natives { struct native_entry *e; size_t n, cap; };

void vm_register_native(VM *vm, const char *cls, const char *name, const char *proto, NativeFn fn)
{
    struct natives *nt = vm->natives;
    if (!nt) nt = vm->natives = calloc(1, sizeof *nt);
    char key[512];
    char raw[512];
    snprintf(raw, sizeof raw, "%s%s", name, proto);
    snprintf(key, sizeof key, "%s.%s", remap(vm, cls), canon_sig(vm, raw));
    if (nt->n == nt->cap) {
        nt->cap = nt->cap ? nt->cap * 2 : 512;
        nt->e = realloc(nt->e, nt->cap * sizeof *nt->e);
    }
    nt->e[nt->n].key = strdup(key);
    nt->e[nt->n].fn = fn;
    nt->n++;
}

static NativeFn find_native(VM *vm, const char *cls, const char *sig)
{
    struct natives *nt = vm->natives;
    if (!nt) return NULL;
    char key[512];
    snprintf(key, sizeof key, "%s.%s", cls, sig);
    for (size_t i = 0; i < nt->n; i++)
        if (strcmp(nt->e[i].key, key) == 0) return nt->e[i].fn;
    return NULL;
}

/* ---- class lookup ---- */

static void add_class(VM *vm, Class *c)
{
    if (vm->nclasses == vm->cap_classes) {
        vm->cap_classes = vm->cap_classes ? vm->cap_classes * 2 : 256;
        vm->classes = realloc(vm->classes, vm->cap_classes * sizeof *vm->classes);
    }
    vm->classes[vm->nclasses++] = c;
}

static Class *load_class(VM *vm, const char *name, Dex *dex, const uint8_t *def);

/* descriptor is canonical (java.* already remapped inside). */
Class *vm_array_class(VM *vm, const char *descriptor)
{
    descriptor = vm_intern(vm, descriptor);
    struct class_entry *e = index_find(descriptor, 0);
    if (e && e->cls)
        return e->cls;
    Class *component = NULL;
    if (descriptor[1] == 'L' || descriptor[1] == '[')
        component = vm_find_class(vm, descriptor + 1);
    Class *c = calloc(1, sizeof *c);
    c->name = descriptor;
    c->array_kind = descriptor[1];
    c->access = ACC_PUBLIC | ACC_FINAL;
    c->super = vm->cls_object;
    c->state = 3;
    c->component = component;
    if (c->super) {
        c->vtable = c->super->vtable;
        c->nvtable = c->super->nvtable;
    }
    e = index_find(descriptor, 1);
    e->name = descriptor;
    e->cls = c;
    add_class(vm, c);
    return c;
}

Class *vm_find_class(VM *vm, const char *descriptor)
{
    if (descriptor[0] == '[') {
        const char *p = descriptor;
        while (*p == '[') p++;
        if (*p != 'L')
            return vm_array_class(vm, descriptor);
        char buf[512];
        size_t dims = (size_t)(p - descriptor);
        memcpy(buf, descriptor, dims);
        snprintf(buf + dims, sizeof buf - dims, "%s", remap(vm, p));
        return vm_array_class(vm, buf);
    }
    const char *name = remap(vm, descriptor);
    struct class_entry *e = index_find(name, 0);
    if (!e)
        return NULL;
    if (e->cls)
        return e->cls;
    return load_class(vm, e->name, e->dex, e->def);
}

Class *vm_resolve_type_slow(Thread *t, Dex *dex, uint32_t type_idx)
{
    Class *c = dex->classes_by_type[type_idx];
    if (c)
        return c;
    c = vm_find_class(t->vm, dex_type(dex, type_idx));
    if (!c) {
        vm_throw(t, "Ljava/lang/NoClassDefFoundError;", dex_type(dex, type_idx));
        return NULL;
    }
    dex->classes_by_type[type_idx] = c;
    return c;
}

static Method *find_declared(Class *c, const char *sig)
{
    for (uint16_t i = 0; i < c->nmethods; i++)
        if (c->methods[i].sig == sig) return &c->methods[i];
    return NULL;
}

/* Same name and parameters, any return type: a call compiled against a covariant override
 * (FloatBuffer.position(int) returning FloatBuffer) still finds the inherited method. */
static Method *find_loose(Class *c, const char *sig)
{
    const char *close = strchr(sig, ')');
    size_t n = close ? (size_t)(close - sig) + 1 : strlen(sig);
    for (Class *k = c; k; k = k->super)
        for (uint16_t i = 0; i < k->nmethods; i++)
            if (strncmp(k->methods[i].sig, sig, n) == 0) return &k->methods[i];
    return NULL;
}

static Method *find_in_hierarchy(Class *c, const char *sig)
{
    for (Class *k = c; k; k = k->super) {
        Method *m = find_declared(k, sig);
        if (m) return m;
    }
    for (Class *k = c; k; k = k->super)
        for (uint16_t i = 0; i < k->ninterfaces; i++) {
            Method *m = find_in_hierarchy(k->interfaces[i], sig);
            if (m) return m;
        }
    return NULL;
}

Method *vm_find_method(Class *cls, const char *name, const char *proto)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s%s", name, proto);
    return find_in_hierarchy(cls, canon_sig(g_vm, buf));
}

/* Resolves the method a method_id names (declared in, or inherited by, the named class). */
Method *vm_resolve_method_slow(Thread *t, Dex *dex, uint32_t method_idx)
{
    Method *m = dex->res_methods[method_idx];
    if (m)
        return m;
    const uint8_t *mid = dex->method_ids + method_idx * 8;
    Class *c = vm_resolve_type(t, dex, rd16(mid));
    if (!c)
        return NULL;
    const char *sig = dex->msigs[method_idx];
    if (!sig) {
        sig = build_sig(t->vm, dex, dex_string(dex, rd32(mid + 4)), rd16(mid + 2));
        dex->msigs[method_idx] = sig;
    }
    m = find_in_hierarchy(c, sig);
    if (!m) m = find_loose(c, sig);
    if (!m && c->array_kind) /* methods on arrays (clone, getClass, ...) come from Object */
        m = find_in_hierarchy(t->vm->cls_object, sig);
    if (!m) {
        char msg[512];
        snprintf(msg, sizeof msg, "%s.%s", c->name, sig);
        vm_throw(t, "Ljava/lang/NoSuchMethodError;", msg);
        return NULL;
    }
    dex->res_methods[method_idx] = m;
    return m;
}

const char *vm_method_sig(Thread *t, Dex *dex, uint32_t method_idx)
{
    const char *sig = dex->msigs[method_idx];
    if (!sig) {
        const uint8_t *mid = dex->method_ids + method_idx * 8;
        sig = build_sig(t->vm, dex, dex_string(dex, rd32(mid + 4)), rd16(mid + 2));
        dex->msigs[method_idx] = sig;
    }
    return sig;
}

/* Field resolution: declared in the named class or inherited. */
Field *vm_resolve_field_slow(Thread *t, Dex *dex, uint32_t field_idx)
{
    Field *f = dex->res_fields[field_idx];
    if (f)
        return f;
    const uint8_t *fid = dex->field_ids + field_idx * 8;
    Class *c = vm_resolve_type(t, dex, rd16(fid));
    if (!c)
        return NULL;
    const char *name = dex_string(dex, rd32(fid + 4));
    for (Class *k = c; k && !f; k = k->super) {
        for (uint16_t i = 0; i < k->nfields; i++)
            if (strcmp(k->fields[i].name, name) == 0) { f = &k->fields[i]; break; }
        if (!f)
            for (uint16_t j = 0; j < k->ninterfaces && !f; j++)
                for (uint16_t i = 0; i < k->interfaces[j]->nfields; i++)
                    if (strcmp(k->interfaces[j]->fields[i].name, name) == 0) { f = &k->interfaces[j]->fields[i]; break; }
    }
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof msg, "%s.%s", c->name, name);
        vm_throw(t, "Ljava/lang/NoSuchFieldError;", msg);
        return NULL;
    }
    dex->res_fields[field_idx] = f;
    return f;
}

uint32_t vm_field_offset(Class *c, const char *name)
{
    for (Class *k = c; k; k = k->super)
        for (uint16_t i = 0; i < k->nfields; i++)
            if (!(k->fields[i].access & ACC_STATIC) && strcmp(k->fields[i].name, name) == 0)
                return k->fields[i].offset;
    return UINT32_MAX;
}

/* ---- virtual dispatch ---- */

#define MCACHE 8192
static struct { Class *c; const char *sig; Method *m; } g_mcache[MCACHE];

Method *vm_lookup_virtual(Class *cls, const char *sig)
{
    size_t slot = (((uintptr_t)cls >> 4) ^ ((uintptr_t)sig >> 3)) & (MCACHE - 1);
    if (g_mcache[slot].c == cls && g_mcache[slot].sig == sig)
        return g_mcache[slot].m;
    Method *found = NULL;
    for (uint16_t i = 0; i < cls->nvtable; i++)
        if (cls->vtable[i]->sig == sig) { found = cls->vtable[i]; break; }
    g_mcache[slot].c = cls;
    g_mcache[slot].sig = sig;
    g_mcache[slot].m = found;
    return found;
}

int vm_instance_of(Class *k, Class *target)
{
    if (k == target || target == g_vm->cls_object)
        return 1;
    if (k->array_kind) {
        if (!target->array_kind)
            return strcmp(target->name, "Ljava/lang/Cloneable;") == 0 || strcmp(target->name, "Ljava/io/Serializable;") == 0 ||
                   0;
        if (k->array_kind != target->array_kind)
            return 0;
        if (!k->component || !target->component)
            return k->component == target->component;
        return vm_instance_of(k->component, target->component);
    }
    if (!(target->access & ACC_INTERFACE)) { /* a class can only be reached through super */
        for (Class *c = k; c; c = c->super)
            if (c == target)
                return 1;
        return 0;
    }
    for (Class *c = k; c; c = c->super) {
        if (c == target)
            return 1;
        for (uint16_t i = 0; i < c->ninterfaces; i++)
            if (vm_instance_of(c->interfaces[i], target))
                return 1;
    }
    return 0;
}

/* ---- loading ---- */

static void build_vtable(Class *c)
{
    size_t cap = (c->super ? c->super->nvtable : 0) + c->nmethods + 8;
    for (uint16_t i = 0; i < c->ninterfaces; i++)
        cap += c->interfaces[i]->nvtable;
    Method **vt = malloc((cap ? cap : 1) * sizeof *vt);
    size_t n = 0;
    if (c->super)
        for (uint16_t i = 0; i < c->super->nvtable; i++) vt[n++] = c->super->vtable[i];
    for (uint16_t i = 0; i < c->nmethods; i++) {
        Method *m = &c->methods[i];
        if (m->access & (ACC_STATIC | ACC_PRIVATE | ACC_CONSTRUCTOR))
            continue;
        size_t k;
        for (k = 0; k < n; k++)
            if (vt[k]->sig == m->sig) { vt[k] = m; break; }
        if (k == n) vt[n++] = m;
    }
    for (uint16_t j = 0; j < c->ninterfaces; j++) {
        Class *ic = c->interfaces[j];
        for (uint16_t i = 0; i < ic->nvtable; i++) {
            Method *im = ic->vtable[i];
            if ((im->access & ACC_ABSTRACT) && !(c->access & ACC_INTERFACE))
                continue;
            size_t k;
            for (k = 0; k < n; k++)
                if (vt[k]->sig == im->sig) break;
            if (k == n) vt[n++] = im;
        }
    }
    c->vtable = vt;
    c->nvtable = (uint16_t)n;
}

static int field_words(char type) { return type == 'J' || type == 'D' ? 2 : 1; }

static Class *load_class(VM *vm, const char *name, Dex *dex, const uint8_t *def)
{
    Class *c = calloc(1, sizeof *c);
    c->name = name;
    c->dex = dex;
    c->class_def = def;
    c->access = rd32(def + 4);
    uint32_t super_idx = rd32(def + 8);
    if (super_idx != 0xffffffffu && remap(vm, dex_type(dex, super_idx)) != name) {
        c->super = vm_find_class(vm, dex_type(dex, super_idx));
        if (!c->super) {
            fprintf(stderr, "henge-vm: %s: missing superclass %s\n", name, dex_type(dex, super_idx));
            free(c);
            return NULL;
        }
    }
    uint32_t if_off = rd32(def + 12);
    if (if_off) {
        const uint8_t *tl = dex->data + if_off;
        c->ninterfaces = (uint16_t)rd32(tl);
        c->interfaces = calloc(c->ninterfaces ? c->ninterfaces : 1, sizeof *c->interfaces);
        for (uint16_t i = 0; i < c->ninterfaces; i++) {
            c->interfaces[i] = vm_find_class(vm, dex_type(dex, rd16(tl + 4 + i * 2)));
            if (!c->interfaces[i]) {
                fprintf(stderr, "henge-vm: %s: missing interface %s\n", name, dex_type(dex, rd16(tl + 4 + i * 2)));
                free(c->interfaces);
                free(c);
                return NULL;
            }
        }
    }
    uint32_t sv_off = rd32(def + 28);
    c->static_values = sv_off ? dex->data + sv_off : NULL;

    uint32_t data_off = rd32(def + 24);
    uint32_t nstatic = 0, ninst = 0, ndirect = 0, nvirtual = 0;
    const uint8_t *cd = data_off ? dex->data + data_off : NULL;
    const uint8_t *p = cd;
    if (cd) {
        nstatic = dex_uleb(&p);
        ninst = dex_uleb(&p);
        ndirect = dex_uleb(&p);
        nvirtual = dex_uleb(&p);
    }

    /* fields: statics first (in the order static values are encoded), then instance fields */
    c->nfields = (uint16_t)(nstatic + ninst);
    c->ninstance = (uint16_t)ninst;
    c->fields = calloc(c->nfields ? c->nfields : 1, sizeof *c->fields);
    uint32_t inst_base = c->super ? c->super->inst_words : 0, inst_words = inst_base, static_words = 0;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < nstatic + ninst; i++) {
        if (i == nstatic) idx = 0;
        idx += dex_uleb(&p);
        uint32_t access = dex_uleb(&p);
        const uint8_t *fid = dex->field_ids + idx * 8;
        Field *f = &c->fields[i];
        f->cls = c;
        f->name = dex_string(dex, rd32(fid + 4));
        f->type_desc = dex_type(dex, rd16(fid + 2));
        f->type = f->type_desc[0];
        f->access = access;
        if (i < nstatic) {
            f->access |= ACC_STATIC;
            f->offset = static_words;
            static_words += (uint32_t)field_words(f->type);
        } else {
            f->offset = inst_words;
            inst_words += (uint32_t)field_words(f->type);
        }
    }
    c->inst_words = inst_words;
    c->static_words = static_words;
    c->statics = calloc(static_words ? static_words : 1, 4);
    uint16_t nref = c->super ? c->super->nref : 0;
    for (uint32_t i = nstatic; i < nstatic + ninst; i++)
        if (c->fields[i].type == 'L' || c->fields[i].type == '[') nref++;
    c->ref_offsets = calloc(nref ? nref : 1, sizeof *c->ref_offsets);
    c->nref = 0;
    if (c->super)
        for (uint16_t i = 0; i < c->super->nref; i++) c->ref_offsets[c->nref++] = c->super->ref_offsets[i];
    for (uint32_t i = nstatic; i < nstatic + ninst; i++)
        if (c->fields[i].type == 'L' || c->fields[i].type == '[') c->ref_offsets[c->nref++] = c->fields[i].offset;

    /* methods */
    c->nmethods = (uint16_t)(ndirect + nvirtual);
    c->methods = calloc(c->nmethods ? c->nmethods : 1, sizeof *c->methods);
    idx = 0;
    for (uint32_t i = 0; i < ndirect + nvirtual; i++) {
        if (i == ndirect) idx = 0;
        idx += dex_uleb(&p);
        uint32_t access = dex_uleb(&p);
        uint32_t code_off = dex_uleb(&p);
        const uint8_t *mid = dex->method_ids + idx * 8;
        Method *m = &c->methods[i];
        m->cls = c;
        m->dex = dex;
        m->name = dex_string(dex, rd32(mid + 4));
        m->sig = dex->msigs[idx] ? dex->msigs[idx] : (dex->msigs[idx] = build_sig(vm, dex, m->name, rd16(mid + 2)));
        m->shorty = dex_string(dex, rd32(dex->proto_ids + rd16(mid + 2) * 12));
        m->access = access;
        m->arg_words = vm_sig_arg_words(m->sig, (access & ACC_STATIC) != 0);
        if (code_off) {
            const uint8_t *code = dex->data + code_off;
            m->registers = rd16(code);
            m->ins = rd16(code + 2);
            m->outs = rd16(code + 4);
            m->tries = rd16(code + 6);
            m->insns_size = rd32(code + 12);
            m->insns = (const uint16_t *)(code + 16);
            if (m->tries)
                m->tries_data = code + 16 + (size_t)m->insns_size * 2 + ((m->insns_size & 1) ? 2 : 0);
        }
        if ((access & ACC_NATIVE) || (access & ACC_CONSTRUCTOR))
            m->native = find_native(vm, name, m->sig);
    }
    build_vtable(c);
    c->state = 1;

    struct class_entry *e = index_find(name, 1);
    e->cls = c;
    add_class(vm, c);
    return c;
}

/* ---- initialization ---- */

static void apply_static_values(Thread *t, Class *c)
{
    if (!c->static_values)
        return;
    const uint8_t *p = c->static_values;
    uint32_t n = dex_uleb(&p);
    uint32_t nstatic = (uint32_t)(c->nfields - c->ninstance);
    for (uint32_t i = 0; i < n && i < nstatic; i++) {
        Field *f = &c->fields[i];
        uint8_t hdr = *p++;
        uint32_t type = hdr & 0x1f, arg = hdr >> 5, size = arg + 1;
        uint64_t v = 0;
        if (type == 0x1e) { /* null */
            continue;
        } else if (type == 0x1f) { /* boolean */
            v = arg;
        } else {
            for (uint32_t k = 0; k < size; k++) v |= (uint64_t)p[k] << (8 * k);
            p += size;
            if (type == 0x00 || type == 0x02 || type == 0x04 || type == 0x06) { /* sign-extend */
                uint32_t bits = 8 * size;
                if (bits < 64 && (v >> (bits - 1)) & 1) v |= ~0ull << bits;
            } else if (type == 0x10) { /* float: zero-extended to the right */
                v <<= 8 * (4 - size);
            } else if (type == 0x11) {
                v <<= 8 * (8 - size);
            } else if (type == 0x17) { /* string */
                Dex *d = c->dex;
                if (!d->res_strings[v]) {
                    Ref s = vm_new_string_utf8(t, dex_string(d, (uint32_t)v));
                    d->res_strings[v] = vm_intern_string(t, s);
                }
                v = d->res_strings[v];
            } else {
                return; /* arrays, annotations, types: not expected as constants */
            }
        }
        if (field_words(f->type) == 2) {
            c->statics[f->offset] = (uint32_t)v;
            c->statics[f->offset + 1] = (uint32_t)(v >> 32);
        } else {
            c->statics[f->offset] = (uint32_t)v;
        }
    }
}

int vm_init_class(Thread *t, Class *c)
{
    if (c->state >= 3) {
        if (c->state == 4) {
            vm_throw(t, "Ljava/lang/NoClassDefFoundError;", c->name);
            return -1;
        }
        return 0;
    }
    if (c->state == 2)
        return 0; /* being initialized on this thread (the global lock keeps others out) */
    c->state = 2;
    if (c->super && vm_init_class(t, c->super) != 0) {
        c->state = 4;
        return -1;
    }
    apply_static_values(t, c);
    Method *clinit = find_declared(c, vm_intern(t->vm, "<clinit>()V"));
    if (clinit) {
        vm_invoke(t, clinit, NULL);
        if (t->exception) {
            c->state = 4;
            return -1;
        }
    }
    c->state = 3;
    return 0;
}

Ref vm_class_object(Thread *t, Class *c)
{
    if (c->class_obj)
        return c->class_obj;
    Ref r = vm_new_object(t, t->vm->cls_class);
    Object *o = vm_obj(t->vm, r);
    uint64_t p = (uint64_t)(uintptr_t)c; /* Class.handle is the first (long) field */
    o->u.words[0] = (uint32_t)p;
    o->u.words[1] = (uint32_t)(p >> 32);
    c->class_obj = r;
    return r;
}

/* ---- primitive classes (int.class, void.class, ...) ---- */

static Class *prims[128];

Class *vm_primitive_class(VM *vm, char desc)
{
    if (prims[(int)desc])
        return prims[(int)desc];
    Class *c = calloc(1, sizeof *c);
    char name[2] = {desc, 0};
    c->name = vm_intern(vm, name);
    c->access = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
    c->state = 3;
    prims[(int)desc] = c;
    add_class(vm, c);
    return c;
}

/* ---- exceptions ---- */

void vm_throw(Thread *t, const char *desc, const char *msg)
{
    VM *vm = t->vm;
    Class *c = vm_find_class(vm, desc);
    if (!c) {
        fprintf(stderr, "henge-vm: missing exception class %s (%s)\n", desc, msg ? msg : "");
        c = vm_find_class(vm, "Ljava/lang/RuntimeException;");
        if (!c)
            abort();
    }
    if (vm_init_class(t, c) != 0)
        return;
    Ref o = vm_new_object(t, c);
    vm_pin(vm, o); /* survives the constructor call below */
    Method *ctor = vm_find_method(c, "<init>", "(Ljava/lang/String;)V");
    if (ctor && msg) {
        uint32_t args[2] = {o, vm_new_string_utf8(t, msg)};
        vm_invoke(t, ctor, args);
    } else if ((ctor = vm_find_method(c, "<init>", "()V"))) {
        uint32_t args[1] = {o};
        vm_invoke(t, ctor, args);
    }
    vm->npins--; /* unpin */
    if (!t->exception) /* else the constructor's own exception wins */
        t->exception = o;
}

/* ---- VM ---- */

Thread *vm_main_thread(VM *vm) { return vm->threads; }

static Thread *new_thread(VM *vm)
{
    Thread *t = calloc(1, sizeof *t);
    t->vm = vm;
    t->stack = malloc((size_t)VM_STACK_WORDS * 4);
    t->sp = t->stack;
    t->stack_end = t->stack + VM_STACK_WORDS;
    t->pthread = pthread_self();
    t->next = vm->threads;
    vm->threads = t;
    return t;
}

VM *vm_create(void)
{
    VM *vm = calloc(1, sizeof *vm);
    if (!vm)
        return NULL;
    g_vm = vm;
    pthread_mutex_init(&vm->lock, NULL);
    vm->gc_threshold = 16u << 20;
    vm->sig_to_string = vm_intern(vm, "toString()Ljava/lang/String;");
    new_thread(vm);
    vm_register_core_natives(vm);
    vm_register_android_natives(vm);
    size_t size;
    const uint8_t *core = vm_core_dex(&size);
    char err[256];
    if (vm_add_dex(vm, core, size, err, sizeof err) != 0) {
        fprintf(stderr, "henge-vm: core dex: %s\n", err);
        vm_destroy(vm);
        return NULL;
    }
    size_t asize;
    const uint8_t *adex = vm_android_dex(&asize);
    if (adex && vm_add_dex(vm, adex, asize, err, sizeof err) != 0) {
        fprintf(stderr, "henge-vm: android dex: %s\n", err);
        vm_destroy(vm);
        return NULL;
    }
    vm->cls_object = vm_find_class(vm, "Ljava/lang/Object;");
    vm->cls_string = vm_find_class(vm, "Ljava/lang/String;");
    vm->cls_class = vm_find_class(vm, "Ljava/lang/Class;");
    vm->cls_throwable = vm_find_class(vm, "Ljava/lang/Throwable;");
    if (!vm->cls_object || !vm->cls_string || !vm->cls_class) {
        fprintf(stderr, "henge-vm: core classes are missing\n");
        vm_destroy(vm);
        return NULL;
    }
    return vm;
}

uint64_t vm_call_static(Thread *t, const char *cls, const char *name, const char *proto, uint32_t *args)
{
    Class *c = vm_find_class(t->vm, cls);
    if (!c) {
        vm_throw(t, "Ljava/lang/NoClassDefFoundError;", cls);
        return 0;
    }
    if (vm_init_class(t, c) != 0)
        return 0;
    Method *m = vm_find_method(c, name, proto);
    if (!m) {
        vm_throw(t, "Ljava/lang/NoSuchMethodError;", name);
        return 0;
    }
    return vm_invoke(t, m, args);
}

/* Release everything owned by the VM. DEX input buffers remain owned by the caller. */
void vm_destroy(VM *vm)
{
    if (!vm)
        return;
    for (uint32_t r = 1; r < vm->table_size; r++)
        if (((uintptr_t)vm->table[r] & 1) == 0)
            free(vm->table[r]);
    free(vm->table);
    for (Thread *t = vm->threads; t;) {
        Thread *n = t->next;
        free(t->stack);
        free(t);
        t = n;
    }
    for (size_t i = 0; i < vm->nclasses; i++) {
        Class *c = vm->classes[i];
        /* Array classes borrow Object's vtable. Ordinary classes own theirs. */
        if (!c->array_kind)
            free(c->vtable);
        free(c->interfaces);
        free(c->fields);
        free(c->methods);
        free(c->statics);
        free(c->ref_offsets);
        free(c);
    }
    free(vm->classes);
    for (size_t i = 0; i < vm->ndex; i++) {
        dex_close(vm->dexes[i]);
        free(vm->dexes[i]);
    }
    free(vm->dexes);
    for (size_t i = 0; i < vm->cap_interned; i++)
        free(vm->interned[i]);
    free(vm->interned);
    struct natives *nt = vm->natives;
    if (nt) {
        for (size_t i = 0; i < nt->n; i++)
            free(nt->e[i].key);
        free(nt->e);
        free(nt);
    }
    free(vm->pins);
    free(vm->string_pool);
    free(g_index.e);
    memset(&g_index, 0, sizeof g_index);
    memset(g_mcache, 0, sizeof g_mcache);
    memset(prims, 0, sizeof prims);
    g_vm = NULL;
    free(vm->string_index);
    pthread_mutex_destroy(&vm->lock);
    free(vm);
}
