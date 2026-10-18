package java.lang;

import java.lang.String;

public class ClassNotFoundException extends Exception {
    public ClassNotFoundException() { super(); }
    public ClassNotFoundException(String message) { super(message); }
    public ClassNotFoundException(String message, Throwable cause) { super(message, cause); }
    public ClassNotFoundException(Throwable cause) { super(cause); }
}
