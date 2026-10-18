package java.lang;

import java.lang.String;

public class UnsatisfiedLinkError extends LinkageError {
    public UnsatisfiedLinkError() { super(); }
    public UnsatisfiedLinkError(String message) { super(message); }
    public UnsatisfiedLinkError(String message, Throwable cause) { super(message, cause); }
    public UnsatisfiedLinkError(Throwable cause) { super(cause); }
}
