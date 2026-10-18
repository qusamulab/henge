package java.lang;

import java.lang.String;

public class NumberFormatException extends IllegalArgumentException {
    public NumberFormatException() { super(); }
    public NumberFormatException(String message) { super(message); }
    public NumberFormatException(String message, Throwable cause) { super(message, cause); }
    public NumberFormatException(Throwable cause) { super(cause); }
}
