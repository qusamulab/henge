package java.lang;

import java.lang.String;

public class ArithmeticException extends RuntimeException {
    public ArithmeticException() { super(); }
    public ArithmeticException(String message) { super(message); }
    public ArithmeticException(String message, Throwable cause) { super(message, cause); }
    public ArithmeticException(Throwable cause) { super(cause); }
}
