package java.lang;

import java.lang.String;

public class ArrayIndexOutOfBoundsException extends IndexOutOfBoundsException {
    public ArrayIndexOutOfBoundsException() { super(); }
    public ArrayIndexOutOfBoundsException(String message) { super(message); }
    public ArrayIndexOutOfBoundsException(String message, Throwable cause) { super(message, cause); }
    public ArrayIndexOutOfBoundsException(Throwable cause) { super(cause); }
}
