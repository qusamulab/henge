package android.os;

import java.util.ArrayList;

/** One main looper: the host runs pending callbacks once per frame (Handler.runPending). */
public class Handler {
    private static final class Entry { Runnable r; long at; Handler h; }
    private static final ArrayList<Entry> queue = new ArrayList<Entry>();

    public interface Callback { boolean handleMessage(Message m); }

    public Handler() {}
    public Handler(Looper looper) {}
    public Handler(Looper looper, Callback cb) {}

    public final boolean post(Runnable r) { return postDelayed(r, 0); }

    public final boolean postDelayed(Runnable r, long delay) {
        Entry e = new Entry();
        e.r = r;
        e.at = SystemClock.uptimeMillis() + delay;
        e.h = this;
        queue.add(e);
        return true;
    }

    public final void removeCallbacks(Runnable r) {
        for (int i = queue.size() - 1; i >= 0; i--) if (queue.get(i).r == r) queue.remove(i);
    }

    public final void removeCallbacksAndMessages(Object token) { queue.clear(); }

    public void handleMessage(Message m) {}

    /** Called by the host on the main thread. */
    public static void runPending() {
        long now = SystemClock.uptimeMillis();
        for (int i = 0; i < queue.size(); ) {
            Entry e = queue.get(i);
            if (e.at <= now) { queue.remove(i); e.r.run(); } else { i++; }
        }
    }
}
