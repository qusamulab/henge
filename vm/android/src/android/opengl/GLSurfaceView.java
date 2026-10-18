package android.opengl;

import android.view.SurfaceView;
import java.util.ArrayList;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** The host owns the EGL surface and calls these from its render loop (one thread does everything). */
public class GLSurfaceView extends SurfaceView {
    public static final int RENDERMODE_WHEN_DIRTY = 0, RENDERMODE_CONTINUOUSLY = 1;

    public interface Renderer {
        void onSurfaceCreated(GL10 gl, EGLConfig config);
        void onSurfaceChanged(GL10 gl, int width, int height);
        void onDrawFrame(GL10 gl);
    }

    private Renderer renderer;
    private int renderMode = RENDERMODE_CONTINUOUSLY;
    private boolean dirty = true;
    private final ArrayList<Runnable> events = new ArrayList<Runnable>();
    public int henge_glVersion = 2;

    public GLSurfaceView(android.content.Context c) {}

    public void setEGLContextClientVersion(int v) { henge_glVersion = v; }
    public void setEGLConfigChooser(boolean needDepth) {}
    public void setEGLConfigChooser(int r, int g, int b, int a, int depth, int stencil) {}
    public void setPreserveEGLContextOnPause(boolean b) {}
    public void setRenderer(Renderer r) { renderer = r; }
    public void setRenderMode(int m) { renderMode = m; }
    public int getRenderMode() { return renderMode; }
    public void requestRender() { dirty = true; }
    public void onPause() {}
    public void onResume() {}
    public void queueEvent(Runnable r) { events.add(r); }

    // host side
    public final void henge_surfaceCreated() { if (renderer != null) renderer.onSurfaceCreated(null, new EGLConfig()); }
    public final void henge_surfaceChanged(int w, int h) { henge_width = w; henge_height = h; if (renderer != null) renderer.onSurfaceChanged(null, w, h); }

    public final boolean henge_drawFrame() {
        while (!events.isEmpty()) events.remove(0).run();
        if (renderMode == RENDERMODE_WHEN_DIRTY && !dirty) return false;
        dirty = false;
        if (renderer != null) renderer.onDrawFrame(null);
        return true;
    }
}
