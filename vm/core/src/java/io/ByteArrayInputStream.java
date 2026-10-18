package java.io;

public class ByteArrayInputStream extends InputStream {
    private final byte[] buf;
    private int pos;
    private final int end;

    public ByteArrayInputStream(byte[] b) { buf = b; end = b.length; }
    public ByteArrayInputStream(byte[] b, int off, int len) { buf = b; pos = off; end = off + len; }

    public int read() { return pos < end ? buf[pos++] & 0xff : -1; }
    public int available() { return end - pos; }

    public int read(byte[] b, int off, int len) {
        if (pos >= end) return -1;
        int n = Math.min(len, end - pos);
        System.arraycopy(buf, pos, b, off, n);
        pos += n;
        return n;
    }
}
