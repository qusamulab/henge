package android.view;

public class Window {
    public void setFlags(int flags, int mask) {}
    public void addFlags(int flags) {}
    public void clearFlags(int flags) {}
    public void setFormat(int format) {}
    public View getDecorView() { return new View(); }
}
