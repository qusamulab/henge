// Run by tests/test_vm.c on henge-vm. Prints PASS / FAIL lines; run() returns the failure count.
public class VmTest {
    static int failures;

    static void check(String what, boolean ok) {
        System.out.println((ok ? "PASS  " : "FAIL  ") + what);
        if (!ok) failures++;
    }

    interface Shape { double area(); default String kind() { return "shape"; } }
    static abstract class Base implements Shape {
        static int created;
        final String name;
        Base(String name) { this.name = name; created++; }
        public String toString() { return name + "[" + area() + "]"; }
    }
    static class Circle extends Base {
        double r;
        Circle(double r) { super("circle"); this.r = r; }
        public double area() { return 3.0 * r * r; }
    }
    static class Rect extends Base {
        int w, h;
        Rect(int w, int h) { super("rect"); this.w = w; this.h = h; }
        public double area() { return w * h; }
        public String kind() { return "rect!"; }
    }

    static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

    static class Counter { static int sum; static { sum = 40; } }

    static class MyException extends RuntimeException {
        final int code;
        MyException(String m, int code) { super(m); this.code = code; }
    }

    static int thrower(int x) {
        if (x > 2) throw new MyException("too big: " + x, x);
        return x;
    }

    static String sw(int v) {
        switch (v) {
            case 1: return "one";
            case 2: return "two";
            case 3: return "three";
            case 100: return "hundred";
            default: return "other";
        }
    }

    static String sw2(String s) {
        switch (s) {
            case "a": return "A";
            case "bb": return "B";
            default: return "?";
        }
    }

    public static int run() {
        failures = 0;
        check("fib(20) == 6765", fib(20) == 6765);

        long big = 1L << 40;
        check("long arithmetic", big * 3 + 7 == 3298534883335L);
        check("long shifts", (big >>> 3) == (1L << 37) && (-1L >>> 60) == 15);
        double d = 1.5;
        check("double arithmetic", d * 4 + 0.25 == 6.25);
        check("float", 0.1f + 0.2f > 0.29f && 7 / 2 == 3 && 7 % 3 == 1 && -7 / 2 == -3);
        check("int overflow", Integer.MAX_VALUE + 1 == Integer.MIN_VALUE);
        check("casts", (int) 3.99 == 3 && (int) -3.99 == -3 && (byte) 200 == -56 && (char) 65 == 'A' && (short) 70000 == 4464);
        check("nan to int", (int) (0.0 / 0.0) == 0 && (long) 1e30 == Long.MAX_VALUE);

        int[] arr = new int[10];
        for (int i = 0; i < arr.length; i++) arr[i] = i * i;
        int s = 0;
        for (int v : arr) s += v;
        check("int array sum", s == 285);
        long[] la = {1L, 2L, 3L};
        check("long array", la[0] + la[1] + la[2] == 6);
        String[] names = {"x", "y", "z"};
        check("string array", names.length == 3 && names[2].equals("z"));
        int[][] grid = new int[3][4];
        grid[2][3] = 9;
        check("2d array", grid[2][3] == 9 && grid.length == 3 && grid[0].length == 4);

        String t = "hello";
        t += " " + 42 + " " + 1.5 + " " + true + " " + 'c' + " " + 7L;
        check("string concat", t.equals("hello 42 1.5 true c 7"));
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 5; i++) sb.append(i).append(',');
        check("StringBuilder", sb.toString().equals("0,1,2,3,4,"));
        check("String methods", "Hello".substring(1, 3).equals("el") && "abcabc".indexOf("ca") == 2 && "abc".compareTo("abd") < 0
                && "  x ".trim().equals("x") && "AbC".toLowerCase().equals("abc") && "a-b".replace('-', '+').equals("a+b"));
        check("String hash/equals", "abc".hashCode() == 96354 && "abc".equals(new String("abc")) && "abc" == "abc");
        check("Integer parse/format", Integer.parseInt("-1234") == -1234 && Integer.toString(255, 16).equals("ff")
                && Integer.toHexString(-1).equals("ffffffff") && Long.parseLong("9000000000") == 9000000000L);
        check("Double toString", Double.toString(1.0).equals("1.0") && Double.toString(0.1).equals("0.1")
                && Double.toString(1e10).equals("1.0E10") && Double.toString(123456.789).equals("123456.789"));
        check("Float toString", Float.toString(0.1f).equals("0.1") && Float.toString(3.5f).equals("3.5"));
        check("String.format", String.format("%d-%s-%.2f", 7, "x", 3.14159).equals("7-x-3.14"));
        check("Math", Math.sqrt(16.0) == 4.0 && Math.abs(-5) == 5 && Math.max(3, 9) == 9 && Math.floor(-1.5) == -2.0 && Math.pow(2, 10) == 1024.0);

