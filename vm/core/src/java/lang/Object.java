package java.lang;

/** The root class. Its constructor is a native no-op (the compiler-inserted super() call would
 *  otherwise resolve back to this class). */
public class Object {
    public Object() {}

    public final native java.lang.Class<?> getClass();

    public native int hashCode();

    public boolean equals(java.lang.Object o) { return this == o; }

    public java.lang.String toString() {
        return getClass().getName() + "@" + Integer.toHexString(hashCode());
    }

    protected native java.lang.Object clone() throws CloneNotSupportedException;

    public final native void notify();

    public final native void notifyAll();

    public final void wait() throws InterruptedException { wait(0L); }

    public final native void wait(long millis) throws InterruptedException;
}
