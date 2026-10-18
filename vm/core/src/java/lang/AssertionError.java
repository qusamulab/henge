package java.lang;

import java.lang.String;

public class AssertionError extends Error {
    public AssertionError() { super(); }
    public AssertionError(String message) { super(message); }
    public AssertionError(String message, Throwable cause) { super(message, cause); }
    public AssertionError(Throwable cause) { super(cause); }
}
