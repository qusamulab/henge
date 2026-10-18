package android.media;

/** Output only. Backed by the platform's AAudio output stream (natives in vm/natives_android.c). */
public class AudioTrack {
    public static final int MODE_STATIC = 0, MODE_STREAM = 1;
    public static final int PLAYSTATE_STOPPED = 1, PLAYSTATE_PAUSED = 2, PLAYSTATE_PLAYING = 3;
    public static final int STATE_UNINITIALIZED = 0, STATE_INITIALIZED = 1;

    private long stream;
    private final int channels, encoding, sampleRate;
    private int playState = PLAYSTATE_STOPPED;

    public AudioTrack(int streamType, int sampleRateInHz, int channelConfig, int audioFormat, int bufferSizeInBytes, int mode) {
        sampleRate = sampleRateInHz;
        channels = channelConfig == AudioFormat.CHANNEL_OUT_STEREO ? 2 : 1;
        encoding = audioFormat;
        stream = nCreate(sampleRateInHz, channels, audioFormat == AudioFormat.ENCODING_PCM_FLOAT ? 2 : 1);
    }

    public static int getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat) {
        int ch = channelConfig == AudioFormat.CHANNEL_OUT_STEREO ? 2 : 1;
        return sampleRateInHz / 10 * ch * (audioFormat == AudioFormat.ENCODING_PCM_FLOAT ? 4 : 2);
    }

    public int getState() { return stream != 0 ? STATE_INITIALIZED : STATE_UNINITIALIZED; }
    public int getPlayState() { return playState; }
    public void play() { if (stream != 0) nStart(stream); playState = PLAYSTATE_PLAYING; }
    public void pause() { if (stream != 0) nStop(stream); playState = PLAYSTATE_PAUSED; }
    public void stop() { if (stream != 0) nStop(stream); playState = PLAYSTATE_STOPPED; }
    public void flush() {}
    public void release() { if (stream != 0) nClose(stream); stream = 0; }
    public int setVolume(float v) { return 0; }

    public int write(short[] data, int offset, int size) { return stream == 0 ? -1 : nWriteShorts(stream, data, offset, size); }
    public int write(float[] data, int offset, int size, int mode) { return stream == 0 ? -1 : nWriteFloats(stream, data, offset, size); }
    public int write(byte[] data, int offset, int size) { return stream == 0 ? -1 : nWriteBytes(stream, data, offset, size); }

    private static native long nCreate(int sampleRate, int channels, int format);
    private static native void nStart(long s);
    private static native void nStop(long s);
    private static native void nClose(long s);
    private static native int nWriteShorts(long s, short[] d, int off, int n);
    private static native int nWriteFloats(long s, float[] d, int off, int n);
    private static native int nWriteBytes(long s, byte[] d, int off, int n);
}
