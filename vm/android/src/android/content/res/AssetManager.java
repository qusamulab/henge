package android.content.res;

import java.io.InputStream;

public class AssetManager {
    public InputStream open(String name) throws java.io.IOException {
        byte[] data = read(name);
        if (data == null) throw new java.io.FileNotFoundException(name);
        return new java.io.ByteArrayInputStream(data);
    }
    private static native byte[] read(String name);
}
