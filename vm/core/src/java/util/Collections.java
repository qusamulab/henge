package java.util;

public class Collections {
    private Collections() {}

    private static final List EMPTY = new ArrayList(0);

    public static final <T> List<T> emptyList() { return unmodifiableList(EMPTY); }
    public static <T> Set<T> emptySet() { return new HashSet<T>(); }
    public static <K, V> Map<K, V> emptyMap() { return new HashMap<K, V>(); }

    public static <T> List<T> singletonList(final T o) {
        return new AbstractList<T>() {
            public T get(int i) { if (i != 0) throw new IndexOutOfBoundsException("index=" + i); return o; }
            public int size() { return 1; }
        };
    }

    public static <T> List<T> unmodifiableList(final List<? extends T> list) {
        return new AbstractList<T>() {
            public T get(int i) { return list.get(i); }
            public int size() { return list.size(); }
        };
    }

    public static <T> void sort(List<T> list) { list.sort(null); }
    public static <T> void sort(List<T> list, Comparator<? super T> c) { list.sort(c); }

    public static void reverse(List<?> list) {
        List l = list;
        for (int i = 0, j = l.size() - 1; i < j; i++, j--) { Object t = l.get(i); l.set(i, l.get(j)); l.set(j, t); }
    }

    public static void swap(List<?> list, int i, int j) { List l = list; Object t = l.get(i); l.set(i, l.get(j)); l.set(j, t); }

    public static <T> boolean addAll(Collection<? super T> c, T... elements) {
        boolean changed = false;
        for (int i = 0; i < elements.length; i++) if (c.add(elements[i])) changed = true;
        return changed;
    }

    public static <T extends Comparable<? super T>> T max(Collection<? extends T> c) {
        Iterator<? extends T> it = c.iterator();
        T best = it.next();
        while (it.hasNext()) { T v = it.next(); if (v.compareTo(best) > 0) best = v; }
        return best;
    }

    public static <T extends Comparable<? super T>> T min(Collection<? extends T> c) {
        Iterator<? extends T> it = c.iterator();
        T best = it.next();
        while (it.hasNext()) { T v = it.next(); if (v.compareTo(best) < 0) best = v; }
        return best;
    }
}
