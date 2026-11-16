#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static JavaVM *java_vm;
static jclass global_class;
static int thread_ok;
static jint registered(JNIEnv *env, jclass c, jint n) { (void)env; (void)c; return n * 3; }
static void *worker(void *unused)
{
    (void)unused;
    JNIEnv *env = NULL;
    if ((*java_vm)->GetEnv(java_vm, (void **)&env, JNI_VERSION_1_6) != JNI_EDETACHED) return NULL;
    if ((*java_vm)->AttachCurrentThread(java_vm, &env, NULL) != JNI_OK) return NULL;
    jmethodID callback = (*env)->GetStaticMethodID(env, global_class, "callback", "(I)I");
    if (callback && (*env)->CallStaticIntMethod(env, global_class, callback, 37) == 42) thread_ok = 1;
    if ((*java_vm)->DetachCurrentThread(java_vm) != JNI_OK) thread_ok = 0;
    return NULL;
}
static double via_v(JNIEnv *env, jclass c, jmethodID id, ...)
{ va_list args; va_start(args, id); double result = (*env)->CallStaticDoubleMethodV(env, c, id, args); va_end(args); return result; }

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    java_vm = vm;
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass c = (*env)->FindClass(env, "JniTest");
    if (!c) return JNI_ERR;
    JNINativeMethod methods[] = {{"registered", "(I)I", (void *)registered}};
    return (*env)->RegisterNatives(env, c, methods, 1) == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
JNIEXPORT jint JNICALL Java_JniTest_nativeTest(JNIEnv *env, jclass clazz)
{
    CHECK((*env)->GetVersion(env) == JNI_VERSION_1_6);
    CHECK((*env)->PushLocalFrame(env, 32) == JNI_OK);
    jmethodID constructor = (*env)->GetMethodID(env, clazz, "<init>", "(I)V");
    CHECK(constructor);
    jobject instance = (*env)->NewObject(env, clazz, constructor, 35);
    CHECK(instance);
    jfieldID field = (*env)->GetFieldID(env, clazz, "value", "I");
    CHECK(field && (*env)->GetIntField(env, instance, field) == 35);
    jfieldID flag = (*env)->GetFieldID(env, clazz, "flag", "Z");
    jfieldID small = (*env)->GetFieldID(env, clazz, "small", "B");
    CHECK(flag && small);
    ((void (*)(JNIEnv *, jobject, jfieldID, uint64_t))(*env)->SetBooleanField)(env, instance, flag, UINT64_C(0xdeadbeef01010101));
    ((void (*)(JNIEnv *, jobject, jfieldID, uint64_t))(*env)->SetByteField)(env, instance, small, UINT64_C(0xdeadbeef010101fe));
    CHECK((*env)->GetBooleanField(env, instance, flag) == 1 && (*env)->GetByteField(env, instance, small) == -2);
    (*env)->SetIntField(env, instance, field, 40);
    jmethodID plus = (*env)->GetMethodID(env, clazz, "plus", "(I)I");
    CHECK(plus && (*env)->CallIntMethod(env, instance, plus, 2) == 42);
    jvalue one[] = {{.i = 2}};
    CHECK((*env)->CallNonvirtualIntMethodA(env, instance, clazz, plus, one) == 42);
    jfieldID wide = (*env)->GetStaticFieldID(env, clazz, "wide", "J");
    CHECK(wide);
    (*env)->SetStaticLongField(env, clazz, wide, INT64_C(0x1234567887654321));
    CHECK((*env)->GetStaticLongField(env, clazz, wide) == INT64_C(0x1234567887654321));
    const jchar utf16[] = {65, 0, 0xd83d, 0xde00};
    jstring string = (*env)->NewString(env, utf16, 4);
    CHECK(string && (*env)->GetStringLength(env, string) == 4 && (*env)->GetStringUTFLength(env, string) == 9);
    const char *utf = (*env)->GetStringUTFChars(env, string, NULL);
    CHECK(utf && !memcmp(utf, "A\xc0\x80\xed\xa0\xbd\xed\xb8\x80", 10));
    jstring copy = (*env)->NewStringUTF(env, utf);
    (*env)->ReleaseStringUTFChars(env, string, utf);
    CHECK((*env)->GetStringLength(env, copy) == 4);
    jintArray array = (*env)->NewIntArray(env, 3);
    jint input[] = {11, 22, 33}, output[3] = {0};
    (*env)->SetIntArrayRegion(env, array, 0, 3, input);
    (*env)->GetIntArrayRegion(env, array, 0, 3, output);
    CHECK(!memcmp(input, output, sizeof input));
    jint *elements = (*env)->GetIntArrayElements(env, array, NULL);
    CHECK(elements); elements[1] = 42;
    (*env)->ReleaseIntArrayElements(env, array, elements, 0);
    (*env)->GetIntArrayRegion(env, array, 1, 1, output);
    CHECK(output[0] == 42);
    (*env)->GetIntArrayRegion(env, array, -1, 1, output);
    CHECK((*env)->ExceptionCheck(env)); (*env)->ExceptionClear(env);
    jmethodID many = (*env)->GetStaticMethodID(env, clazz, "many", "(IDIDIDIDIDIDIDIDIDID)D");
    CHECK(many);
    CHECK((*env)->CallStaticDoubleMethod(env, clazz, many, 1,1.5,2,2.5,3,3.5,4,4.5,5,5.5,6,6.5,7,7.5,8,8.5,9,9.5,10,10.5) == 115.0);
    CHECK(via_v(env, clazz, many, 1,1.5,2,2.5,3,3.5,4,4.5,5,5.5,6,6.5,7,7.5,8,8.5,9,9.5,10,10.5) == 115.0);
    jobject global = (*env)->NewGlobalRef(env, instance);
    CHECK((*env)->GetObjectRefType(env, global) == JNIGlobalRefType);
    instance = (*env)->PopLocalFrame(env, instance);
    CHECK((*env)->IsSameObject(env, global, instance));
    jmethodID collect = (*env)->GetStaticMethodID(env, clazz, "collect", "()V");
    CHECK(collect); (*env)->CallStaticVoidMethod(env, clazz, collect);
    CHECK((*env)->GetIntField(env, instance, field) == 40);
    (*env)->DeleteGlobalRef(env, global);
    global_class = (*env)->NewGlobalRef(env, clazz);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
    pthread_join(thread, NULL);
    (*env)->DeleteGlobalRef(env, global_class);
    CHECK(thread_ok);
    CHECK(!(*env)->ExceptionCheck(env));
    return 0;
}
JNIEXPORT jdouble JNICALL Java_JniTest_mix(JNIEnv *env, jclass c,
    jint a,jdouble x,jint b,jdouble y,jint d,jdouble z,jint e,jdouble u,jint f,jdouble v,
    jint g,jdouble w,jint h,jdouble q,jint i,jdouble r,jint j,jdouble s,jint k,jdouble t)
{ (void)env; (void)c; return a+b+d+e+f+g+h+i+j+k+x+y+z+u+v+w+q+r+s+t; }
JNIEXPORT jobject JNICALL Java_JniTest_identity(JNIEnv *env, jclass c, jobject value)
{ (void)env; (void)c; return value; }

JNIEXPORT jint JNICALL Java_dev_henge_jnitest_JniActivity_runNative(JNIEnv *env, jclass activity)
{
    (void)activity;
    jclass test = (*env)->FindClass(env, "JniTest");
    jmethodID run = test ? (*env)->GetStaticMethodID(env, test, "run", "()I") : NULL;
    return run ? (*env)->CallStaticIntMethod(env, test, run) : -1;
}
