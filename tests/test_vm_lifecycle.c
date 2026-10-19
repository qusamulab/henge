/* Repeated VM lifetimes, GC accounting and cleanup after failed APK startup. */
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void exercise_vm(void)
{
    VM *vm = vm_create();
    assert(vm);
    Thread *t = vm_main_thread(vm);

    /* String constructors replace a placeholder without retaining the old allocation. */
    Ref s = vm_new_object(t, vm->cls_string);
    uint16_t chars[] = {'h', 'e', 'n', 'g', 'e'};
    vm_string_set(vm, s, chars, 5);
    size_t bytes = vm->live_bytes;
    for (int i = 0; i < 100; i++)
        vm_string_set(vm, s, chars, 5);
    assert(vm->live_bytes == bytes);
    vm_pin(vm, s);
    vm_gc(t);
    assert(vm->live_bytes == bytes);
    assert(vm_obj(vm, s)->length == 5);

    /* Arrays borrow Object's vtable; destruction must not free it twice. */
    Ref dead = vm_new_array(t, vm_array_class(vm, "[I"), 128);
    assert(dead && vm->live_bytes > bytes);
    vm_gc(t);
    assert(vm_obj(vm, dead) == NULL && vm->live_bytes == bytes);
    assert(vm_primitive_class(vm, 'I'));
    assert(vm_array_class(vm, "[Ljava/lang/String;"));
    assert(vm_find_method(vm->cls_string, "length", "()I"));

    /* Exercise intern-index growth, duplicate lookup and GC roots. */
    for (int i = 0; i < 2048; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "intern-%d", i);
        Ref first = vm_intern_string(t, vm_new_string_utf8(t, buf));
        Ref second = vm_intern_string(t, vm_new_string_utf8(t, buf));
        assert(first == second);
    }
    vm_gc(t);
    Ref first = vm_intern_string(t, vm_new_string_utf8(t, "intern-1024"));
    assert(vm_obj(vm, first)->length == strlen("intern-1024"));
    assert(!t->exception);
    vm_destroy(vm);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    for (int i = 0; i < 3; i++) {
        exercise_vm();
        struct vm_host_opts opts = {.apk_path = argv[1]};
        char err[256] = "";
        /* The NativeActivity fixture has a launcher but no DEX. */
        assert(vm_host_start(&opts, err, sizeof err) == NULL);
        assert(strstr(err, "no classes.dex"));
        assert(vm_android_log == NULL && vm_asset_reader == NULL);
    }
    vm_destroy(NULL);
    puts("PASS: test_vm_lifecycle (3 VM restarts and failed host starts)");
    return 0;
}
