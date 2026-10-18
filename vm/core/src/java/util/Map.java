package java.util;

import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;

public interface Map<K, V> {
    interface Entry<K, V> {
        K getKey();
        V getValue();
        V setValue(V value);
    }

    int size();
    boolean isEmpty();
    boolean containsKey(Object key);
    boolean containsValue(Object value);
    V get(Object key);
    V put(K key, V value);
    V remove(Object key);
    void putAll(Map<? extends K, ? extends V> m);
    void clear();
    Set<K> keySet();
    Collection<V> values();
    Set<Map.Entry<K, V>> entrySet();

    default V getOrDefault(Object key, V def) {
        V v = get(key);
        return v != null || containsKey(key) ? v : def;
    }

    default V putIfAbsent(K key, V value) {
        V v = get(key);
        if (v == null) v = put(key, value);
        return v;
    }

    default V computeIfAbsent(K key, Function<? super K, ? extends V> f) {
        V v = get(key);
        if (v == null) {
            v = f.apply(key);
            if (v != null) put(key, v);
        }
        return v;
    }

    default V computeIfPresent(K key, BiFunction<? super K, ? super V, ? extends V> f) {
        V old = get(key);
        if (old == null) return null;
        V v = f.apply(key, old);
        if (v != null) put(key, v); else remove(key);
        return v;
    }

    default V compute(K key, BiFunction<? super K, ? super V, ? extends V> f) {
        V v = f.apply(key, get(key));
        if (v == null) remove(key); else put(key, v);
        return v;
    }

    default V merge(K key, V value, BiFunction<? super V, ? super V, ? extends V> f) {
        V old = get(key);
        V v = old == null ? value : f.apply(old, value);
        if (v == null) remove(key); else put(key, v);
        return v;
    }

    default void forEach(BiConsumer<? super K, ? super V> action) {
        for (Iterator<Map.Entry<K, V>> it = entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<K, V> e = it.next();
            action.accept(e.getKey(), e.getValue());
        }
    }
}
