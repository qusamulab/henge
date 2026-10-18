/* henge-vm test: runs VmTest (vm/tests/src) on the bytecode interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/vm.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*size);
    if (fread(b, 1, *size, f) != *size) { free(b); fclose(f); return NULL; }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s vmtest.dex\n", argv[0]);
        return 2;
    }
    size_t size;
    uint8_t *dex = read_file(argv[1], &size);
    if (!dex) { perror(argv[1]); return 2; }
    VM *vm = vm_create();
    if (!vm) { printf("FAIL  vm_create\n"); return 1; }
    char err[256];
    if (vm_add_dex(vm, dex, size, err, sizeof err) != 0) { printf("FAIL  vm_add_dex: %s\n", err); return 1; }
    Thread *t = vm_main_thread(vm);
    uint64_t failures = vm_call_static(t, "LVmTest;", "run", "()I", NULL);
    if (t->exception) {
        Object *e = vm_obj(vm, t->exception);
        printf("FAIL  uncaught exception %s\n", e ? e->cls->name : "?");
        return 1;
    }
    printf("%s: test_vm (%d failure%s)\n", failures ? "FAIL" : "PASS", (int)failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
