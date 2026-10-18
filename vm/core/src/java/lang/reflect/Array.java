package java.lang.reflect;

public final class Array {
    private Array() {}
    public static native Object newInstance(Class<?> componentType, int length);
    public static native Object newInstance(Class<?> componentType, int... dimensions);
    public static native int getLength(Object array);
}
