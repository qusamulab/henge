package java.lang;

import java.lang.String;
import java.lang.Object;

public final class StringBuilder implements CharSequence {
    private char[] value;
    private int count;

    public StringBuilder() { value = new char[16]; }
    public StringBuilder(int capacity) { value = new char[capacity < 1 ? 1 : capacity]; }
    public StringBuilder(String s) { value = new char[s.length() + 16]; append(s); }
    public StringBuilder(java.lang.CharSequence s) { value = new char[s.length() + 16]; append(s.toString()); }

    private void ensure(int extra) {
        int need = count + extra;
        if (need > value.length) {
            char[] n = new char[need * 2 + 2];
            System.arraycopy(value, 0, n, 0, count);
            value = n;
        }
    }

    public StringBuilder append(String s) {
        if (s == null) s = "null";
        int n = s.length();
        ensure(n);
        s.getChars(0, n, value, count);
        count += n;
        return this;
    }

    public StringBuilder append(Object o) { return append(String.valueOf(o)); }
    public StringBuilder append(java.lang.CharSequence s) { return append(s == null ? "null" : s.toString()); }
    public StringBuilder append(char[] chars) { return append(new String(chars)); }
    public StringBuilder append(char c) { ensure(1); value[count++] = c; return this; }
    public StringBuilder append(int i) { return append(Integer.toString(i)); }
    public StringBuilder append(long l) { return append(Long.toString(l)); }
    public StringBuilder append(float f) { return append(Float.toString(f)); }
    public StringBuilder append(double d) { return append(Double.toString(d)); }
    public StringBuilder append(boolean b) { return append(b ? "true" : "false"); }
    public StringBuilder append(java.lang.CharSequence s, int start, int end) { return append(s.toString().substring(start, end)); }
    public StringBuilder appendCodePoint(int cp) { return append((char) cp); }

    public StringBuilder insert(int offset, String s) {
        int n = s.length();
        ensure(n);
        System.arraycopy(value, offset, value, offset + n, count - offset);
        s.getChars(0, n, value, offset);
        count += n;
        return this;
    }

    public StringBuilder insert(int offset, char c) { return insert(offset, String.valueOf(c)); }
    public StringBuilder insert(int offset, int i) { return insert(offset, Integer.toString(i)); }
    public StringBuilder insert(int offset, Object o) { return insert(offset, String.valueOf(o)); }

    public StringBuilder reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) { char t = value[i]; value[i] = value[j]; value[j] = t; }
        return this;
    }

    public StringBuilder deleteCharAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index);
        System.arraycopy(value, index + 1, value, index, count - index - 1);
        count--;
        return this;
    }

    public StringBuilder delete(int start, int end) {
        if (end > count) end = count;
        System.arraycopy(value, end, value, start, count - end);
        count -= end - start;
        return this;
    }

    public StringBuilder replace(int start, int end, String s) {
        delete(start, end);
        return insert(start, s);
    }

    public void setLength(int n) {
        if (n > count) { ensure(n - count); for (int i = count; i < n; i++) value[i] = 0; }
        count = n;
    }

    public int indexOf(String s) { return toString().indexOf(s); }
    public int length() { return count; }
    public int capacity() { return value.length; }
    public boolean isEmpty() { return count == 0; }
    public char charAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException("index " + i); return value[i]; }
    public void setCharAt(int i, char c) { value[i] = c; }
    public java.lang.CharSequence subSequence(int s, int e) { return substring(s, e); }
    public String substring(int s) { return new String(value, s, count - s); }
    public String substring(int s, int e) { return new String(value, s, e - s); }
    public String toString() { return new String(value, 0, count); }
}
