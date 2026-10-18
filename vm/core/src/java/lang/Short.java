package java.lang;

public final class Short extends Number implements Comparable<java.lang.Short> {
    public static final Class<java.lang.Short> TYPE = Class.getPrimitiveClass("short");
    private final short value;
    public Short(short v) { value = v; }
    public static java.lang.Short valueOf(short s) { return new java.lang.Short(s); }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int compareTo(java.lang.Short o) { return value - o.value; }
    public int hashCode() { return value; }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Short && ((java.lang.Short) o).value == value; }
    public java.lang.String toString() { return Integer.toString(value); }
}
