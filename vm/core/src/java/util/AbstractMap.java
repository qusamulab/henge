package java.util;

public abstract class AbstractMap<K, V> implements Map<K, V> {
    public abstract Set<Map.Entry<K, V>> entrySet();

    public int size() { return entrySet().size(); }
    public boolean isEmpty() { return size() == 0; }

    public V get(Object key) {
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<K, V> e = it.next();
            if (Objects.equals(key, e.getKey())) return e.getValue();
        }
        return null;
    }

    public boolean containsKey(Object key) {
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); )
            if (Objects.equals(key, it.next().getKey())) return true;
        return false;
    }

    public boolean containsValue(Object value) {
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); )
            if (Objects.equals(value, it.next().getValue())) return true;
        return false;
    }

    public V put(K key, V value) { throw new UnsupportedOperationException("put"); }
    public V remove(Object key) { throw new UnsupportedOperationException("remove"); }
    public void clear() { entrySet().clear(); }

    public void putAll(Map<? extends K, ? extends V> m) {
        for (Iterator it = m.entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<K, V> e = (Map.Entry<K, V>) it.next();
            put(e.getKey(), e.getValue());
        }
    }

    public Set<K> keySet() {
        final Map<K, V> self = this;
        return new AbstractSet<K>() {
            public int size() { return self.size(); }
            public boolean contains(Object o) { return self.containsKey(o); }
            public Iterator<K> iterator() {
                final Iterator<Map.Entry<K, V>> it = self.entrySet().iterator();
                return new Iterator<K>() {
                    public boolean hasNext() { return it.hasNext(); }
                    public K next() { return it.next().getKey(); }
                    public void remove() { it.remove(); }
                };
            }
        };
    }

    public Collection<V> values() {
        final Map<K, V> self = this;
        return new AbstractCollection<V>() {
            public int size() { return self.size(); }
            public Iterator<V> iterator() {
                final Iterator<Map.Entry<K, V>> it = self.entrySet().iterator();
                return new Iterator<V>() {
                    public boolean hasNext() { return it.hasNext(); }
                    public V next() { return it.next().getValue(); }
                    public void remove() { it.remove(); }
                };
            }
        };
    }

    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Map)) return false;
        Map<?, ?> m = (Map<?, ?>) o;
        if (m.size() != size()) return false;
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<K, V> e = it.next();
            if (!Objects.equals(e.getValue(), m.get(e.getKey())) || (e.getValue() == null && !m.containsKey(e.getKey()))) return false;
        }
        return true;
    }

    public int hashCode() {
        int h = 0;
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) h += it.next().hashCode();
        return h;
    }

    public String toString() {
        Iterator<Map.Entry<K, V>> it = entrySet().iterator();
        if (!it.hasNext()) return "{}";
        StringBuilder sb = new StringBuilder("{");
        for (;;) {
            Map.Entry<K, V> e = it.next();
            sb.append(e.getKey() == this ? "(this Map)" : String.valueOf(e.getKey())).append('=');
            sb.append(e.getValue() == this ? "(this Map)" : String.valueOf(e.getValue()));
            if (!it.hasNext()) return sb.append('}').toString();
            sb.append(", ");
        }
    }
}
