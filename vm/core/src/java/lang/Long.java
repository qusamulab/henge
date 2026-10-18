package java.lang;

import java.lang.String;

public final class Long extends Number implements Comparable<java.lang.Long> {
    public static final Class<java.lang.Long> TYPE = Class.getPrimitiveClass("long");
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    public static final long MIN_VALUE = 0x8000000000000000L;
    private final long value;

    public Long(long v) { value = v; }

    public static java.lang.Long valueOf(long l) { return new java.lang.Long(l); }

    public static long parseLong(String s) {
        if (s == null) throw new NumberFormatException("null");
        int n = s.length(), i = 0;
        boolean neg = false;
        if (n == 0) throw new NumberFormatException("empty string");
        if (s.charAt(0) == '-') { neg = true; i = 1; } else if (s.charAt(0) == '+') { i = 1; }
        if (i == n) throw new NumberFormatException(s);
        long r = 0;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), 10);
            if (d < 0) throw new NumberFormatException(s);
            r = r * 10 - d;
        }
        return neg ? r : -r;
    }

    public static String toString(long l) { return toString(l, 10); }

    public static String toString(long l, int radix) {
        if (l == 0) return "0";
        char[] buf = new char[65];
        int pos = 65;
        boolean neg = l < 0;
        if (!neg) l = -l;
        while (l != 0) {
            buf[--pos] = Character.forDigit((int) -(l % radix), radix);
            l /= radix;
        }
        if (neg) buf[--pos] = '-';
        return new String(buf, pos, 65 - pos);
    }

    public static String toUnsignedString(long l, int radix) {
        if (l == 0) return "0";
        char[] buf = new char[65];
        int pos = 65;
        while (l != 0) {
            long q = (l >>> 1) / radix << 1;
            long r = l - q * radix;
            if (r >= radix) { q++; r -= radix; }
            buf[--pos] = Character.forDigit((int) r, radix);
            l = q;
        }
        return new String(buf, pos, 65 - pos);
    }

    public static String toHexString(long l) { return toUnsignedString(l, 16); }
    public static int compare(long a, long b) { return a < b ? -1 : (a == b ? 0 : 1); }
    public static int hashCode(long l) { return (int) (l ^ (l >>> 32)); }
    public static long max(long a, long b) { return a >= b ? a : b; }
    public static long min(long a, long b) { return a <= b ? a : b; }

    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int compareTo(java.lang.Long o) { return compare(value, o.value); }
    public int hashCode() { return hashCode(value); }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Long && ((java.lang.Long) o).value == value; }
    public String toString() { return toString(value, 10); }
}
