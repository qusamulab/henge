package java.util;

public class PriorityQueue<E> extends AbstractCollection<E> implements Queue<E> {
    private Object[] q = new Object[16];
    private int size;
    private final Comparator<? super E> cmp;

    public PriorityQueue() { cmp = null; }
    public PriorityQueue(int capacity) { cmp = null; }
    public PriorityQueue(Comparator<? super E> c) { cmp = c; }
    public PriorityQueue(int capacity, Comparator<? super E> c) { cmp = c; }

    private int compare(Object a, Object b) { return cmp != null ? ((Comparator) cmp).compare(a, b) : ((Comparable) a).compareTo(b); }

    public int size() { return size; }
    public boolean add(E e) { return offer(e); }

    public boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        if (size == q.length) { Object[] n = new Object[size * 2]; System.arraycopy(q, 0, n, 0, size); q = n; }
        int i = size++;
        while (i > 0) {
            int p = (i - 1) >>> 1;
            if (compare(e, q[p]) >= 0) break;
            q[i] = q[p];
            i = p;
        }
        q[i] = e;
        return true;
    }

    public E peek() { return size == 0 ? null : (E) q[0]; }
    public E element() { E e = peek(); if (e == null) throw new NoSuchElementException(); return e; }
    public E remove() { E e = poll(); if (e == null) throw new NoSuchElementException(); return e; }

    public E poll() {
        if (size == 0) return null;
        E top = (E) q[0];
        Object last = q[--size];
        q[size] = null;
        if (size > 0) {
            int i = 0;
            for (;;) {
                int c = 2 * i + 1;
                if (c >= size) break;
                if (c + 1 < size && compare(q[c + 1], q[c]) < 0) c++;
                if (compare(last, q[c]) <= 0) break;
                q[i] = q[c];
                i = c;
            }
            q[i] = last;
        }
        return top;
    }

    public void clear() { for (int i = 0; i < size; i++) q[i] = null; size = 0; }

    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int i = 0;
            public boolean hasNext() { return i < size; }
            public E next() { if (i >= size) throw new NoSuchElementException(); return (E) q[i++]; }
        };
    }
}
