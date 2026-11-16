package dev.henge.jnitest;

public class JniActivity extends android.app.Activity {
    private static native int runNative();
    protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        System.loadLibrary("jnitest");
        int result = runNative();
        if (result != 0) throw new IllegalStateException("JNI test failed: " + result);
        android.util.Log.i("JniActivity", "PASS: Java Activity through JNI");
        finish();
    }
}
