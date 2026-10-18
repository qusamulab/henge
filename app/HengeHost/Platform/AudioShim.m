/* AAudio for Android code, on top of an AudioUnit (RemoteIO) output.
 *
 * Output streams only. With a data callback the audio thread pulls frames straight from the app's
 * callback; without one, AAudioStream_write feeds a ring buffer. Formats: 16-bit PCM and float.
 */
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "henge/henge.h"

enum {
    AAUDIO_OK = 0,
    AAUDIO_ERROR_INVALID_STATE = -895,
    AAUDIO_ERROR_INVALID_FORMAT = -892,
    AAUDIO_ERROR_UNAVAILABLE = -886,
    AAUDIO_ERROR_UNIMPLEMENTED = -890,
    AAUDIO_ERROR_NULL = -881,
};
enum { FORMAT_UNSPECIFIED = 0, FORMAT_I16 = 1, FORMAT_FLOAT = 2 };
enum { STATE_UNINITIALIZED = 0, STATE_OPEN = 2, STATE_STARTING = 3, STATE_STARTED = 4, STATE_PAUSED = 6,
       STATE_STOPPED = 10, STATE_CLOSED = 12 };
enum { CALLBACK_CONTINUE = 0, CALLBACK_STOP = 1 };

typedef int32_t (*DataCallback)(void *stream, void *user, void *audio, int32_t frames);
typedef void (*ErrorCallback)(void *stream, void *user, int32_t error);

typedef struct {
    int32_t sample_rate, channels, format, frames_per_callback, direction, buffer_capacity;
    DataCallback data_cb;
    void *data_user;
    ErrorCallback error_cb;
    void *error_user;
} Builder;

typedef struct Stream {
    Builder b;
    AudioUnit unit;
    _Atomic int state;
    _Atomic int64_t frames_done;
    int bytes_per_frame;
    /* write mode: single-producer single-consumer ring of bytes */
    uint8_t *ring;
    size_t ring_size;
    _Atomic size_t head, tail;
} Stream;

static int32_t he_createBuilder(Builder **out)
{
    Builder *b = calloc(1, sizeof *b);
    if (!b) return AAUDIO_ERROR_UNAVAILABLE;
    b->channels = 2;
    *out = b;
    return AAUDIO_OK;
}

static int32_t he_deleteBuilder(Builder *b) { free(b); return AAUDIO_OK; }

#define SETTER(fn, field) static void fn(Builder *b, int32_t v) { b->field = v; }
SETTER(he_setSampleRate, sample_rate)
SETTER(he_setChannelCount, channels)
SETTER(he_setFormat, format)
SETTER(he_setFramesPerCallback, frames_per_callback)
SETTER(he_setDirection, direction)
SETTER(he_setBufferCapacity, buffer_capacity)
static void he_ignoreInt(Builder *b, int32_t v) { (void)b; (void)v; }
static void he_setDataCallback(Builder *b, DataCallback cb, void *user) { b->data_cb = cb; b->data_user = user; }
static void he_setErrorCallback(Builder *b, ErrorCallback cb, void *user) { b->error_cb = cb; b->error_user = user; }

static OSStatus render(void *refcon, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                       UInt32 frames, AudioBufferList *io)
{
    (void)flags; (void)ts; (void)bus;
    Stream *s = refcon;
    void *out = io->mBuffers[0].mData;
    size_t bytes = (size_t)frames * s->bytes_per_frame;
    if (atomic_load(&s->state) != STATE_STARTED) {
        memset(out, 0, bytes);
        return noErr;
    }
    if (s->b.data_cb) {
        int32_t r = s->b.data_cb(s, s->b.data_user, out, (int32_t)frames);
        atomic_fetch_add(&s->frames_done, frames);
        if (r == CALLBACK_STOP) atomic_store(&s->state, STATE_STOPPED);
        return noErr;
    }
    size_t head = atomic_load_explicit(&s->head, memory_order_acquire), tail = s->tail;
    size_t avail = head - tail, n = avail < bytes ? avail : bytes;
    for (size_t i = 0; i < n; i++) ((uint8_t *)out)[i] = s->ring[(tail + i) % s->ring_size];
    memset((uint8_t *)out + n, 0, bytes - n);
    atomic_store_explicit(&s->tail, tail + n, memory_order_release);
    atomic_fetch_add(&s->frames_done, frames);
    return noErr;
}

