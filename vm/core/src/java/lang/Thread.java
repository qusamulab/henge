package java.lang;

/** One interpreter thread: start() runs the body to completion on the caller (no preemption). */
public class Thread implements Runnable {
    private static final Thread MAIN = new Thread("main");
    private Runnable target;
    private String name;
    private boolean started, daemon;

    public Thread() { this.name = "Thread"; }
    public Thread(Runnable r) { target = r; name = "Thread"; }
    public Thread(Runnable r, String name) { target = r; this.name = name; }
    public Thread(String name) { this.name = name; }

    public static Thread currentThread() { return MAIN; }
    public static native void sleep(long millis) throws InterruptedException;
    public static void yield() {}
    public static boolean interrupted() { return false; }

    public void run() { if (target != null) target.run(); }
    public void start() {
        if (started) throw new IllegalStateException("already started");
        started = true;
        run();
    }
    public final void join() throws InterruptedException {}
    public final void join(long ms) throws InterruptedException {}
    public void interrupt() {}
    public boolean isInterrupted() { return false; }
    public final boolean isAlive() { return false; }
    public final void setDaemon(boolean d) { daemon = d; }
    public final boolean isDaemon() { return daemon; }
    public final String getName() { return name; }
    public final void setName(String n) { name = n; }
    public final void setPriority(int p) {}
    public long getId() { return 1; }
    public StackTraceElement[] getStackTrace() { return new StackTraceElement[0]; }
}
