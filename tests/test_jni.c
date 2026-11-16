#include "vm/vm.h"
#include "henge/henge.h"
#include "henge/jni.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    henge_set_library_path(argv[1]);
    FILE *file = fopen(argv[2], "rb"); assert(file);
    fseek(file, 0, SEEK_END); long size = ftell(file); rewind(file);
    uint8_t *data = malloc((size_t)size); assert(data);
    assert(fread(data, 1, (size_t)size, file) == (size_t)size); fclose(file);
    VM *vm = vm_create(); assert(vm);
    char err[512]; assert(vm_add_dex(vm, data, (size_t)size, err, sizeof err) == 0);
    Thread *t = vm_main_thread(vm);
    JNIEnv *env = vm_jni_env(t);
    assert((*env)->GetVersion(env) == JNI_VERSION_1_6);
    uint64_t result = vm_call_static(t, "LJniTest;", "run", "()I", NULL);
    if (t->exception) { (*env)->ExceptionDescribe(env); puts("FAIL: JNI exception"); return 1; }
    if (result) { printf("FAIL: Java JNI test returned %llu\n", (unsigned long long)result); return 1; }
    /* Strong globals survive; weak globals are cleared before handles can be recycled. */
    assert((*env)->PushLocalFrame(env, 8) == JNI_OK);
    jstring str = (*env)->NewStringUTF(env, "weak-only");
    jweak weak = (*env)->NewWeakGlobalRef(env, str);
    (*env)->PopLocalFrame(env, NULL);
    vm_gc(t);
    assert((*env)->IsSameObject(env, weak, NULL));
    (*env)->DeleteWeakGlobalRef(env, weak);
    Method *method = vm_find_method(vm_find_class(vm, "LJniTest;"), "registered", "(I)I");
    void *previous = method->jni_native;
    assert(vm_jni_load(t, "jnifail") == -1 && t->exception);
    (*env)->ExceptionClear(env);
    assert(method->jni_native == previous); /* failed JNI_OnLoad rolls registration back */
    assert(vm_jni_load(t, "jnifail") == -1 && t->exception);
    (*env)->ExceptionClear(env);
    vm_destroy(vm); free(data);
    puts("PASS: test_jni (JNI_OnLoad, registration, fields, strings, arrays, A/V/varargs, native calls, threads, GC roots)");
    return 0;
}
