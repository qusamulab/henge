package android.util;

public final class Log {
    public static final int VERBOSE = 2, DEBUG = 3, INFO = 4, WARN = 5, ERROR = 6;
    private static native void write(int prio, String tag, String msg);
    public static int v(String tag, String msg) { write(VERBOSE, tag, msg); return 0; }
    public static int d(String tag, String msg) { write(DEBUG, tag, msg); return 0; }
    public static int i(String tag, String msg) { write(INFO, tag, msg); return 0; }
    public static int w(String tag, String msg) { write(WARN, tag, msg); return 0; }
    public static int e(String tag, String msg) { write(ERROR, tag, msg); return 0; }
    public static int e(String tag, String msg, Throwable t) { write(ERROR, tag, msg + ": " + t); return 0; }
    public static int w(String tag, Throwable t) { write(WARN, tag, String.valueOf(t)); return 0; }
    public static String getStackTraceString(Throwable t) { return String.valueOf(t); }
}
