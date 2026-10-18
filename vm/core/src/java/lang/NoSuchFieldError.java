package java.lang;

import java.lang.String;

public class NoSuchFieldError extends IncompatibleClassChangeError {
    public NoSuchFieldError() { super(); }
    public NoSuchFieldError(String message) { super(message); }
    public NoSuchFieldError(String message, Throwable cause) { super(message, cause); }
    public NoSuchFieldError(Throwable cause) { super(cause); }
}
