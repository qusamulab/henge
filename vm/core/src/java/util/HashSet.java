package java.util;

public class HashSet<E> extends AbstractSet<E> implements Cloneable, java.io.Serializable {
    private final HashMap<E, Object> map;
    private static final Object PRESENT = new Object();

    public HashSet() { map = new HashMap<E, Object>(); }
    public HashSet(int capacity) { map = new HashMap<E, Object>(capacity); }
    public HashSet(Collection<? extends E> c) { map = new HashMap<E, Object>(c.size() * 2); addAll(c); }

    public int size() { return map.size(); }
    public boolean isEmpty() { return map.isEmpty(); }
    public boolean contains(Object o) { return map.containsKey(o); }
    public boolean add(E e) { return map.put(e, PRESENT) == null; }
    public boolean remove(Object o) { return map.remove(o) == PRESENT; }
    public void clear() { map.clear(); }
    public Iterator<E> iterator() { return map.keySet().iterator(); }
    public Object clone() { return new HashSet<E>(this); }
}
