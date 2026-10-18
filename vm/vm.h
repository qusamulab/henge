/* henge-vm: a small Dalvik bytecode interpreter, enough to run simple Kotlin / Java apps whose
 * java.* and android.* classes are provided by henge itself (vm/core). Not ART: no JIT, no AOT, no
 * full class library, one global lock.
 *
 * Object references are 32-bit handles into a table (dex registers are 32 bits wide). */
#ifndef HENGE_VM_H
#define HENGE_VM_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t Ref; /* 0 = null */

typedef struct Dex Dex;
typedef struct Class Class;
typedef struct Method Method;
typedef struct Field Field;
typedef struct Object Object;
typedef struct Thread Thread;
typedef struct VM VM;

/* ---- DEX file ---- */

struct Dex {
    const uint8_t *data;
    size_t size;
    uint32_t nstrings, ntypes, nprotos, nfields, nmethods, nclasses;
    const uint8_t *string_ids, *type_ids, *proto_ids, *field_ids, *method_ids, *class_defs;
    const char **strings; /* decoded lazily: MUTF-8 data pointers */
    Class **classes_by_type; /* per type_idx cache of resolved classes */
    Method **res_methods;    /* per method_idx: the declared method it names */
    const char **msigs;      /* per method_idx: interned signature */
    Field **res_fields;      /* per field_idx */
    Ref *res_strings;        /* per string_idx: interned java.lang.String, once created */
};

int dex_open(Dex *dex, const uint8_t *data, size_t size, char *err, size_t errlen);
const char *dex_string(Dex *dex, uint32_t idx); /* MUTF-8 */
const char *dex_type(Dex *dex, uint32_t type_idx);
uint32_t dex_uleb(const uint8_t **p);
int32_t dex_sleb(const uint8_t **p);

/* ---- classes ---- */

enum {
    ACC_PUBLIC = 0x1, ACC_PRIVATE = 0x2, ACC_PROTECTED = 0x4, ACC_STATIC = 0x8, ACC_FINAL = 0x10,
    ACC_NATIVE = 0x100, ACC_INTERFACE = 0x200, ACC_ABSTRACT = 0x400,
    ACC_CONSTRUCTOR = 0x10000,
};

struct Field {
    Class *cls;
    const char *name;
    char type;        /* descriptor first char: Z B S C I J F D L [ */
    const char *type_desc;
    uint32_t access;
    uint32_t offset;  /* in 32-bit words, within the instance or the statics block */
};

typedef uint64_t (*NativeFn)(Thread *t, uint32_t *args);

struct Method {
    Class *cls;
    const char *name;
    const char *sig;    /* interned "name(params)ret": identity comparable */
    const char *shorty;
    uint32_t access;
    uint16_t registers, ins, outs, tries;
    const uint16_t *insns;
    uint32_t insns_size; /* in 16-bit units */
    const uint8_t *tries_data;   /* try_items followed by encoded_catch_handler_list */
    NativeFn native;
    Dex *dex;
    uint16_t arg_words;  /* receiver + parameter words */
};

struct Class {
    const char *name;     /* descriptor, e.g. "Lfoo/Bar;" */
    Dex *dex;
    const uint8_t *class_def;
    Class *super;
    Class **interfaces;
    uint16_t ninterfaces;
    uint32_t access;
    int state;            /* 0 loaded, 1 linked, 2 initializing, 3 initialized, 4 error */
    pthread_t init_thread;
    Field *fields;        /* instance fields declared here, then static ones */
    uint16_t nfields, ninstance;
    uint32_t inst_words;  /* instance size in words, including inherited */
    uint32_t *ref_offsets; /* word offsets of reference fields, including inherited */
    uint16_t nref;
    uint32_t *statics;    /* static storage */
    uint32_t static_words;
    Method *methods;      /* declared methods */
    uint16_t nmethods;
    Method **vtable;      /* all callable methods by signature (declared, inherited, defaults) */
    uint16_t nvtable;
    char array_kind;      /* for array classes: element descriptor first char; 0 otherwise */
    Class *component;     /* array element class (or NULL for primitives) */
    Ref class_obj;        /* java.lang.Class mirror */
    const uint8_t *static_values; /* encoded_array of initial static values, or NULL */
};

/* ---- objects ---- */

enum { OBJ_INSTANCE = 0, OBJ_ARRAY = 1, OBJ_STRING = 2 };

struct Object {
    Class *cls;
    uint32_t kind : 4;
    uint32_t mark : 1;
    uint32_t length;     /* arrays: element count; strings: UTF-16 code units */
    uint32_t hash;       /* strings: cached hash; others: identity hash once assigned */
    union {
        uint32_t words[1];  /* instance fields */
        uint8_t bytes[1];   /* array elements (size by array_kind) */
        uint16_t chars[1];  /* string data */
    } u;
};

/* ---- VM ---- */

#define VM_STACK_WORDS (1u << 20)

struct Thread {
    VM *vm;
    uint32_t *stack, *sp;
    uint32_t *stack_end;
    Ref exception;
    pthread_t pthread;
    Ref thread_obj;
    int depth;
    Thread *next;
};

