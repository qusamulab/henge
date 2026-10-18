package java.lang;

import java.lang.String;

public final class Double extends Number implements Comparable<java.lang.Double> {
    public static final Class<java.lang.Double> TYPE = Class.getPrimitiveClass("double");
    public static final double MAX_VALUE = 1.7976931348623157e+308;
    public static final double NaN = 0.0d / 0.0;
    public static final double POSITIVE_INFINITY = 1.0 / 0.0;
    public static final double NEGATIVE_INFINITY = -1.0 / 0.0;
    private final double value;
    public Double(double v) { value = v; }
    public static java.lang.Double valueOf(double d) { return new java.lang.Double(d); }
    public static native long doubleToRawLongBits(double d);
    public static long doubleToLongBits(double d) { return d != d ? 0x7ff8000000000000L : doubleToRawLongBits(d); }
    public static native double longBitsToDouble(long bits);
    public static native String toString(double d);
    public static native double parseDouble(String s);
    public static int compare(double a, double b) { return a < b ? -1 : (a > b ? 1 : Long.compare(doubleToLongBits(a), doubleToLongBits(b))); }
    public static boolean isNaN(double d) { return d != d; }
    public static boolean isInfinite(double d) { return d == POSITIVE_INFINITY || d == NEGATIVE_INFINITY; }
    public static double max(double a, double b) { return a >= b ? a : b; }
    public static double min(double a, double b) { return a <= b ? a : b; }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return (float) value; }
    public double doubleValue() { return value; }
    public int compareTo(java.lang.Double o) { return compare(value, o.value); }
    public int hashCode() { return Long.hashCode(doubleToLongBits(value)); }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Double && doubleToLongBits(((java.lang.Double) o).value) == doubleToLongBits(value); }
    public String toString() { return toString(value); }
}
