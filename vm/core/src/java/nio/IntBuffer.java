package java.nio;

public class IntBuffer extends Buffer {
    IntBuffer(byte[] hb, int off, int cap) { super(hb, off, 2, cap); }

    public static IntBuffer allocate(int n) { return new IntBuffer(new byte[n * 4], 0, n); }
    public static IntBuffer wrap(int[] a) { IntBuffer b = allocate(a.length); b.put(a); b.rewind(); return b; }


    public int get(int i) {
        int o = byteOffset + (check(i) << 2);
        return (hb[o] & 0xff) | (hb[o + 1] & 0xff) << 8 | (hb[o + 2] & 0xff) << 16 | (hb[o + 3] & 0xff) << 24;
    }
    public int get() { int v = get(position); position++; return v; }
    public IntBuffer put(int i, int v) {
        int o = byteOffset + (check(i) << 2);
        hb[o] = (byte) v; hb[o + 1] = (byte) (v >> 8); hb[o + 2] = (byte) (v >> 16); hb[o + 3] = (byte) (v >> 24);
        return this;
    }
    public IntBuffer put(int v) { put(next(), v); return this; }
    public IntBuffer put(int[] a) { for (int i = 0; i < a.length; i++) put(a[i]); return this; }
    public int[] array() { int[] a = new int[capacity]; for (int i = 0; i < capacity; i++) a[i] = get(i); return a; }
}
