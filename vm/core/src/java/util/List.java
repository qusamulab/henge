package java.util;

import java.util.function.UnaryOperator;

public interface List<E> extends Collection<E> {
    E get(int index);
    E set(int index, E element);
    void add(int index, E element);
    E remove(int index);
    int indexOf(Object o);
    int lastIndexOf(Object o);
    ListIterator<E> listIterator();
    ListIterator<E> listIterator(int index);
    List<E> subList(int from, int to);
    boolean addAll(int index, Collection<? extends E> c);

    default void sort(Comparator<? super E> c) {
        Object[] a = toArray();
        Arrays.sort(a, (Comparator) c);
        for (int i = 0; i < a.length; i++) set(i, (E) a[i]);
    }

    default void replaceAll(UnaryOperator<E> op) {
        for (int i = 0; i < size(); i++) set(i, op.apply(get(i)));
    }
}
