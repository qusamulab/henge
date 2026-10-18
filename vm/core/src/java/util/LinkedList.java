package java.util;

public class LinkedList<E> extends ArrayList<E> implements Deque<E> {
    public LinkedList() {}
    public LinkedList(Collection<? extends E> c) { super(c); }
    public void addFirst(E e) { add(0, e); }
    public void addLast(E e) { add(e); }
    public E removeFirst() { if (isEmpty()) throw new NoSuchElementException(); return remove(0); }
    public E removeLast() { if (isEmpty()) throw new NoSuchElementException(); return remove(size() - 1); }
    public E pollFirst() { return isEmpty() ? null : remove(0); }
    public E pollLast() { return isEmpty() ? null : remove(size() - 1); }
    public E peekFirst() { return isEmpty() ? null : get(0); }
    public E peekLast() { return isEmpty() ? null : get(size() - 1); }
    public E getFirst() { if (isEmpty()) throw new NoSuchElementException(); return get(0); }
    public E getLast() { if (isEmpty()) throw new NoSuchElementException(); return get(size() - 1); }
    public void push(E e) { addFirst(e); }
    public E pop() { return removeFirst(); }
    public boolean offer(E e) { return add(e); }
    public E poll() { return pollFirst(); }
    public E peek() { return peekFirst(); }
    public E remove() { return removeFirst(); }
    public E element() { return getFirst(); }
}