        Shape[] shapes = {new Circle(2.0), new Rect(3, 4)};
        check("virtual dispatch", shapes[0].area() == 12.0 && shapes[1].area() == 12.0);
        check("interface default", shapes[0].kind().equals("shape") && shapes[1].kind().equals("rect!"));
        check("toString via virtual", shapes[1].toString().equals("rect[12.0]"));
        check("static init", Base.created == 2 && Counter.sum == 40);
        check("instanceof / cast", shapes[0] instanceof Circle && !(shapes[0] instanceof Rect) && ((Base) shapes[1]).name.equals("rect"));

        int caught = 0, fin = 0;
        try { thrower(1); thrower(5); } catch (MyException e) { caught = e.code; } finally { fin++; }
        check("try/catch/finally", caught == 5 && fin == 1);
        try { Object o = "str"; Integer bad = (Integer) o; caught = -1; } catch (ClassCastException e) { caught = 99; }
        check("ClassCastException", caught == 99);
        try { int z = 0; caught = 10 / z; } catch (ArithmeticException e) { caught = 77; }
        check("ArithmeticException", caught == 77);
        try { int[] a2 = new int[2]; a2[2] = 1; } catch (ArrayIndexOutOfBoundsException e) { caught = 55; }
        check("ArrayIndexOutOfBounds", caught == 55);
        try { String n = null; n.length(); } catch (NullPointerException e) { caught = 33; }
        check("NullPointerException", caught == 33);
        try { "abc".charAt(9); } catch (StringIndexOutOfBoundsException e) { caught = 22; }
        check("StringIndexOutOfBounds", caught == 22);
        try { Integer.parseInt("x1"); } catch (NumberFormatException e) { caught = 11; }
        check("NumberFormatException", caught == 11);

        check("switch int", sw(1).equals("one") && sw(3).equals("three") && sw(100).equals("hundred") && sw(5).equals("other"));
        check("switch string", sw2("a").equals("A") && sw2("bb").equals("B") && sw2("c").equals("?"));

        Integer boxed = 1234;
        Object ob = boxed;
        check("boxing", ob.equals(1234) && boxed.intValue() == 1234 && boxed.hashCode() == 1234);
        Runnable r = () -> failures += 0;
        r.run();
        check("lambda", true);
        int[] src = {1, 2, 3, 4, 5};
        int[] dst = new int[5];
        System.arraycopy(src, 1, dst, 0, 4);
        check("arraycopy", dst[0] == 2 && dst[3] == 5);
        check("clone", src.clone().length == 5 && src.clone() != src);
        check("identity hash", System.identityHashCode(src) == System.identityHashCode(src));
        check("getClass().getName()", new Rect(1, 1).getClass().getName().equals("VmTest$Rect") && "x".getClass().getName().equals("java.lang.String"));

        // allocate enough to trigger collections
        long total = 0;
        for (int i = 0; i < 200000; i++) { StringBuilder b2 = new StringBuilder("x"); b2.append(i); total += b2.length(); }
        check("allocation / gc loop", total > 0);
        return failures;
    }
}