static int32_t he_openStream(Builder *b, Stream **out)
{
    if (b->direction != 0) return AAUDIO_ERROR_UNIMPLEMENTED; /* output only */
    Stream *s = calloc(1, sizeof *s);
    if (!s) return AAUDIO_ERROR_UNAVAILABLE;
    s->b = *b;
    if (s->b.format == FORMAT_UNSPECIFIED) s->b.format = FORMAT_FLOAT;
    if (s->b.sample_rate <= 0) s->b.sample_rate = 48000;
    if (s->b.channels <= 0) s->b.channels = 2;
    if (s->b.format != FORMAT_I16 && s->b.format != FORMAT_FLOAT) { free(s); return AAUDIO_ERROR_INVALID_FORMAT; }
    s->bytes_per_frame = s->b.channels * (s->b.format == FORMAT_FLOAT ? 4 : 2);

    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_RemoteIO, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &d);
    if (!comp || AudioComponentInstanceNew(comp, &s->unit) != noErr) { free(s); return AAUDIO_ERROR_UNAVAILABLE; }
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = s->b.sample_rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = (s->b.format == FORMAT_FLOAT ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger) | kAudioFormatFlagIsPacked;
    f.mChannelsPerFrame = (UInt32)s->b.channels;
    f.mBitsPerChannel = s->b.format == FORMAT_FLOAT ? 32 : 16;
    f.mBytesPerFrame = (UInt32)s->bytes_per_frame;
    f.mFramesPerPacket = 1;
    f.mBytesPerPacket = f.mBytesPerFrame;
    AURenderCallbackStruct cb = {render, s};
    if (AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) != noErr ||
        AudioUnitSetProperty(s->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr ||
        AudioUnitInitialize(s->unit) != noErr) {
        AudioComponentInstanceDispose(s->unit);
        free(s);
        return AAUDIO_ERROR_UNAVAILABLE;
    }
    if (!s->b.data_cb) {
        s->ring_size = (size_t)s->b.sample_rate * s->bytes_per_frame / 2; /* half a second */
        s->ring = calloc(1, s->ring_size);
    }
    atomic_store(&s->state, STATE_OPEN);
    *out = s;
    return AAUDIO_OK;
}

static int32_t he_requestStart(Stream *s)
{
    NSError *e = nil;
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:&e];
    [[AVAudioSession sharedInstance] setActive:YES error:&e];
    atomic_store(&s->state, STATE_STARTED);
    return AudioOutputUnitStart(s->unit) == noErr ? AAUDIO_OK : AAUDIO_ERROR_UNAVAILABLE;
}

static int32_t he_requestStop(Stream *s)
{
    atomic_store(&s->state, STATE_STOPPED);
    AudioOutputUnitStop(s->unit);
    return AAUDIO_OK;
}

static int32_t he_requestPause(Stream *s)
{
    atomic_store(&s->state, STATE_PAUSED);
    AudioOutputUnitStop(s->unit);
    return AAUDIO_OK;
}

static int32_t he_requestFlush(Stream *s) { (void)s; return AAUDIO_OK; }

static int32_t he_close(Stream *s)
{
    if (!s) return AAUDIO_OK;
    AudioOutputUnitStop(s->unit);
    AudioUnitUninitialize(s->unit);
    AudioComponentInstanceDispose(s->unit);
    free(s->ring);
    free(s);
    return AAUDIO_OK;
}

/* Blocking write (no data callback): copies into the ring, waiting up to timeout for space. */
static int32_t he_write(Stream *s, const void *buf, int32_t frames, int64_t timeout_ns)
{
    if (!s->ring) return AAUDIO_ERROR_INVALID_STATE;
    size_t bytes = (size_t)frames * s->bytes_per_frame, done = 0;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (done < bytes) {
        size_t head = s->head, tail = atomic_load_explicit(&s->tail, memory_order_acquire);
        size_t room = s->ring_size - (head - tail), n = bytes - done < room ? bytes - done : room;
        for (size_t i = 0; i < n; i++) s->ring[(head + i) % s->ring_size] = ((const uint8_t *)buf)[done + i];
        atomic_store_explicit(&s->head, head + n, memory_order_release);
        done += n;
        if (done == bytes) break;
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        int64_t waited = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000 + (t1.tv_nsec - t0.tv_nsec);
        if (waited >= timeout_ns) break;
        usleep(2000);
    }
    return (int32_t)(done / s->bytes_per_frame);
}

