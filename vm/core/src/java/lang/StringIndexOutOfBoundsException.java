package java.lang;

import java.lang.String;

public class StringIndexOutOfBoundsException extends IndexOutOfBoundsException {
    public StringIndexOutOfBoundsException() { super(); }
    public StringIndexOutOfBoundsException(String message) { super(message); }
    public StringIndexOutOfBoundsException(String message, Throwable cause) { super(message, cause); }
    public StringIndexOutOfBoundsException(Throwable cause) { super(cause); }
}
