package android.content;

public abstract class Context {
    public static final String AUDIO_SERVICE = "audio";
    public String getPackageName() { return PackageInfo.name; }
    public Context getApplicationContext() { return this; }
    public Object getSystemService(String name) { return null; }
    public android.content.res.AssetManager getAssets() { return new android.content.res.AssetManager(); }
    public static class PackageInfo { public static String name = ""; }
}
