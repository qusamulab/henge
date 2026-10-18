package java.util;

public abstract class AbstractList<E> extends AbstractCollection<E> implements List<E> {
    protected int modCount;

    public abstract E get(int index);

    public boolean add(E e) { add(size(), e); return true; }
    public E set(int index, E element) { throw new UnsupportedOperationException("set"); }
    public void add(int index, E element) { throw new UnsupportedOperationException("add"); }
    public E remove(int index) { throw new UnsupportedOperationException("remove"); }

    public int indexOf(Object o) {
        for (int i = 0, n = size(); i < n; i++) if (Objects.equals(o, get(i))) return i;
        return -1;
    }

    public int lastIndexOf(Object o) {
        for (int i = size() - 1; i >= 0; i--) if (Objects.equals(o, get(i))) return i;
        return -1;
    }

    public boolean addAll(int index, Collection<? extends E> c) {
        boolean changed = false;
        for (Iterator<? extends E> it = c.iterator(); it.hasNext(); ) { add(index++, it.next()); changed = true; }
        return changed;
    }

    public void clear() { for (int i = size() - 1; i >= 0; i--) remove(i); }

    public Iterator<E> iterator() { return listIterator(0); }
    public ListIterator<E> listIterator() { return listIterator(0); }

    public ListIterator<E> listIterator(final int start) {
        return new ListIterator<E>() {
            int cursor = start, last = -1, expected = modCount;
            public boolean hasNext() { return cursor < size(); }
            public E next() {
                if (modCount != expected) throw new ConcurrentModificationException();
                if (cursor >= size()) throw new NoSuchElementException();
                last = cursor;
                return get(cursor++);
            }
            public boolean hasPrevious() { return cursor > 0; }
            public E previous() {
                if (cursor <= 0) throw new NoSuchElementException();
                last = --cursor;
                return get(cursor);
            }
            public int nextIndex() { return cursor; }
            public int previousIndex() { return cursor - 1; }
            public void remove() {
                if (last < 0) throw new IllegalStateException();
                AbstractList.this.remove(last);
                if (last < cursor) cursor--;
                last = -1;
                expected = modCount;
            }
            public void set(E e) { AbstractList.this.set(last, e); }
            public void add(E e) { AbstractList.this.add(cursor++, e); last = -1; expected = modCount; }
        };
    }

    public List<E> subList(int from, int to) {
        if (from < 0 || to > size() || from > to) throw new IndexOutOfBoundsException("from=" + from + " to=" + to);
        final List<E> self = this;
        final int off = from, len = to - from;
        return new AbstractList<E>() {
            public E get(int i) { if (i < 0 || i >= len) throw new IndexOutOfBoundsException("index=" + i); return self.get(off + i); }
            public E set(int i, E e) { return self.set(off + i, e); }
            public int size() { return len; }
        };
    }

    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof List)) return false;
        List<?> other = (List<?>) o;
        if (other.size() != size()) return false;
        for (int i = 0, n = size(); i < n; i++) if (!Objects.equals(get(i), other.get(i))) return false;
        return true;
    }

    public int hashCode() {
        int h = 1;
        for (int i = 0, n = size(); i < n; i++) { E e = get(i); h = 31 * h + (e == null ? 0 : e.hashCode()); }
        return h;
    }
}
