package java.nio;

/** Buffers are views over a byte[] (hb) at byteOffset; elemShift is log2 of the element size. */
public abstract class Buffer {
    public byte[] hb;
    public int byteOffset;
    public int elemShift;
    int position, limit, capacity;

    Buffer(byte[] hb, int byteOffset, int elemShift, int capacity) {
        this.hb = hb;
        this.byteOffset = byteOffset;
        this.elemShift = elemShift;
        this.capacity = capacity;
        this.limit = capacity;
    }

    public final int capacity() { return capacity; }
    public final int position() { return position; }
    public final int limit() { return limit; }
    public Buffer position(int p) { if (p < 0 || p > limit) throw new IllegalArgumentException("position " + p); position = p; return this; }
    public Buffer limit(int l) { if (l < 0 || l > capacity) throw new IllegalArgumentException("limit " + l); limit = l; if (position > l) position = l; return this; }
    public Buffer clear() { position = 0; limit = capacity; return this; }
    public Buffer flip() { limit = position; position = 0; return this; }
    public Buffer rewind() { position = 0; return this; }
    public final int remaining() { return limit - position; }
    public final boolean hasRemaining() { return position < limit; }
    public boolean isDirect() { return true; }
    public boolean hasArray() { return true; }
    final int next() { if (position >= limit) throw new BufferOverflowException(); return position++; }
    final int check(int i) { if (i < 0 || i >= limit) throw new IndexOutOfBoundsException("index " + i); return i; }
}
