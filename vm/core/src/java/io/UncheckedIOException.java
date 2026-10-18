package java.io;

public class UncheckedIOException extends RuntimeException {
    public UncheckedIOException(String m, IOException c) { super(m, c); }
}
