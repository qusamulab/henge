package java.lang;

import java.lang.String;

public class OutOfMemoryError extends VirtualMachineError {
    public OutOfMemoryError() { super(); }
    public OutOfMemoryError(String message) { super(message); }
    public OutOfMemoryError(String message, Throwable cause) { super(message, cause); }
    public OutOfMemoryError(Throwable cause) { super(cause); }
}
