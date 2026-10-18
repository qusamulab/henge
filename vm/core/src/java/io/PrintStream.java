package java.io;

public class PrintStream {
    private final int fd;
    public PrintStream(int fd) { this.fd = fd; }
    private static native void write(int fd, String s);
    public void print(String s) { write(fd, s == null ? "null" : s); }
    public void print(Object o) { print(String.valueOf(o)); }
    public void print(int i) { print(Integer.toString(i)); }
    public void print(long l) { print(Long.toString(l)); }
    public void print(float f) { print(Float.toString(f)); }
    public void print(double d) { print(Double.toString(d)); }
    public void print(boolean b) { print(b ? "true" : "false"); }
    public void print(char c) { print(String.valueOf(c)); }
    public void println() { print("\n"); }
    public void println(String s) { print(s); print("\n"); }
    public void println(Object o) { println(String.valueOf(o)); }
    public void println(int i) { println(Integer.toString(i)); }
    public void println(long l) { println(Long.toString(l)); }
    public void println(float f) { println(Float.toString(f)); }
    public void println(double d) { println(Double.toString(d)); }
    public void println(boolean b) { println(b ? "true" : "false"); }
    public void println(char c) { println(String.valueOf(c)); }
    public void flush() {}
}
