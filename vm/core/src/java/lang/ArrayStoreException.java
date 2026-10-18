package java.lang;

import java.lang.String;

public class ArrayStoreException extends RuntimeException {
    public ArrayStoreException() { super(); }
    public ArrayStoreException(String message) { super(message); }
    public ArrayStoreException(String message, Throwable cause) { super(message, cause); }
    public ArrayStoreException(Throwable cause) { super(cause); }
}
