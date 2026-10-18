package java.util;

public class LinkedHashMap<K, V> extends HashMap<K, V> {
    public LinkedHashMap() {}
    public LinkedHashMap(int capacity) { super(capacity); }
    public LinkedHashMap(Map<? extends K, ? extends V> m) { super(m); }
}