struct VM {
    Class *cls_object, *cls_string, *cls_class, *cls_throwable;
    pthread_mutex_t lock;     /* the global interpreter lock */
    Dex **dexes;
    size_t ndex;
    Object **table;           /* handle table */
    uint32_t table_size, table_cap, free_head;
    uint32_t live_bytes, gc_threshold;
    int gc_wanted;
    Class **classes;          /* loaded classes */
    size_t nclasses, cap_classes;
    Thread *threads;
    Ref *pins; size_t npins, cap_pins; /* native roots */
    const char *(*intern)(VM *vm, const char *s);
    char **interned; size_t ninterned, cap_interned;
    Ref *string_pool; size_t nstring_pool, cap_string_pool; /* interned String objects */
    void *natives;            /* registered native methods */
    char errbuf[512];
};

/* ---- API ---- */

VM *vm_create(void);
int vm_add_dex(VM *vm, const uint8_t *data, size_t size, char *err, size_t errlen); /* takes a borrowed buffer */
Thread *vm_main_thread(VM *vm);

Class *vm_find_class(VM *vm, const char *descriptor); /* NULL if missing; applies the java.* remap */
Method *vm_find_method(Class *cls, const char *name, const char *proto); /* proto "(II)V"; searches supers */
int vm_init_class(Thread *t, Class *cls);
uint64_t vm_invoke(Thread *t, Method *m, uint32_t *args); /* args: receiver + params as words */
uint64_t vm_call_static(Thread *t, const char *cls, const char *name, const char *proto, uint32_t *args);

Ref vm_new_object(Thread *t, Class *cls);
Ref vm_new_array(Thread *t, Class *array_cls, uint32_t length);
Ref vm_new_string_utf8(Thread *t, const char *s);
Ref vm_new_string_utf16(Thread *t, const uint16_t *chars, uint32_t n);
Object *vm_obj(VM *vm, Ref r);
char *vm_string_utf8(VM *vm, Ref r); /* malloc'd */
Ref vm_intern_string(Thread *t, Ref s);
void vm_string_set(VM *vm, Ref r, const uint16_t *chars, uint32_t n); /* String constructors: replace a placeholder's contents */
Class *vm_array_class(VM *vm, const char *descriptor); /* "[I", "[Ljava/lang/String;" */

void vm_throw(Thread *t, const char *exception_class, const char *message);
void vm_pin(VM *vm, Ref r);
void vm_gc(Thread *t);

const char *vm_intern(VM *vm, const char *s);
Method *vm_lookup_virtual(Class *cls, const char *sig);
int vm_instance_of(Class *obj_cls, Class *target);
Class *vm_resolve_type(Thread *t, Dex *dex, uint32_t type_idx);
uint16_t vm_sig_arg_words(const char *sig, int is_static);
Ref vm_class_object(Thread *t, Class *cls);
Class *vm_primitive_class(VM *vm, char desc); /* "int" -> 'I' ... */

Method *vm_resolve_method(Thread *t, Dex *dex, uint32_t method_idx);
const char *vm_method_sig(Thread *t, Dex *dex, uint32_t method_idx);
Field *vm_resolve_field(Thread *t, Dex *dex, uint32_t field_idx);

uint32_t vm_field_offset(Class *cls, const char *name); /* instance field word offset, or UINT32_MAX */
extern void (*vm_android_log)(int prio, const char *tag, const char *msg);
extern void (*vm_asset_reader)(const char *name, uint8_t **data, size_t *size); /* assets/<name> of the running APK */
void vm_register_android_natives(VM *vm);

const uint8_t *vm_android_dex(size_t *size); /* henge-android.dex, embedded */
void vm_destroy(VM *vm);

/* ---- host: runs an APK's launcher Activity (Kotlin / Java) on the VM ---- */

typedef struct vm_host vm_host;

struct vm_host_opts {
    const char *apk_path;
    void *window;        /* struct ANativeWindow* (runtime/android.h) backing the GLSurfaceView */
    int width, height;   /* window size in pixels */
    void (*log)(int prio, const char *tag, const char *msg, void *user); /* android.util.Log and uncaught exceptions */
    void *user;
};

/* Starts the activity on its own thread and returns once onCreate and the first surface callbacks have run
 * (NULL with a message in err on failure). */
vm_host *vm_host_start(const struct vm_host_opts *opts, char *err, size_t errlen);
void vm_host_touch(vm_host *h, int phase, int id, float x, float y); /* phase: 0 down 1 move 2 up 3 cancel */
void vm_host_stop(vm_host *h); /* onPause / onStop / onDestroy, joins the thread */
int vm_host_running(vm_host *h);

/* natives */
void vm_register_native(VM *vm, const char *cls, const char *name, const char *proto, NativeFn fn);
void vm_register_core_natives(VM *vm);
const uint8_t *vm_core_dex(size_t *size); /* henge-core.dex, embedded */

#define VM_ARG_I(args, i) ((int32_t)(args)[i])
static inline uint64_t vm_wide(const uint32_t *a) { return (uint64_t)a[0] | ((uint64_t)a[1] << 32); }

#endif
