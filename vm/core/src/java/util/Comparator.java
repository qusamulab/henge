package java.util;

public interface Comparator<T> {
    int compare(T a, T b);

    default Comparator<T> reversed() {
        final Comparator<T> self = this;
        return new Comparator<T>() { public int compare(T a, T b) { return self.compare(b, a); } };
    }

    default Comparator<T> thenComparing(final Comparator<? super T> other) {
        final Comparator<T> self = this;
        return new Comparator<T>() {
            public int compare(T a, T b) {
                int r = self.compare(a, b);
                return r != 0 ? r : other.compare(a, b);
            }
        };
    }
}
