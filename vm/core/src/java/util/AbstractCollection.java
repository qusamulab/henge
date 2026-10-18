package java.util;

public abstract class AbstractCollection<E> implements Collection<E> {
    public abstract Iterator<E> iterator();
    public abstract int size();
    public boolean isEmpty() { return size() == 0; }

    public boolean contains(Object o) {
        for (Iterator<E> it = iterator(); it.hasNext(); ) if (Objects.equals(o, it.next())) return true;
        return false;
    }

    public Object[] toArray() {
        Object[] a = new Object[size()];
        int i = 0;
        for (Iterator<E> it = iterator(); it.hasNext(); ) a[i++] = it.next();
        return a;
    }

    public <T> T[] toArray(T[] a) {
        int n = size();
        if (a.length < n) a = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), n);
        int i = 0;
        for (Iterator<E> it = iterator(); it.hasNext(); ) a[i++] = (T) it.next();
        if (a.length > n) a[n] = null;
        return a;
    }

    public boolean add(E e) { throw new UnsupportedOperationException("add"); }

    public boolean remove(Object o) {
        for (Iterator<E> it = iterator(); it.hasNext(); ) {
            if (Objects.equals(o, it.next())) { it.remove(); return true; }
        }
        return false;
    }

    public boolean containsAll(Collection<?> c) {
        for (Iterator<?> it = c.iterator(); it.hasNext(); ) if (!contains(it.next())) return false;
        return true;
    }

    public boolean addAll(Collection<? extends E> c) {
        boolean changed = false;
        for (Iterator<? extends E> it = c.iterator(); it.hasNext(); ) if (add(it.next())) changed = true;
        return changed;
    }

    public boolean removeAll(Collection<?> c) {
        boolean changed = false;
        for (Iterator<E> it = iterator(); it.hasNext(); ) if (c.contains(it.next())) { it.remove(); changed = true; }
        return changed;
    }

    public boolean retainAll(Collection<?> c) {
        boolean changed = false;
        for (Iterator<E> it = iterator(); it.hasNext(); ) if (!c.contains(it.next())) { it.remove(); changed = true; }
        return changed;
    }

    public void clear() { for (Iterator<E> it = iterator(); it.hasNext(); ) { it.next(); it.remove(); } }

    public String toString() {
        Iterator<E> it = iterator();
        if (!it.hasNext()) return "[]";
        StringBuilder sb = new StringBuilder("[");
        for (;;) {
            E e = it.next();
            sb.append(e == this ? "(this Collection)" : String.valueOf(e));
            if (!it.hasNext()) return sb.append(']').toString();
            sb.append(", ");
        }
    }
}
