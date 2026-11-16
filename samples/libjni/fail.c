#include <jni.h>
static jint bad(JNIEnv *env, jclass c, jint n) { (void)env; (void)c; (void)n; return -999; }
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass c = (*env)->FindClass(env, "JniTest");
    if (!c) return JNI_ERR;
    JNINativeMethod methods[] = {{"registered", "(I)I", (void *)bad}};
    (*env)->RegisterNatives(env, c, methods, 1);
    return JNI_ERR;
}