static int32_t he_getSampleRate(Stream *s) { return s->b.sample_rate; }
static int32_t he_getChannelCount(Stream *s) { return s->b.channels; }
static int32_t he_getFormat(Stream *s) { return s->b.format; }
static int32_t he_getFramesPerBurst(Stream *s) { (void)s; return 256; }
static int32_t he_getFramesPerCallback(Stream *s) { return s->b.frames_per_callback; }
static int32_t he_getBufferSize(Stream *s) { return s->b.buffer_capacity > 0 ? s->b.buffer_capacity : 1024; }
static int32_t he_setBufferSize(Stream *s, int32_t n) { (void)s; return n; }
static int32_t he_getState(Stream *s) { return atomic_load(&s->state); }
static int32_t he_zero(Stream *s) { (void)s; return 0; }
static int32_t he_getDirection(Stream *s) { (void)s; return 0; }
static int64_t he_getFramesWritten(Stream *s) { return atomic_load(&s->frames_done); }
static int32_t he_waitForStateChange(Stream *s, int32_t from, int32_t *next, int64_t timeout)
{
    (void)timeout;
    if (next) *next = atomic_load(&s->state);
    (void)from;
    return AAUDIO_OK;
}
static const char *he_resultText(int32_t r) { return r == AAUDIO_OK ? "AAUDIO_OK" : "AAUDIO_ERROR"; }

#define E(n, f) {n, (void *)(f)}
static const struct henge_shim_entry k_aaudio[] = {
    E("AAudio_createStreamBuilder", he_createBuilder), E("AAudioStreamBuilder_delete", he_deleteBuilder),
    E("AAudioStreamBuilder_setSampleRate", he_setSampleRate), E("AAudioStreamBuilder_setChannelCount", he_setChannelCount),
    E("AAudioStreamBuilder_setSamplesPerFrame", he_setChannelCount), E("AAudioStreamBuilder_setFormat", he_setFormat),
    E("AAudioStreamBuilder_setFramesPerDataCallback", he_setFramesPerCallback),
    E("AAudioStreamBuilder_setDirection", he_setDirection), E("AAudioStreamBuilder_setBufferCapacityInFrames", he_setBufferCapacity),
    E("AAudioStreamBuilder_setDeviceId", he_ignoreInt), E("AAudioStreamBuilder_setSharingMode", he_ignoreInt),
    E("AAudioStreamBuilder_setPerformanceMode", he_ignoreInt), E("AAudioStreamBuilder_setUsage", he_ignoreInt),
    E("AAudioStreamBuilder_setContentType", he_ignoreInt), E("AAudioStreamBuilder_setInputPreset", he_ignoreInt),
    E("AAudioStreamBuilder_setSessionId", he_ignoreInt),
    E("AAudioStreamBuilder_setDataCallback", he_setDataCallback), E("AAudioStreamBuilder_setErrorCallback", he_setErrorCallback),
    E("AAudioStreamBuilder_openStream", he_openStream),
    E("AAudioStream_requestStart", he_requestStart), E("AAudioStream_requestStop", he_requestStop),
    E("AAudioStream_requestPause", he_requestPause), E("AAudioStream_requestFlush", he_requestFlush),
    E("AAudioStream_close", he_close), E("AAudioStream_write", he_write),
    E("AAudioStream_getSampleRate", he_getSampleRate), E("AAudioStream_getChannelCount", he_getChannelCount),
    E("AAudioStream_getSamplesPerFrame", he_getChannelCount), E("AAudioStream_getFormat", he_getFormat),
    E("AAudioStream_getFramesPerBurst", he_getFramesPerBurst), E("AAudioStream_getFramesPerDataCallback", he_getFramesPerCallback),
    E("AAudioStream_getBufferSizeInFrames", he_getBufferSize), E("AAudioStream_getBufferCapacityInFrames", he_getBufferSize),
    E("AAudioStream_setBufferSizeInFrames", he_setBufferSize), E("AAudioStream_getState", he_getState),
    E("AAudioStream_getXRunCount", he_zero), E("AAudioStream_getPerformanceMode", he_zero),
    E("AAudioStream_getSharingMode", he_zero), E("AAudioStream_getDeviceId", he_zero),
    E("AAudioStream_getDirection", he_getDirection), E("AAudioStream_getFramesWritten", he_getFramesWritten),
    E("AAudioStream_getFramesRead", he_getFramesWritten), E("AAudioStream_waitForStateChange", he_waitForStateChange),
    E("AAudio_convertResultToText", he_resultText),
};
#undef E

void henge_register_audio_shims(void)
{
    henge_shim_register(k_aaudio, sizeof k_aaudio / sizeof k_aaudio[0]);
}
