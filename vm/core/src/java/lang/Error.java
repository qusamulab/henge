package java.lang;

import java.lang.String;

public class Error extends Throwable {
    public Error() { super(); }
    public Error(String message) { super(message); }
    public Error(String message, Throwable cause) { super(message, cause); }
    public Error(Throwable cause) { super(cause); }
}
