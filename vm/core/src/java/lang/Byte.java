package java.lang;

public final class Byte extends Number implements Comparable<java.lang.Byte> {
    public static final Class<java.lang.Byte> TYPE = Class.getPrimitiveClass("byte");
    private final byte value;
    public Byte(byte v) { value = v; }
    public static java.lang.Byte valueOf(byte b) { return new java.lang.Byte(b); }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int compareTo(java.lang.Byte o) { return value - o.value; }
    public int hashCode() { return value; }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Byte && ((java.lang.Byte) o).value == value; }
    public java.lang.String toString() { return Integer.toString(value); }
}
