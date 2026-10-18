package java.io;

public class ByteArrayOutputStream extends OutputStream {
    private byte[] buf = new byte[32];
    private int count;

    public void write(int b) {
        if (count == buf.length) { byte[] n = new byte[count * 2]; System.arraycopy(buf, 0, n, 0, count); buf = n; }
        buf[count++] = (byte) b;
    }

    public byte[] toByteArray() { byte[] r = new byte[count]; System.arraycopy(buf, 0, r, 0, count); return r; }
    public int size() { return count; }
    public void reset() { count = 0; }
    public String toString() { return new String(buf, 0, count); }
}
