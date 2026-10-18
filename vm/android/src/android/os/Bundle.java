package android.os;

import java.util.HashMap;

public class Bundle {
    private final HashMap<String, Object> map = new HashMap<String, Object>();
    public void putInt(String k, int v) { map.put(k, Integer.valueOf(v)); }
    public void putLong(String k, long v) { map.put(k, Long.valueOf(v)); }
    public void putFloat(String k, float v) { map.put(k, Float.valueOf(v)); }
    public void putBoolean(String k, boolean v) { map.put(k, Boolean.valueOf(v)); }
    public void putString(String k, String v) { map.put(k, v); }
    public int getInt(String k) { return getInt(k, 0); }
    public int getInt(String k, int def) { Object o = map.get(k); return o instanceof Integer ? ((Integer) o).intValue() : def; }
    public long getLong(String k, long def) { Object o = map.get(k); return o instanceof Long ? ((Long) o).longValue() : def; }
    public float getFloat(String k, float def) { Object o = map.get(k); return o instanceof Float ? ((Float) o).floatValue() : def; }
    public boolean getBoolean(String k, boolean def) { Object o = map.get(k); return o instanceof Boolean ? ((Boolean) o).booleanValue() : def; }
    public String getString(String k) { Object o = map.get(k); return o instanceof String ? (String) o : null; }
    public boolean containsKey(String k) { return map.containsKey(k); }
    public int size() { return map.size(); }
}
