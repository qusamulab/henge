package java.lang;

import java.lang.String;

public final class Character implements Comparable<java.lang.Character> {
    public static final Class<java.lang.Character> TYPE = Class.getPrimitiveClass("char");
    private final char value;
    public Character(char c) { value = c; }
    public static java.lang.Character valueOf(char c) { return new java.lang.Character(c); }
    public char charValue() { return value; }
    public static boolean isDigit(char c) { return c >= '0' && c <= '9'; }
    public static boolean isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c > 127; }
    public static boolean isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
    public static boolean isWhitespace(char c) { return c == ' ' || (c >= 9 && c <= 13); }
    public static boolean isUpperCase(char c) { return c >= 'A' && c <= 'Z'; }
    public static boolean isLowerCase(char c) { return c >= 'a' && c <= 'z'; }
    public static char toUpperCase(char c) { return isLowerCase(c) ? (char) (c - 32) : c; }
    public static char toLowerCase(char c) { return isUpperCase(c) ? (char) (c + 32) : c; }
    public static int digit(char c, int radix) {
        int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'z' ? c - 'a' + 10 : (c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1));
        return d < radix ? d : -1;
    }
    public static char forDigit(int d, int radix) { return (char) (d < 10 ? '0' + d : 'a' + d - 10); }
    public static int compare(char a, char b) { return a - b; }
    public int compareTo(java.lang.Character o) { return value - o.value; }
    public int hashCode() { return value; }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Character && ((java.lang.Character) o).value == value; }
    public String toString() { return new String(new char[] {value}); }
    public static String toString(char c) { return new String(new char[] {c}); }
}
