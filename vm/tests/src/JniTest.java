public class JniTest {
    public int value;
    public boolean flag;
    public byte small;
    public static long wide;
    public JniTest(int value) { this.value = value; }
    public int plus(int n) { return value + n; }
    public static int callback(int value) { return value + 5; }
    public static void collect() { System.gc(); }
    public static double many(int a, double x, int b, double y, int c, double z, int d, double u,
                              int e, double v, int f, double w, int g, double q, int h, double r,
                              int i, double s, int j, double t) {
        return a+b+c+d+e+f+g+h+i+j+x+y+z+u+v+w+q+r+s+t;
    }
    public static native int nativeTest();
    public static native double mix(int a, double x, int b, double y, int c, double z, int d, double u,
                                     int e, double v, int f, double w, int g, double q, int h, double r,
                                     int i, double s, int j, double t);
    public static native int registered(int n);
    public static native Object identity(Object value);
    public static int run() {
        System.loadLibrary("jnitest");
        if (nativeTest() != 0) return 1;
        if (mix(1,1.5,2,2.5,3,3.5,4,4.5,5,5.5,6,6.5,7,7.5,8,8.5,9,9.5,10,10.5) != 115.0) return 2;
        if (registered(7) != 21) return 3;
        Object o = new JniTest(8);
        if (identity(o) != o) return 4;
        return 0;
    }
}
