package java.lang;

/** java.lang.String: instances are created and laid out by the VM (UTF-16), every method is native. */
public final class String implements CharSequence, Comparable<java.lang.String> {
    public String() {}
    public String(char[] chars) {}
    public String(char[] chars, int offset, int count) {}
    public String(java.lang.String s) {}
    public String(byte[] bytes) {}
    public String(byte[] bytes, int offset, int count) {}
    public String(StringBuilder sb) {}

    public native int length();
    public native char charAt(int index);
    public native boolean isEmpty();
    public native java.lang.String substring(int begin);
    public native java.lang.String substring(int begin, int end);
    public java.lang.CharSequence subSequence(int begin, int end) { return substring(begin, end); }
    public native int indexOf(int ch);
    public native int indexOf(int ch, int from);
    public native int indexOf(java.lang.String s);
    public native int indexOf(java.lang.String s, int from);
    public native int lastIndexOf(int ch);
    public native int lastIndexOf(int ch, int from);
    public native int lastIndexOf(java.lang.String s);
    public native boolean equals(java.lang.Object o);
    public native boolean equalsIgnoreCase(java.lang.String o);
    public native int hashCode();
    public native int compareTo(java.lang.String o);
    public native int compareToIgnoreCase(java.lang.String o);
    public native boolean startsWith(java.lang.String prefix);
    public native boolean startsWith(java.lang.String prefix, int offset);
    public native boolean endsWith(java.lang.String suffix);
    public boolean contains(java.lang.CharSequence s) { return indexOf(s.toString()) >= 0; }
    public native java.lang.String replace(char from, char to);
    public native java.lang.String replace(java.lang.CharSequence from, java.lang.CharSequence to);
    public native java.lang.String trim();
    public native java.lang.String toLowerCase();
    public native java.lang.String toUpperCase();
    public native java.lang.String concat(java.lang.String s);
    public native java.lang.String intern();
    public native char[] toCharArray();
    public native void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin);
    public native byte[] getBytes();
    public java.lang.String toString() { return null; } // replaced natively: returns this
    public boolean matches(java.lang.String regex) { throw new UnsupportedOperationException("regex"); }

    public static native java.lang.String valueOf(java.lang.Object o);
    public static native java.lang.String valueOf(char[] chars);
    public static java.lang.String valueOf(int i) { return Integer.toString(i); }
    public static java.lang.String valueOf(long l) { return Long.toString(l); }
    public static java.lang.String valueOf(float f) { return Float.toString(f); }
    public static java.lang.String valueOf(double d) { return Double.toString(d); }
    public static java.lang.String valueOf(boolean b) { return b ? "true" : "false"; }
    public static java.lang.String valueOf(char c) { return new java.lang.String(new char[] {c}); }
    public static java.lang.String copyValueOf(char[] chars) { return valueOf(chars); }
    public static native java.lang.String format(java.lang.String fmt, java.lang.Object... args);
}
