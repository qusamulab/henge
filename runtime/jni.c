/* Stand-in JavaVM / JNIEnv for NativeActivity code that touches JNI (activity->vm, activity->env).
 * There is no Java: every JNI function returns 0 / NULL, GetVersion reports 1.6, and
 * AttachCurrentThread / GetEnv hand out the one environment. Code that needs real Java objects
 * (most Java-based games) still fails, but code that only probes JNI keeps running. */
#include "android.h"
#include "shim.h"

#include <stdint.h>

#define JNI_FUNCTIONS 235 /* slots in JNINativeInterface, including the 4 reserved ones */
#define JNI_VERSION_1_6 0x00010006

static intptr_t jni_stub(void)
{
    return 0;
}

static int32_t jni_get_version(void *env)
{
    (void)env;
    return JNI_VERSION_1_6;
}

static const void *g_functions[JNI_FUNCTIONS];
static const void *const *g_env_ptr;
static const void *g_invoke[8];
static const void *const *g_vm_ptr;

void *hr_jni_env = &g_env_ptr;
void *hr_jni_vm = &g_vm_ptr;

static int32_t vm_get_env(void *vm, void **penv, int32_t version)
{
    (void)vm;
    (void)version;
    *penv = hr_jni_env;
    return 0;
}

static int32_t vm_attach(void *vm, void **penv, void *args)
{
    (void)args;
    return vm_get_env(vm, penv, JNI_VERSION_1_6);
}

static int32_t vm_detach(void *vm)
{
    (void)vm;
    return 0;
}

__attribute__((constructor)) static void jni_init(void)
{
    for (int i = 0; i < JNI_FUNCTIONS; i++)
        g_functions[i] = (const void *)jni_stub;
    g_functions[0] = g_functions[1] = g_functions[2] = g_functions[3] = 0;
    g_functions[4] = (const void *)jni_get_version;
    g_env_ptr = g_functions;
    /* JNIInvokeInterface: 3 reserved, DestroyJavaVM, AttachCurrentThread, DetachCurrentThread, GetEnv,
     * AttachCurrentThreadAsDaemon */
    g_invoke[3] = (const void *)jni_stub;
    g_invoke[4] = (const void *)vm_attach;
    g_invoke[5] = (const void *)vm_detach;
    g_invoke[6] = (const void *)vm_get_env;
    g_invoke[7] = (const void *)vm_attach;
    g_vm_ptr = g_invoke;
}
