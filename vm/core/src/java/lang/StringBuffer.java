package java.lang;

public final class StringBuffer implements CharSequence {
    private final StringBuilder sb;
    public StringBuffer() { sb = new StringBuilder(); }
    public StringBuffer(String s) { sb = new StringBuilder(s); }
    public StringBuffer append(String s) { sb.append(s); return this; }
    public StringBuffer append(Object o) { sb.append(o); return this; }
    public StringBuffer append(char c) { sb.append(c); return this; }
    public StringBuffer append(int i) { sb.append(i); return this; }
    public StringBuffer append(long l) { sb.append(l); return this; }
    public StringBuffer append(float f) { sb.append(f); return this; }
    public StringBuffer append(double d) { sb.append(d); return this; }
    public StringBuffer append(boolean b) { sb.append(b); return this; }
    public StringBuffer reverse() { sb.reverse(); return this; }
    public int length() { return sb.length(); }
    public char charAt(int i) { return sb.charAt(i); }
    public CharSequence subSequence(int s, int e) { return sb.substring(s, e); }
    public String toString() { return sb.toString(); }
}
