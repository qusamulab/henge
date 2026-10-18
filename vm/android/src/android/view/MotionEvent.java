package android.view;

public final class MotionEvent {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3,
        ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6, ACTION_MASK = 0xff, ACTION_POINTER_INDEX_MASK = 0xff00,
        ACTION_POINTER_INDEX_SHIFT = 8;

    public int action;
    public int count;
    public int[] ids = new int[10];
    public float[] xs = new float[10];
    public float[] ys = new float[10];
    public long eventTime;

    public int getAction() { return action; }
    public int getActionMasked() { return action & ACTION_MASK; }
    public int getActionIndex() { return (action & ACTION_POINTER_INDEX_MASK) >> ACTION_POINTER_INDEX_SHIFT; }
    public int getPointerCount() { return count; }
    public int getPointerId(int i) { return ids[i]; }
    public int findPointerIndex(int id) { for (int i = 0; i < count; i++) if (ids[i] == id) return i; return -1; }
    public float getX() { return xs[0]; }
    public float getY() { return ys[0]; }
    public float getX(int i) { return xs[i]; }
    public float getY(int i) { return ys[i]; }
    public long getEventTime() { return eventTime; }
    public long getDownTime() { return eventTime; }
    public float getPressure() { return 1.0f; }
}
