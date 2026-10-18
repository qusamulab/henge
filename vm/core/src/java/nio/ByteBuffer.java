package java.nio;

public class ByteBuffer extends Buffer implements Comparable<ByteBuffer> {
    private boolean big;

    ByteBuffer(byte[] hb, int off, int cap) { super(hb, off, 0, cap); }

    public static ByteBuffer allocate(int n) { return new ByteBuffer(new byte[n], 0, n); }
    public static ByteBuffer allocateDirect(int n) { return new ByteBuffer(new byte[n], 0, n); }
    public static ByteBuffer wrap(byte[] a) { return new ByteBuffer(a, 0, a.length); }

    public ByteBuffer order(ByteOrder o) { big = o == ByteOrder.BIG_ENDIAN; return this; }
    public ByteOrder order() { return big ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }
    public byte[] array() { return hb; }
    public int arrayOffset() { return 0; }


    public byte get() { return hb[byteOffset + next()]; }
    public byte get(int i) { return hb[byteOffset + check(i)]; }
    public ByteBuffer put(byte b) { hb[byteOffset + next()] = b; return this; }
    public ByteBuffer put(int i, byte b) { hb[byteOffset + check(i)] = b; return this; }
    public ByteBuffer put(byte[] src) { return put(src, 0, src.length); }
    public ByteBuffer put(byte[] src, int off, int len) {
        if (len > remaining()) throw new BufferOverflowException();
        System.arraycopy(src, off, hb, byteOffset + position, len);
        position += len;
        return this;
    }
    public ByteBuffer get(byte[] dst) { return get(dst, 0, dst.length); }
    public ByteBuffer get(byte[] dst, int off, int len) {
        if (len > remaining()) throw new BufferOverflowException();
        System.arraycopy(hb, byteOffset + position, dst, off, len);
        position += len;
        return this;
    }

    private int rd(int at, int n) {
        int v = 0;
        if (big) for (int i = 0; i < n; i++) v = (v << 8) | (hb[byteOffset + at + i] & 0xff);
        else for (int i = n - 1; i >= 0; i--) v = (v << 8) | (hb[byteOffset + at + i] & 0xff);
        return v;
    }
    private void wr(int at, int n, int v) {
        if (big) for (int i = n - 1; i >= 0; i--) { hb[byteOffset + at + i] = (byte) v; v >>= 8; }
        else for (int i = 0; i < n; i++) { hb[byteOffset + at + i] = (byte) v; v >>= 8; }
    }

    public int getInt() { int v = rd(position, 4); position += 4; return v; }
    public int getInt(int i) { return rd(i, 4); }
    public ByteBuffer putInt(int v) { wr(position, 4, v); position += 4; return this; }
    public ByteBuffer putInt(int i, int v) { wr(i, 4, v); return this; }
    public short getShort() { short v = (short) rd(position, 2); position += 2; return v; }
    public ByteBuffer putShort(short v) { wr(position, 2, v); position += 2; return this; }
    public float getFloat() { return Float.intBitsToFloat(getInt()); }
    public ByteBuffer putFloat(float f) { return putInt(Float.floatToRawIntBits(f)); }
    public long getLong() { long hi = getInt(), lo = getInt() & 0xffffffffL; return big ? (hi << 32) | lo : (lo << 32) | (hi & 0xffffffffL); }

    public FloatBuffer asFloatBuffer() { return new FloatBuffer(hb, byteOffset + position, (limit - position) >> 2); }
    public IntBuffer asIntBuffer() { return new IntBuffer(hb, byteOffset + position, (limit - position) >> 2); }
    public ShortBuffer asShortBuffer() { return new ShortBuffer(hb, byteOffset + position, (limit - position) >> 1); }

    public int compareTo(ByteBuffer o) { return remaining() - o.remaining(); }
}
