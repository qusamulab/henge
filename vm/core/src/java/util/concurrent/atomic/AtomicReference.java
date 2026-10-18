package java.util.concurrent.atomic;

public class AtomicReference<V> {
    private V value;
    public AtomicReference() {}
    public AtomicReference(V v) { value = v; }
    public final V get() { return value; }
    public final void set(V v) { value = v; }
    public final V getAndSet(V v) { V o = value; value = v; return o; }
    public final boolean compareAndSet(V e, V n) { if (value == e) { value = n; return true; } return false; }
}
