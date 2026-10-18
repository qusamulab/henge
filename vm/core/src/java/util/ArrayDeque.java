package java.util;

public class ArrayDeque<E> extends AbstractCollection<E> implements Deque<E> {
    private Object[] a = new Object[16];
    private int head, size;

    public ArrayDeque() {}
    public ArrayDeque(int capacity) {}
    public ArrayDeque(Collection<? extends E> c) { addAll(c); }

    private void grow() {
        Object[] n = new Object[a.length * 2];
        for (int i = 0; i < size; i++) n[i] = a[(head + i) & (a.length - 1)];
        a = n;
        head = 0;
    }

    public int size() { return size; }
    public void addFirst(E e) {
        if (e == null) throw new NullPointerException();
        if (size == a.length) grow();
        head = (head - 1) & (a.length - 1);
        a[head] = e;
        size++;
    }
    public void addLast(E e) {
        if (e == null) throw new NullPointerException();
        if (size == a.length) grow();
        a[(head + size) & (a.length - 1)] = e;
        size++;
    }
    public boolean add(E e) { addLast(e); return true; }
    public boolean offer(E e) { addLast(e); return true; }
    public E pollFirst() {
        if (size == 0) return null;
        E e = (E) a[head];
        a[head] = null;
        head = (head + 1) & (a.length - 1);
        size--;
        return e;
    }
    public E pollLast() {
        if (size == 0) return null;
        int i = (head + size - 1) & (a.length - 1);
        E e = (E) a[i];
        a[i] = null;
        size--;
        return e;
    }
    public E removeFirst() { E e = pollFirst(); if (e == null) throw new NoSuchElementException(); return e; }
    public E removeLast() { E e = pollLast(); if (e == null) throw new NoSuchElementException(); return e; }
    public E poll() { return pollFirst(); }
    public E remove() { return removeFirst(); }
    public E peekFirst() { return size == 0 ? null : (E) a[head]; }
    public E peekLast() { return size == 0 ? null : (E) a[(head + size - 1) & (a.length - 1)]; }
    public E peek() { return peekFirst(); }
    public E getFirst() { E e = peekFirst(); if (e == null) throw new NoSuchElementException(); return e; }
    public E getLast() { E e = peekLast(); if (e == null) throw new NoSuchElementException(); return e; }
    public E element() { return getFirst(); }
    public void push(E e) { addFirst(e); }
    public E pop() { return removeFirst(); }
    public void clear() { for (int i = 0; i < a.length; i++) a[i] = null; head = 0; size = 0; }

    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int i = 0;
            public boolean hasNext() { return i < size; }
            public E next() { if (i >= size) throw new NoSuchElementException(); return (E) a[(head + i++) & (a.length - 1)]; }
        };
    }
}
