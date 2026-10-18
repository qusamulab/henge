package java.lang;

import java.lang.String;

public class Exception extends Throwable {
    public Exception() { super(); }
    public Exception(String message) { super(message); }
    public Exception(String message, Throwable cause) { super(message, cause); }
    public Exception(Throwable cause) { super(cause); }
}
