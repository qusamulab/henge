package java.lang;

import java.lang.String;

public class Throwable {
    private String detailMessage;
    private Throwable cause;

    public Throwable() {}
    public Throwable(String message) { detailMessage = message; }
    public Throwable(String message, Throwable cause) { detailMessage = message; this.cause = cause; }
    public Throwable(Throwable cause) { this.cause = cause; if (cause != null) detailMessage = cause.toString(); }

    public String getMessage() { return detailMessage; }
    public String getLocalizedMessage() { return getMessage(); }
    public Throwable getCause() { return cause; }
    public Throwable initCause(Throwable c) { cause = c; return this; }
    public Throwable fillInStackTrace() { return this; }
    public StackTraceElement[] getStackTrace() { return new StackTraceElement[0]; }
    public void setStackTrace(StackTraceElement[] trace) {}
    public final void addSuppressed(Throwable t) {}
    public final Throwable[] getSuppressed() { return new Throwable[0]; }

    public String toString() {
        String n = getClass().getName();
        String m = getLocalizedMessage();
        return m != null ? n + ": " + m : n;
    }

    public void printStackTrace() {
        System.err.println(toString());
        if (cause != null) System.err.println("Caused by: " + cause);
    }
}
