package java.util;

public class ArrayList<E> extends AbstractList<E> implements RandomAccess, Cloneable, java.io.Serializable {
    private Object[] data;
    private int size;

    public ArrayList() { data = new Object[10]; }
    public ArrayList(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity " + capacity);
        data = new Object[capacity < 4 ? 4 : capacity];
    }
    public ArrayList(Collection<? extends E> c) { data = new Object[c.size() + 4]; addAll(c); }

    private void grow(int need) {
        if (need > data.length) {
            Object[] n = new Object[need + (need >> 1) + 4];
            System.arraycopy(data, 0, n, 0, size);
            data = n;
        }
    }

    public int size() { return size; }
    public boolean isEmpty() { return size == 0; }
    public void ensureCapacity(int n) { grow(n); }
    public void trimToSize() {}

    private void check(int i) { if (i < 0 || i >= size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size); }

    public E get(int i) { check(i); return (E) data[i]; }
    public E set(int i, E e) { check(i); E old = (E) data[i]; data[i] = e; return old; }

    public boolean add(E e) { grow(size + 1); data[size++] = e; modCount++; return true; }

    public void add(int i, E e) {
        if (i < 0 || i > size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size);
        grow(size + 1);
        System.arraycopy(data, i, data, i + 1, size - i);
        data[i] = e;
        size++;
        modCount++;
    }

    public E remove(int i) {
        check(i);
        E old = (E) data[i];
        System.arraycopy(data, i + 1, data, i, size - i - 1);
        data[--size] = null;
        modCount++;
        return old;
    }

    public boolean remove(Object o) {
        int i = indexOf(o);
        if (i < 0) return false;
        remove(i);
        return true;
    }

    public boolean addAll(Collection<? extends E> c) {
        Object[] a = c.toArray();
        grow(size + a.length);
        System.arraycopy(a, 0, data, size, a.length);
        size += a.length;
        modCount++;
        return a.length != 0;
    }

    public void clear() {
        for (int i = 0; i < size; i++) data[i] = null;
        size = 0;
        modCount++;
    }

    public Object[] toArray() {
        Object[] a = new Object[size];
        System.arraycopy(data, 0, a, 0, size);
        return a;
    }

    public void sort(Comparator<? super E> c) {
        Arrays.sort(data, 0, size, (Comparator) c);
        modCount++;
    }

    public Object clone() { return new ArrayList<E>(this); }
}
