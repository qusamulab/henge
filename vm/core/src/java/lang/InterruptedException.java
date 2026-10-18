package java.lang;

import java.lang.String;

public class InterruptedException extends Exception {
    public InterruptedException() { super(); }
    public InterruptedException(String message) { super(message); }
    public InterruptedException(String message, Throwable cause) { super(message, cause); }
    public InterruptedException(Throwable cause) { super(cause); }
}
