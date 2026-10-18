package java.lang;

import java.lang.String;

public class UnsupportedOperationException extends RuntimeException {
    public UnsupportedOperationException() { super(); }
    public UnsupportedOperationException(String message) { super(message); }
    public UnsupportedOperationException(String message, Throwable cause) { super(message, cause); }
    public UnsupportedOperationException(Throwable cause) { super(cause); }
}
