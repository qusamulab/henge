package java.util;

public abstract class AbstractSet<E> extends AbstractCollection<E> implements Set<E> {
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Set)) return false;
        Collection<?> c = (Collection<?>) o;
        return c.size() == size() && containsAll(c);
    }

    public int hashCode() {
        int h = 0;
        for (Iterator<E> it = iterator(); it.hasNext(); ) { E e = it.next(); if (e != null) h += e.hashCode(); }
        return h;
    }
}
