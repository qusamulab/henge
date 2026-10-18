package java.lang;

import java.lang.String;

public class NoSuchMethodError extends IncompatibleClassChangeError {
    public NoSuchMethodError() { super(); }
    public NoSuchMethodError(String message) { super(message); }
    public NoSuchMethodError(String message, Throwable cause) { super(message, cause); }
    public NoSuchMethodError(Throwable cause) { super(cause); }
}
