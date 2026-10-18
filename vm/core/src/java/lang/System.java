package java.lang;

import java.lang.String;
import java.io.PrintStream;

public final class System {
    public static final PrintStream out = new PrintStream(1);
    public static final PrintStream err = new PrintStream(2);
    private System() {}
    public static native void arraycopy(java.lang.Object src, int srcPos, java.lang.Object dst, int dstPos, int length);
    public static native long currentTimeMillis();
    public static native long nanoTime();
    public static native int identityHashCode(java.lang.Object o);
    public static native void exit(int status);
    public static String getProperty(String key) { return null; }
    public static String getProperty(String key, String def) { return def; }
    public static String lineSeparator() { return "\n"; }
    public static String getenv(String key) { return null; }
    public static void gc() {}
}
