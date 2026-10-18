package java.util.concurrent.atomic;

public class AtomicInteger extends Number {
    private int value;
    public AtomicInteger() {}
    public AtomicInteger(int v) { value = v; }
    public final int get() { return value; }
    public final void set(int v) { value = v; }
    public final int getAndSet(int v) { int o = value; value = v; return o; }
    public final int incrementAndGet() { return ++value; }
    public final int decrementAndGet() { return --value; }
    public final int getAndIncrement() { return value++; }
    public final int getAndDecrement() { return value--; }
    public final int addAndGet(int d) { value += d; return value; }
    public final int getAndAdd(int d) { int o = value; value += d; return o; }
    public final boolean compareAndSet(int e, int n) { if (value == e) { value = n; return true; } return false; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return Integer.toString(value); }
}
