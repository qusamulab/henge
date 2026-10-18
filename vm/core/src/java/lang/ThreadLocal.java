package java.lang;

public class ThreadLocal<T> {
    private T value;
    private boolean set;
    protected T initialValue() { return null; }
    public T get() { if (!set) { value = initialValue(); set = true; } return value; }
    public void set(T v) { value = v; set = true; }
    public void remove() { set = false; value = null; }
}
