package java.lang;

import java.lang.String;

public class CloneNotSupportedException extends Exception {
    public CloneNotSupportedException() { super(); }
    public CloneNotSupportedException(String message) { super(message); }
    public CloneNotSupportedException(String message, Throwable cause) { super(message, cause); }
    public CloneNotSupportedException(Throwable cause) { super(cause); }
}
