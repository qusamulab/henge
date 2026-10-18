package android.view;

public class View {
    public interface OnTouchListener { boolean onTouch(View v, MotionEvent e); }

    private OnTouchListener touchListener;
    public int henge_width, henge_height;

    public View() {}
    public int getWidth() { return henge_width; }
    public int getHeight() { return henge_height; }
    public void setOnTouchListener(OnTouchListener l) { touchListener = l; }
    public boolean onTouchEvent(MotionEvent e) { return false; }
    public boolean performClick() { return false; }
    public void invalidate() {}
    public boolean post(Runnable r) { new android.os.Handler().post(r); return true; }

    public boolean dispatchTouchEvent(MotionEvent e) {
        if (touchListener != null && touchListener.onTouch(this, e)) return true;
        return onTouchEvent(e);
    }
}
