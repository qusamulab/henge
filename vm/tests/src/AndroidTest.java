// android.* classes of henge-vm without a window: matrices, buffers, handlers, touch events.
import android.opengl.Matrix;
import android.os.Bundle;
import android.os.Handler;
import android.view.MotionEvent;
import android.view.View;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;

public class AndroidTest {
    static int failures;
    static int ran;

    static void check(String what, boolean ok) {
        System.out.println((ok ? "PASS  " : "FAIL  ") + what);
        if (!ok) failures++;
    }

    static boolean near(float a, float b) { return Math.abs(a - b) < 1e-4f; }

    public static int run() {
        failures = 0;
        float[] m = new float[16];
        Matrix.setIdentityM(m, 0);
        Matrix.translateM(m, 0, 2f, 3f, 4f);
        check("translate", near(m[12], 2f) && near(m[13], 3f) && near(m[14], 4f));
        float[] o = new float[16];
        Matrix.orthoM(o, 0, 0f, 100f, 0f, 200f, -1f, 1f);
        check("ortho", near(o[0], 0.02f) && near(o[5], 0.01f) && near(o[12], -1f) && near(o[13], -1f));
        float[] r = new float[16], p = new float[16];
        Matrix.setIdentityM(r, 0);
        Matrix.rotateM(r, 0, 90f, 0f, 0f, 1f);
        Matrix.multiplyMM(p, 0, o, 0, r, 0);
        check("rotate / multiply", near(r[0], 0f) && near(r[1], 1f) && near(p[1], 0.01f));

        FloatBuffer fb = ByteBuffer.allocateDirect(16 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        fb.put(new float[] {1f, 2.5f, -3f});
        fb.position(0);
        check("FloatBuffer", fb.get(0) == 1f && fb.get(1) == 2.5f && fb.get(2) == -3f && fb.capacity() == 16);
        ShortBuffer sb = ByteBuffer.allocateDirect(8).order(ByteOrder.nativeOrder()).asShortBuffer();
        sb.put((short) 7).put((short) -2);
        check("ShortBuffer", sb.get(0) == 7 && sb.get(1) == -2);
        ByteBuffer bb = ByteBuffer.allocate(8).order(ByteOrder.BIG_ENDIAN);
        bb.putInt(0x01020304);
        check("ByteBuffer big endian", bb.get(0) == 1 && bb.get(3) == 4);

        Bundle b = new Bundle();
        b.putInt("n", 5);
        b.putString("s", "x");
        check("Bundle", b.getInt("n") == 5 && b.getString("s").equals("x") && b.getInt("missing", 9) == 9);

        ran = 0;
        Handler h = new Handler();
        h.post(new Runnable() { public void run() { ran++; } });
        h.postDelayed(new Runnable() { public void run() { ran += 10; } }, 100000);
        Handler.runPending();
        check("Handler", ran == 1);

        final int[] touched = new int[1];
        View v = new View() { public boolean onTouchEvent(MotionEvent e) { touched[0] = e.getActionMasked() * 100 + e.getPointerCount(); return true; } };
        MotionEvent e = new MotionEvent();
        e.action = MotionEvent.ACTION_MOVE;
        e.count = 2;
        check("touch dispatch", v.dispatchTouchEvent(e) && touched[0] == 202);

        Enum1 en = Enum1.valueOf("B");
        check("enum", en == Enum1.B && Enum1.values().length == 3 && en.ordinal() == 1 && en.name().equals("B"));
        java.util.ArrayList<String> list = new java.util.ArrayList<String>();
        list.add("b"); list.add("a"); list.add("c");
        java.util.Collections.sort(list);
        check("collections", list.toString().equals("[a, b, c]"));
        java.util.HashMap<String, Integer> map = new java.util.HashMap<String, Integer>();
        for (int i = 0; i < 100; i++) map.put("k" + i, i);
        check("HashMap", map.size() == 100 && map.get("k42") == 42 && map.remove("k1") == 1 && !map.containsKey("k1"));
        return failures;
    }

    enum Enum1 { A, B, C }
}
