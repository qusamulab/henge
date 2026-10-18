package java.nio;

public class FloatBuffer extends Buffer {
    FloatBuffer(byte[] hb, int off, int cap) { super(hb, off, 2, cap); }

    public static FloatBuffer allocate(int n) { return new FloatBuffer(new byte[n * 4], 0, n); }
    public static FloatBuffer wrap(float[] a) { FloatBuffer b = allocate(a.length); b.put(a); b.rewind(); return b; }


    private int at(int i) { return byteOffset + (i << 2); }

    public float get(int i) {
        int o = at(check(i));
        return Float.intBitsToFloat((hb[o] & 0xff) | (hb[o + 1] & 0xff) << 8 | (hb[o + 2] & 0xff) << 16 | (hb[o + 3] & 0xff) << 24);
    }

    public float get() { float f = get(position); position++; return f; }

    public FloatBuffer put(int i, float f) {
        int o = at(check(i));
        int v = Float.floatToRawIntBits(f);
        hb[o] = (byte) v; hb[o + 1] = (byte) (v >> 8); hb[o + 2] = (byte) (v >> 16); hb[o + 3] = (byte) (v >> 24);
        return this;
    }

    public FloatBuffer put(float f) { put(next(), f); return this; }

    public FloatBuffer put(float[] a) { return put(a, 0, a.length); }
    public FloatBuffer put(float[] a, int off, int len) {
        if (len > remaining()) throw new BufferOverflowException();
        for (int i = 0; i < len; i++) put(position++, a[off + i]);
        return this;
    }

    public FloatBuffer get(float[] a) {
        if (a.length > remaining()) throw new BufferOverflowException();
        for (int i = 0; i < a.length; i++) a[i] = get(position++);
        return this;
    }

    public float[] array() {
        float[] a = new float[capacity];
        for (int i = 0; i < capacity; i++) a[i] = get(i);
        return a;
    }
}
