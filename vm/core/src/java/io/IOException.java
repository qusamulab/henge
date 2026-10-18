package java.io;

public class IOException extends Exception {
    public IOException() {}
    public IOException(String m) { super(m); }
    public IOException(String m, Throwable c) { super(m, c); }
    public IOException(Throwable c) { super(c); }
}
