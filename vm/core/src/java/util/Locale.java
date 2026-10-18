package java.util;

public final class Locale {
    public static final Locale ROOT = new Locale("");
    public static final Locale ENGLISH = new Locale("en");
    public static final Locale US = new Locale("en_US");
    public static final Locale JAPAN = new Locale("ja_JP");
    private final String tag;
    public Locale(String tag) { this.tag = tag; }
    public static Locale getDefault() { return US; }
    public String toString() { return tag; }
}
