package java.lang;

import java.lang.Object;
import java.lang.String;

public final class Class<T> {
    private long handle; // the VM's Class*

    private Class() {}

    public native String getName();

    public String getSimpleName() {
        String n = getName();
        int i = Math.max(n.lastIndexOf('.'), n.lastIndexOf('$'));
        return n.substring(i + 1);
    }

    public String getCanonicalName() { return getName().replace('$', '.'); }

    public native boolean isInstance(Object o);

    public native boolean isArray();

    public native boolean isInterface();

    public native Class<? super T> getSuperclass();

    public native Class<?> getComponentType();

    public boolean desiredAssertionStatus() { return false; }

    public static native Class<?> forName(String name) throws ClassNotFoundException;

    static native <U> Class<U> getPrimitiveClass(String name);

    public boolean isPrimitive() { return getName().length() <= 7 && getPrimitiveClassCheck(getName()); }

    private static boolean getPrimitiveClassCheck(String n) {
        return n.equals("int") || n.equals("long") || n.equals("short") || n.equals("byte") || n.equals("char")
            || n.equals("boolean") || n.equals("float") || n.equals("double") || n.equals("void");
    }

    public String toString() { return (isInterface() ? "interface " : "class ") + getName(); }

    public native int hashCode();
}
