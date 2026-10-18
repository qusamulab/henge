package java.lang;

import java.lang.String;

public final class Boolean implements Comparable<java.lang.Boolean> {
    public static final Class<java.lang.Boolean> TYPE = Class.getPrimitiveClass("boolean");
    public static final java.lang.Boolean TRUE = new java.lang.Boolean(true);
    public static final java.lang.Boolean FALSE = new java.lang.Boolean(false);
    private final boolean value;
    public Boolean(boolean v) { value = v; }
    public static java.lang.Boolean valueOf(boolean b) { return b ? TRUE : FALSE; }
    public static boolean parseBoolean(String s) { return s != null && s.equalsIgnoreCase("true"); }
    public boolean booleanValue() { return value; }
    public int compareTo(java.lang.Boolean o) { return value == o.value ? 0 : (value ? 1 : -1); }
    public int hashCode() { return value ? 1231 : 1237; }
    public boolean equals(java.lang.Object o) { return o instanceof java.lang.Boolean && ((java.lang.Boolean) o).value == value; }
    public String toString() { return value ? "true" : "false"; }
    public static String toString(boolean b) { return b ? "true" : "false"; }
    public static int compare(boolean a, boolean b) { return a == b ? 0 : (a ? 1 : -1); }
}
