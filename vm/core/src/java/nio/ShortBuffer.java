package java.nio;

public class ShortBuffer extends Buffer {
    ShortBuffer(byte[] hb, int off, int cap) { super(hb, off, 1, cap); }

    public static ShortBuffer allocate(int n) { return new ShortBuffer(new byte[n * 2], 0, n); }
    public static ShortBuffer wrap(short[] a) { ShortBuffer b = allocate(a.length); b.put(a); b.rewind(); return b; }


    public short get(int i) { int o = byteOffset + (check(i) << 1); return (short) ((hb[o] & 0xff) | (hb[o + 1] << 8)); }
    public short get() { short v = get(position); position++; return v; }
    public ShortBuffer put(int i, short v) { int o = byteOffset + (check(i) << 1); hb[o] = (byte) v; hb[o + 1] = (byte) (v >> 8); return this; }
    public ShortBuffer put(short v) { put(next(), v); return this; }
    public ShortBuffer put(short[] a) { for (int i = 0; i < a.length; i++) put(a[i]); return this; }
    public short[] array() { short[] a = new short[capacity]; for (int i = 0; i < capacity; i++) a[i] = get(i); return a; }
}
