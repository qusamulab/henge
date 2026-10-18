package java.util;

public class Arrays {
    private Arrays() {}

    public static <T> List<T> asList(final T... a) {
        return new AbstractList<T>() {
            public T get(int i) { return a[i]; }
            public T set(int i, T e) { T old = a[i]; a[i] = e; return old; }
            public int size() { return a.length; }
            public Object[] toArray() { Object[] r = new Object[a.length]; System.arraycopy(a, 0, r, 0, a.length); return r; }
        };
    }

    public static int[] copyOf(int[] a, int n) { int[] r = new int[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static long[] copyOf(long[] a, int n) { long[] r = new long[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static float[] copyOf(float[] a, int n) { float[] r = new float[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static double[] copyOf(double[] a, int n) { double[] r = new double[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static char[] copyOf(char[] a, int n) { char[] r = new char[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static byte[] copyOf(byte[] a, int n) { byte[] r = new byte[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static boolean[] copyOf(boolean[] a, int n) { boolean[] r = new boolean[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static <T> T[] copyOf(T[] a, int n) {
        T[] r = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), n);
        System.arraycopy(a, 0, r, 0, Math.min(n, a.length));
        return r;
    }
    public static int[] copyOfRange(int[] a, int from, int to) { int[] r = new int[to - from]; System.arraycopy(a, from, r, 0, Math.min(to, a.length) - from); return r; }
    public static byte[] copyOfRange(byte[] a, int from, int to) { byte[] r = new byte[to - from]; System.arraycopy(a, from, r, 0, Math.min(to, a.length) - from); return r; }
    public static char[] copyOfRange(char[] a, int from, int to) { char[] r = new char[to - from]; System.arraycopy(a, from, r, 0, Math.min(to, a.length) - from); return r; }
    public static float[] copyOfRange(float[] a, int from, int to) { float[] r = new float[to - from]; System.arraycopy(a, from, r, 0, Math.min(to, a.length) - from); return r; }
    public static <T> T[] copyOfRange(T[] a, int from, int to) {
        T[] r = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), to - from);
        System.arraycopy(a, from, r, 0, Math.min(to, a.length) - from);
        return r;
    }

    public static void fill(int[] a, int v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(long[] a, long v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(float[] a, float v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(double[] a, double v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(char[] a, char v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(byte[] a, byte v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(boolean[] a, boolean v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(Object[] a, Object v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(int[] a, int from, int to, int v) { for (int i = from; i < to; i++) a[i] = v; }

    public static void sort(int[] a) { sort(a, 0, a.length); }
    public static void sort(int[] a, int from, int to) {
        for (int i = from + 1; i < to; i++) {
            int v = a[i], j = i - 1;
            while (j >= from && a[j] > v) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    public static void sort(long[] a) { for (int i = 1; i < a.length; i++) { long v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; } }
    public static void sort(float[] a) { for (int i = 1; i < a.length; i++) { float v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; } }
    public static void sort(double[] a) { for (int i = 1; i < a.length; i++) { double v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; } }
    public static void sort(char[] a) { for (int i = 1; i < a.length; i++) { char v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; } }
    public static void sort(Object[] a) { sort(a, 0, a.length, null); }
    public static <T> void sort(T[] a, Comparator<? super T> c) { sort(a, 0, a.length, c); }

    /** Stable merge sort. */
    public static void sort(Object[] a, int from, int to, Comparator c) {
        if (to - from < 2) return;
        Object[] tmp = new Object[to - from];
        mergeSort(a, tmp, from, to, c);
    }

    private static int cmp(Object x, Object y, Comparator c) { return c != null ? c.compare(x, y) : ((Comparable) x).compareTo(y); }

    private static void mergeSort(Object[] a, Object[] tmp, int from, int to, Comparator c) {
        if (to - from < 2) return;
        int mid = (from + to) >>> 1;
        mergeSort(a, tmp, from, mid, c);
        mergeSort(a, tmp, mid, to, c);
        int i = from, j = mid, k = 0;
        while (i < mid && j < to) tmp[k++] = cmp(a[j], a[i], c) < 0 ? a[j++] : a[i++];
        while (i < mid) tmp[k++] = a[i++];
        while (j < to) tmp[k++] = a[j++];
        System.arraycopy(tmp, 0, a, from, k);
    }

    public static int binarySearch(int[] a, int key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) { int mid = (lo + hi) >>> 1; if (a[mid] < key) lo = mid + 1; else if (a[mid] > key) hi = mid - 1; else return mid; }
        return -(lo + 1);
    }

    public static boolean equals(int[] a, int[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static boolean equals(Object[] a, Object[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (!Objects.equals(a[i], b[i])) return false;
        return true;
    }

    public static int hashCode(int[] a) { if (a == null) return 0; int h = 1; for (int i = 0; i < a.length; i++) h = 31 * h + a[i]; return h; }
    public static int hashCode(Object[] a) { if (a == null) return 0; int h = 1; for (int i = 0; i < a.length; i++) h = 31 * h + (a[i] == null ? 0 : a[i].hashCode()); return h; }

    public static String toString(int[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(long[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(float[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(double[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(Object[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(String.valueOf(a[i])); }
        return sb.append(']').toString();
    }
}
