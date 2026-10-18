package android.app;

import android.content.ContextWrapper;
import android.os.Bundle;
import android.os.Handler;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;

public class Activity extends ContextWrapper {
    /** The host reads these after onCreate. */
    public View henge_contentView;
    public boolean henge_finished;
    private final Window window = new Window();

    public Activity() {}

    protected void onCreate(Bundle savedInstanceState) {}
    protected void onStart() {}
    protected void onResume() {}
    protected void onPause() {}
    protected void onStop() {}
    protected void onDestroy() {}
    protected void onRestart() {}
    public void onWindowFocusChanged(boolean hasFocus) {}
    public boolean onTouchEvent(MotionEvent e) { return false; }
    public boolean onKeyDown(int keyCode, android.view.KeyEvent e) { return false; }
    public void onBackPressed() { finish(); }

    public void setContentView(View v) { henge_contentView = v; }
    public Window getWindow() { return window; }
    public boolean requestWindowFeature(int f) { return true; }
    public void setRequestedOrientation(int o) {}
    public void finish() { henge_finished = true; }
    public boolean isFinishing() { return henge_finished; }
    public final void runOnUiThread(Runnable r) { new Handler().post(r); }
    public final Activity getParent() { return null; }

    // called by the host
    public final void henge_create(Bundle b) { onCreate(b); }
    public final void henge_start() { onStart(); }
    public final void henge_resume() { onResume(); }
    public final void henge_pause() { onPause(); }
    public final void henge_stop() { onStop(); }
    public final void henge_destroy() { onDestroy(); }
    public final void henge_focus(boolean f) { onWindowFocusChanged(f); }
}
