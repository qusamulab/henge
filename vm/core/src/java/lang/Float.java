package java.lang;

import java.lang.String;

public final class Float extends Number implements Comparable<java.lang.Float> {
    public static final Class<java.lang.Float> TYPE = Class.getPrimitiveClass("float");
    public static final float MAX_VALUE = 3.4028235e+38f;
    public static final float NaN = 0.0f / 0.0f;
    private final float value;
    public Float(float v) { value = v; }
    public static java.lang.Float valueOf(float f) { return new java.lang.Float(f); }
    public static native int floatToRawIntBits(float f);
    public static int floatToIntBits(float f) { return f != f ? 0x7fc00000 : floatToRawIntBits(f); }
    public static native float intBitsToFloat(int bits);
    public static native String toString(float f);
    public static native float parseFloat(String s);
    public static int compare(float a, float b) { return a < b ? -1 : (a > b ? 1 : Integer.compare(floatToIntBits(a), floatToIntBits(b))); }
    public static boolean isNaN(float f) { return f != f; }
    public static boolean isInfinite(float f) { return f == 1.0f / 0.0f || f == -1.0f / 0.0f; }
    public static float max(float a, float b) { return a >= b ? a : b; }
    public static float min(float a, float b) { return a <= b ? a : b; }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int compareTo(java.lang.Float o) { return compare(value, o.value); }
    public int hashCode() { return floatToIntBits(value); }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Float && floatToIntBits(((java.lang.Float) o).value) == floatToIntBits(value); }
    public String toString() { return toString(value); }
}
