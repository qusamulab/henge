package android.opengl;

import java.nio.Buffer;
import java.nio.FloatBuffer;

/** OpenGL ES 2.0: every call is forwarded to the platform's GLES (natives in vm/natives_android.c). */
public class GLES20 {
    public static final int GL_DEPTH_BUFFER_BIT = 0x100, GL_STENCIL_BUFFER_BIT = 0x400, GL_COLOR_BUFFER_BIT = 0x4000;
    public static final int GL_POINTS = 0, GL_LINES = 1, GL_LINE_LOOP = 2, GL_LINE_STRIP = 3, GL_TRIANGLES = 4,
        GL_TRIANGLE_STRIP = 5, GL_TRIANGLE_FAN = 6;
    public static final int GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x300, GL_ONE_MINUS_SRC_COLOR = 0x301,
        GL_SRC_ALPHA = 0x302, GL_ONE_MINUS_SRC_ALPHA = 0x303, GL_DST_ALPHA = 0x304, GL_ONE_MINUS_DST_ALPHA = 0x305;
    public static final int GL_CULL_FACE = 0xb44, GL_BLEND = 0xbe2, GL_DEPTH_TEST = 0xb71, GL_SCISSOR_TEST = 0xc11,
        GL_STENCIL_TEST = 0xb90, GL_DITHER = 0xbd0;
    public static final int GL_FRONT = 0x404, GL_BACK = 0x405, GL_CW = 0x900, GL_CCW = 0x901;
    public static final int GL_LESS = 0x201, GL_LEQUAL = 0x203, GL_ALWAYS = 0x207, GL_EQUAL = 0x202, GL_GREATER = 0x204;
    public static final int GL_BYTE = 0x1400, GL_UNSIGNED_BYTE = 0x1401, GL_SHORT = 0x1402, GL_UNSIGNED_SHORT = 0x1403,
        GL_INT = 0x1404, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406;
    public static final int GL_RGB = 0x1907, GL_RGBA = 0x1908, GL_LUMINANCE = 0x1909, GL_ALPHA = 0x1906;
    public static final int GL_TEXTURE_2D = 0xde1, GL_TEXTURE0 = 0x84c0, GL_TEXTURE_MAG_FILTER = 0x2800,
        GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
        GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812f, GL_REPEAT = 0x2901;
    public static final int GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893, GL_STATIC_DRAW = 0x88e4,
        GL_DYNAMIC_DRAW = 0x88e8, GL_STREAM_DRAW = 0x88e0;
    public static final int GL_FRAGMENT_SHADER = 0x8b30, GL_VERTEX_SHADER = 0x8b31, GL_COMPILE_STATUS = 0x8b81,
        GL_LINK_STATUS = 0x8b82, GL_INFO_LOG_LENGTH = 0x8b84;
    public static final int GL_NO_ERROR = 0, GL_FALSE = 0, GL_TRUE = 1;
    public static final int GL_VERSION = 0x1f02, GL_RENDERER = 0x1f01, GL_VENDOR = 0x1f00, GL_EXTENSIONS = 0x1f03;
    public static final int GL_UNPACK_ALIGNMENT = 0xcf5, GL_FRAMEBUFFER = 0x8d40;

    public static native void glClearColor(float r, float g, float b, float a);
    public static native void glClear(int mask);
    public static native void glViewport(int x, int y, int w, int h);
    public static native void glScissor(int x, int y, int w, int h);
    public static native void glEnable(int cap);
    public static native void glDisable(int cap);
    public static native void glBlendFunc(int src, int dst);
    public static native void glDepthFunc(int f);
    public static native void glDepthMask(boolean flag);
    public static native void glCullFace(int mode);
    public static native void glFrontFace(int mode);
    public static native void glLineWidth(float w);
    public static native int glGetError();
    public static native String glGetString(int name);
    public static native void glGetIntegerv(int pname, int[] params, int offset);
    public static native void glPixelStorei(int pname, int param);
    public static native void glFinish();
    public static native void glFlush();

    public static native int glCreateShader(int type);
    public static native void glShaderSource(int shader, String source);
    public static native void glCompileShader(int shader);
    public static native void glGetShaderiv(int shader, int pname, int[] params, int offset);
    public static native String glGetShaderInfoLog(int shader);
    public static native void glDeleteShader(int shader);
    public static native int glCreateProgram();
    public static native void glAttachShader(int program, int shader);
    public static native void glLinkProgram(int program);
    public static native void glGetProgramiv(int program, int pname, int[] params, int offset);
    public static native String glGetProgramInfoLog(int program);
    public static native void glUseProgram(int program);
    public static native void glDeleteProgram(int program);
    public static native int glGetAttribLocation(int program, String name);
    public static native int glGetUniformLocation(int program, String name);
    public static native void glBindAttribLocation(int program, int index, String name);

    public static native void glUniform1i(int loc, int v);
    public static native void glUniform1f(int loc, float v);
    public static native void glUniform2f(int loc, float x, float y);
    public static native void glUniform3f(int loc, float x, float y, float z);
    public static native void glUniform4f(int loc, float x, float y, float z, float w);
    public static native void glUniform1fv(int loc, int count, float[] v, int offset);
    public static native void glUniform2fv(int loc, int count, float[] v, int offset);
    public static native void glUniform3fv(int loc, int count, float[] v, int offset);
    public static native void glUniform4fv(int loc, int count, float[] v, int offset);
    public static native void glUniformMatrix4fv(int loc, int count, boolean transpose, float[] v, int offset);
    public static native void glUniformMatrix4fv(int loc, int count, boolean transpose, FloatBuffer v);

    public static native void glEnableVertexAttribArray(int index);
    public static native void glDisableVertexAttribArray(int index);
    public static native void glVertexAttribPointer(int index, int size, int type, boolean normalized, int stride, int offset);
    public static native void glVertexAttribPointer(int index, int size, int type, boolean normalized, int stride, Buffer ptr);
    public static native void glDrawArrays(int mode, int first, int count);
    public static native void glDrawElements(int mode, int count, int type, int offset);
    public static native void glDrawElements(int mode, int count, int type, Buffer indices);

    public static native void glGenBuffers(int n, int[] buffers, int offset);
    public static native void glDeleteBuffers(int n, int[] buffers, int offset);
    public static native void glBindBuffer(int target, int buffer);
    public static native void glBufferData(int target, int size, Buffer data, int usage);
    public static native void glBufferSubData(int target, int offset, int size, Buffer data);

    public static native void glGenTextures(int n, int[] textures, int offset);
    public static native void glDeleteTextures(int n, int[] textures, int offset);
    public static native void glBindTexture(int target, int texture);
    public static native void glActiveTexture(int texture);
    public static native void glTexParameteri(int target, int pname, int param);
    public static native void glTexImage2D(int target, int level, int internalformat, int width, int height, int border, int format, int type, Buffer pixels);
    public static native void glTexSubImage2D(int target, int level, int xoffset, int yoffset, int width, int height, int format, int type, Buffer pixels);
    public static native void glGenerateMipmap(int target);
    public static native void glReadPixels(int x, int y, int w, int h, int format, int type, Buffer pixels);
}
