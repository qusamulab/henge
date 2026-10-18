package java.util;

public class LinkedHashSet<E> extends HashSet<E> {
    public LinkedHashSet() {}
    public LinkedHashSet(int capacity) { super(capacity); }
    public LinkedHashSet(Collection<? extends E> c) { super(c); }
}
