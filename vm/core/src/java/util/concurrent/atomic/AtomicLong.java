package java.util.concurrent.atomic;

public class AtomicLong extends Number {
    private long value;
    public AtomicLong() {}
    public AtomicLong(long v) { value = v; }
    public final long get() { return value; }
    public final void set(long v) { value = v; }
    public final long incrementAndGet() { return ++value; }
    public final long getAndIncrement() { return value++; }
    public final long addAndGet(long d) { value += d; return value; }
    public final boolean compareAndSet(long e, long n) { if (value == e) { value = n; return true; } return false; }
    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return Long.toString(value); }
}
