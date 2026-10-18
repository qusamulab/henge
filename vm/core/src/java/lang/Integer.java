package java.lang;

import java.lang.String;

public final class Integer extends Number implements Comparable<java.lang.Integer> {
    public static final Class<java.lang.Integer> TYPE = Class.getPrimitiveClass("int");
    public static final int MAX_VALUE = 0x7fffffff;
    public static final int MIN_VALUE = 0x80000000;
    private final int value;

    public Integer(int v) { value = v; }

    public static java.lang.Integer valueOf(int i) { return new java.lang.Integer(i); }
    public static int parseInt(String s) { return parseInt(s, 10); }

    public static int parseInt(String s, int radix) {
        if (s == null) throw new NumberFormatException("null");
        int n = s.length(), i = 0, sign = 1;
        if (n == 0) throw new NumberFormatException("empty string");
        if (s.charAt(0) == '-') { sign = -1; i = 1; } else if (s.charAt(0) == '+') { i = 1; }
        if (i == n) throw new NumberFormatException(s);
        long r = 0;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException(s);
            r = r * radix + d;
            if (r > 2147483648L) throw new NumberFormatException(s);
        }
        r *= sign;
        if (r > MAX_VALUE || r < MIN_VALUE) throw new NumberFormatException(s);
        return (int) r;
    }

    public static String toString(int i) { return Long.toString((long) i, 10); }
    public static String toString(int i, int radix) { return Long.toString((long) i, radix); }
    public static String toHexString(int i) { return Long.toUnsignedString(i & 0xffffffffL, 16); }
    public static String toBinaryString(int i) { return Long.toUnsignedString(i & 0xffffffffL, 2); }
    public static int compare(int a, int b) { return a < b ? -1 : (a == b ? 0 : 1); }
    public static int signum(int i) { return i > 0 ? 1 : (i < 0 ? -1 : 0); }
    public static int bitCount(int i) {
        i = i - ((i >>> 1) & 0x55555555);
        i = (i & 0x33333333) + ((i >>> 2) & 0x33333333);
        i = (i + (i >>> 4)) & 0x0f0f0f0f;
        return (i * 0x01010101) >>> 24;
    }
    public static int max(int a, int b) { return a >= b ? a : b; }
    public static int min(int a, int b) { return a <= b ? a : b; }
    public static int sum(int a, int b) { return a + b; }
    public static int hashCode(int i) { return i; }

    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int compareTo(java.lang.Integer o) { return compare(value, o.value); }
    public int hashCode() { return value; }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Integer && ((java.lang.Integer) o).value == value; }
    public String toString() { return toString(value); }
}
