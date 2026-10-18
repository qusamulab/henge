package java.util;

public class HashMap<K, V> extends AbstractMap<K, V> implements Cloneable, java.io.Serializable {
    static class Node<K, V> implements Map.Entry<K, V> {
        final int hash;
        final K key;
        V value;
        Node<K, V> next;        // bucket chain
        Node<K, V> before, after; // insertion order
        Node(int hash, K key, V value) { this.hash = hash; this.key = key; this.value = value; }
        public K getKey() { return key; }
        public V getValue() { return value; }
        public V setValue(V v) { V old = value; value = v; return old; }
        public boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return Objects.equals(key, e.getKey()) && Objects.equals(value, e.getValue());
        }
        public int hashCode() { return (key == null ? 0 : key.hashCode()) ^ (value == null ? 0 : value.hashCode()); }
        public String toString() { return key + "=" + value; }
    }

    Node<K, V>[] table;
    int size;
    Node<K, V> head, tail; // all entries, in insertion order (LinkedHashMap's order; HashMap order is unspecified anyway)
    int modCount;

    public HashMap() { table = new Node[16]; }
    public HashMap(int capacity) { int n = 16; while (n < capacity) n <<= 1; table = new Node[n]; }
    public HashMap(Map<? extends K, ? extends V> m) { this(m.size() * 2); putAll(m); }

    static int hash(Object k) { int h = k == null ? 0 : k.hashCode(); return h ^ (h >>> 16); }

    final Node<K, V> getNode(Object key) {
        int h = hash(key);
        for (Node<K, V> n = table[h & (table.length - 1)]; n != null; n = n.next)
            if (n.hash == h && (n.key == key || (key != null && key.equals(n.key)))) return n;
        return null;
    }

    public int size() { return size; }
    public boolean isEmpty() { return size == 0; }
    public boolean containsKey(Object key) { return getNode(key) != null; }
    public V get(Object key) { Node<K, V> n = getNode(key); return n == null ? null : n.value; }

    public V put(K key, V value) {
        int h = hash(key);
        int i = h & (table.length - 1);
        for (Node<K, V> n = table[i]; n != null; n = n.next) {
            if (n.hash == h && (n.key == key || (key != null && key.equals(n.key)))) {
                V old = n.value;
                n.value = value;
                return old;
            }
        }
        Node<K, V> n = new Node<K, V>(h, key, value);
        n.next = table[i];
        table[i] = n;
        if (tail == null) head = n; else { tail.after = n; n.before = tail; }
        tail = n;
        size++;
        modCount++;
        if (size > table.length * 3 / 4) resize();
        return null;
    }

    private void resize() {
        Node<K, V>[] old = table;
        Node<K, V>[] nt = new Node[old.length * 2];
        for (int i = 0; i < old.length; i++) {
            Node<K, V> n = old[i];
            while (n != null) {
                Node<K, V> next = n.next;
                int j = n.hash & (nt.length - 1);
                n.next = nt[j];
                nt[j] = n;
                n = next;
            }
        }
        table = nt;
    }

    public V remove(Object key) {
        int h = hash(key), i = h & (table.length - 1);
        Node<K, V> prev = null;
        for (Node<K, V> n = table[i]; n != null; prev = n, n = n.next) {
            if (n.hash == h && (n.key == key || (key != null && key.equals(n.key)))) {
                if (prev == null) table[i] = n.next; else prev.next = n.next;
                if (n.before == null) head = n.after; else n.before.after = n.after;
                if (n.after == null) tail = n.before; else n.after.before = n.before;
                size--;
                modCount++;
                return n.value;
            }
        }
        return null;
    }

    public void clear() {
        for (int i = 0; i < table.length; i++) table[i] = null;
        head = tail = null;
        size = 0;
        modCount++;
    }

    public boolean containsValue(Object value) {
        for (Node<K, V> n = head; n != null; n = n.after) if (Objects.equals(value, n.value)) return true;
        return false;
    }

    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() { return size; }
            public boolean contains(Object o) {
                if (!(o instanceof Map.Entry)) return false;
                Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
                Node<K, V> n = getNode(e.getKey());
                return n != null && Objects.equals(n.value, e.getValue());
            }
            public void clear() { HashMap.this.clear(); }
            public Iterator<Map.Entry<K, V>> iterator() {
                return new Iterator<Map.Entry<K, V>>() {
                    Node<K, V> next = head, last;
                    int expected = modCount;
                    public boolean hasNext() { return next != null; }
                    public Map.Entry<K, V> next() {
                        if (modCount != expected) throw new ConcurrentModificationException();
                        if (next == null) throw new NoSuchElementException();
                        last = next;
                        next = next.after;
                        return last;
                    }
                    public void remove() {
                        if (last == null) throw new IllegalStateException();
                        HashMap.this.remove(last.key);
                        last = null;
                        expected = modCount;
                    }
                };
            }
        };
    }

    public Object clone() { return new HashMap<K, V>(this); }
}
