package java.util;

import java.util.function.Predicate;

public interface Collection<E> extends Iterable<E> {
    int size();
    boolean isEmpty();
    boolean contains(Object o);
    Object[] toArray();
    <T> T[] toArray(T[] a);
    boolean add(E e);
    boolean remove(Object o);
    boolean containsAll(Collection<?> c);
    boolean addAll(Collection<? extends E> c);
    boolean removeAll(Collection<?> c);
    boolean retainAll(Collection<?> c);
    void clear();

    default boolean removeIf(Predicate<? super E> filter) {
        boolean removed = false;
        Iterator<E> it = iterator();
        while (it.hasNext()) {
            if (filter.test(it.next())) { it.remove(); removed = true; }
        }
        return removed;
    }
}
