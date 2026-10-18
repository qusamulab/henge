/* Native methods of the android.* classes (vm/android/src): OpenGL ES 2, logging, assets, audio.
 * GL and AAudio calls go through the runtime's shim lookup, so they reach the platform's OpenGLES
 * framework and AudioUnit on iOS and do nothing where those are absent (macOS tests). */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *henge_shim_lookup(const char *name);

#define NATIVE(name) static uint64_t name(Thread *t, uint32_t *a)
#define UNUSED (void)t; (void)a

void (*vm_android_log)(int prio, const char *tag, const char *msg);
void (*vm_asset_reader)(const char *name, uint8_t **data, size_t *size);

static Object *obj(Thread *t, Ref r) { return vm_obj(t->vm, r); }

/* Looks a function up once per call site. */
#define FN(var, name) static void *var; if (!var) var = henge_shim_lookup(name); if (!var) return 0

/* ---- logging ---- */

NATIVE(log_write)
{
    char *tag = a[1] ? vm_string_utf8(t->vm, a[1]) : strdup("");
    char *msg = a[2] ? vm_string_utf8(t->vm, a[2]) : strdup("null");
    if (vm_android_log) vm_android_log((int)a[0], tag, msg);
    else fprintf(stderr, "[%s] %s\n", tag, msg);
    free(tag);
    free(msg);
    return 0;
}

/* ---- assets ---- */

NATIVE(asset_read)
{
    if (!a[0] || !vm_asset_reader) return 0;
    char *name = vm_string_utf8(t->vm, a[0]);
    uint8_t *data = NULL;
    size_t size = 0;
    vm_asset_reader(name, &data, &size);
    free(name);
    if (!data) return 0;
    Ref r = vm_new_array(t, vm_array_class(t->vm, "[B"), (uint32_t)size);
    if (!t->exception) memcpy(obj(t, r)->u.bytes, data, size);
    free(data);
    return t->exception ? 0 : r;
}

/* ---- OpenGL ES ---- */

/* pointer to the data a java.nio.Buffer view currently points at */
static void *buffer_ptr(Thread *t, Ref buffer)
{
    static uint32_t f_hb = UINT32_MAX - 1, f_off, f_shift, f_pos;
    if (!buffer) return NULL;
    Object *b = obj(t, buffer);
    if (f_hb == UINT32_MAX - 1) {
        f_hb = vm_field_offset(b->cls, "hb");
        f_off = vm_field_offset(b->cls, "byteOffset");
        f_shift = vm_field_offset(b->cls, "elemShift");
        f_pos = vm_field_offset(b->cls, "position");
    }
    Object *arr = obj(t, b->u.words[f_hb]);
    if (!arr) return NULL;
    return arr->u.bytes + b->u.words[f_off] + ((size_t)b->u.words[f_pos] << b->u.words[f_shift]);
}

static int32_t *int_array(Thread *t, Ref r, uint32_t off) { Object *o = obj(t, r); return o ? (int32_t *)o->u.bytes + off : NULL; }
static float *float_array(Thread *t, Ref r, uint32_t off) { Object *o = obj(t, r); return o ? (float *)o->u.bytes + off : NULL; }

#define GL_V_I1(jn, cn) NATIVE(jn) { FN(p, cn); ((void (*)(int))p)((int)a[0]); (void)t; return 0; }
#define GL_V_I2(jn, cn) NATIVE(jn) { FN(p, cn); ((void (*)(int, int))p)((int)a[0], (int)a[1]); (void)t; return 0; }
#define GL_V_I3(jn, cn) NATIVE(jn) { FN(p, cn); ((void (*)(int, int, int))p)((int)a[0], (int)a[1], (int)a[2]); (void)t; return 0; }
#define GL_V_I4(jn, cn) NATIVE(jn) { FN(p, cn); ((void (*)(int, int, int, int))p)((int)a[0], (int)a[1], (int)a[2], (int)a[3]); (void)t; return 0; }

GL_V_I1(gl_clear, "glClear") GL_V_I1(gl_enable, "glEnable") GL_V_I1(gl_disable, "glDisable")
GL_V_I1(gl_depth_func, "glDepthFunc") GL_V_I1(gl_depth_mask, "glDepthMask") GL_V_I1(gl_cull_face, "glCullFace")
GL_V_I1(gl_front_face, "glFrontFace") GL_V_I1(gl_use_program, "glUseProgram") GL_V_I1(gl_compile_shader, "glCompileShader")
GL_V_I1(gl_link_program, "glLinkProgram") GL_V_I1(gl_delete_shader, "glDeleteShader") GL_V_I1(gl_delete_program, "glDeleteProgram")
GL_V_I1(gl_enable_va, "glEnableVertexAttribArray") GL_V_I1(gl_disable_va, "glDisableVertexAttribArray")
GL_V_I1(gl_active_texture, "glActiveTexture") GL_V_I1(gl_generate_mipmap, "glGenerateMipmap")
GL_V_I2(gl_blend_func, "glBlendFunc") GL_V_I2(gl_attach_shader, "glAttachShader") GL_V_I2(gl_bind_buffer, "glBindBuffer")
GL_V_I2(gl_bind_texture, "glBindTexture") GL_V_I2(gl_pixel_storei, "glPixelStorei") GL_V_I2(gl_uniform1i, "glUniform1i")
GL_V_I3(gl_draw_arrays, "glDrawArrays") GL_V_I3(gl_tex_parameteri, "glTexParameteri")
GL_V_I4(gl_viewport, "glViewport") GL_V_I4(gl_scissor, "glScissor")

NATIVE(gl_finish) { FN(p, "glFinish"); ((void (*)(void))p)(); (void)t; (void)a; return 0; }
NATIVE(gl_flush) { FN(p, "glFlush"); ((void (*)(void))p)(); (void)t; (void)a; return 0; }
NATIVE(gl_get_error) { FN(p, "glGetError"); (void)t; (void)a; return (uint32_t)((int (*)(void))p)(); }
NATIVE(gl_create_program) { FN(p, "glCreateProgram"); (void)t; (void)a; return (uint32_t)((int (*)(void))p)(); }
NATIVE(gl_create_shader) { FN(p, "glCreateShader"); (void)t; return (uint32_t)((int (*)(int))p)((int)a[0]); }

NATIVE(gl_clear_color) { FN(p, "glClearColor"); (void)t; ((void (*)(float, float, float, float))p)(u2f(a[0]), u2f(a[1]), u2f(a[2]), u2f(a[3])); return 0; }
NATIVE(gl_line_width) { FN(p, "glLineWidth"); (void)t; ((void (*)(float))p)(u2f(a[0])); return 0; }
NATIVE(gl_uniform1f) { FN(p, "glUniform1f"); (void)t; ((void (*)(int, float))p)((int)a[0], u2f(a[1])); return 0; }
NATIVE(gl_uniform2f) { FN(p, "glUniform2f"); (void)t; ((void (*)(int, float, float))p)((int)a[0], u2f(a[1]), u2f(a[2])); return 0; }
NATIVE(gl_uniform3f) { FN(p, "glUniform3f"); (void)t; ((void (*)(int, float, float, float))p)((int)a[0], u2f(a[1]), u2f(a[2]), u2f(a[3])); return 0; }
NATIVE(gl_uniform4f) { FN(p, "glUniform4f"); (void)t; ((void (*)(int, float, float, float, float))p)((int)a[0], u2f(a[1]), u2f(a[2]), u2f(a[3]), u2f(a[4])); return 0; }

#define GL_UNIFORM_FV(jn, cn) NATIVE(jn) { FN(p, cn); ((void (*)(int, int, const float *))p)((int)a[0], (int)a[1], float_array(t, a[2], a[3])); return 0; }
GL_UNIFORM_FV(gl_uniform1fv, "glUniform1fv") GL_UNIFORM_FV(gl_uniform2fv, "glUniform2fv")
GL_UNIFORM_FV(gl_uniform3fv, "glUniform3fv") GL_UNIFORM_FV(gl_uniform4fv, "glUniform4fv")

NATIVE(gl_uniform_matrix4fv_arr)
{
    FN(p, "glUniformMatrix4fv");
    ((void (*)(int, int, int, const float *))p)((int)a[0], (int)a[1], (int)(a[2] & 0xff), float_array(t, a[3], a[4]));
    return 0;
}

NATIVE(gl_uniform_matrix4fv_buf)
{
    FN(p, "glUniformMatrix4fv");
    ((void (*)(int, int, int, const void *))p)((int)a[0], (int)a[1], (int)(a[2] & 0xff), buffer_ptr(t, a[3]));
    return 0;
}

NATIVE(gl_shader_source)
{
    FN(p, "glShaderSource");
    char *src = vm_string_utf8(t->vm, a[1]);
    const char *list[1] = {src};
    ((void (*)(int, int, const char **, const int *))p)((int)a[0], 1, list, NULL);
    free(src);
    return 0;
}

NATIVE(gl_get_shaderiv)
{
    FN(p, "glGetShaderiv");
    ((void (*)(int, int, int *))p)((int)a[0], (int)a[1], int_array(t, a[2], a[3]));
    return 0;
}

NATIVE(gl_get_programiv)
{
    FN(p, "glGetProgramiv");
    ((void (*)(int, int, int *))p)((int)a[0], (int)a[1], int_array(t, a[2], a[3]));
    return 0;
}

NATIVE(gl_shader_info_log)
{
    char buf[2048] = "";
    static void *p;
    if (!p) p = henge_shim_lookup("glGetShaderInfoLog");
    if (p) ((void (*)(int, int, int *, char *))p)((int)a[0], sizeof buf, NULL, buf);
    return vm_new_string_utf8(t, buf);
}

NATIVE(gl_program_info_log)
{
    char buf[2048] = "";
    static void *p;
    if (!p) p = henge_shim_lookup("glGetProgramInfoLog");
    if (p) ((void (*)(int, int, int *, char *))p)((int)a[0], sizeof buf, NULL, buf);
    return vm_new_string_utf8(t, buf);
}

NATIVE(gl_get_string)
{
    static void *p;
    if (!p) p = henge_shim_lookup("glGetString");
    const char *s = p ? ((const char *(*)(int))p)((int)a[0]) : NULL;
    return vm_new_string_utf8(t, s ? s : "");
}

NATIVE(gl_get_integerv)
{
    FN(p, "glGetIntegerv");
    ((void (*)(int, int *))p)((int)a[0], int_array(t, a[1], a[2]));
    return 0;
}

NATIVE(gl_get_attrib_location)
{
    FN(p, "glGetAttribLocation");
    char *n = vm_string_utf8(t->vm, a[1]);
    int r = ((int (*)(int, const char *))p)((int)a[0], n);
    free(n);
    return (uint32_t)r;
}

NATIVE(gl_get_uniform_location)
{
    FN(p, "glGetUniformLocation");
    char *n = vm_string_utf8(t->vm, a[1]);
    int r = ((int (*)(int, const char *))p)((int)a[0], n);
    free(n);
    return (uint32_t)r;
}

NATIVE(gl_bind_attrib_location)
{
    FN(p, "glBindAttribLocation");
    char *n = vm_string_utf8(t->vm, a[2]);
    ((void (*)(int, int, const char *))p)((int)a[0], (int)a[1], n);
    free(n);
    return 0;
}

NATIVE(gl_vertex_attrib_pointer_off)
{
    FN(p, "glVertexAttribPointer");
    (void)t;
    ((void (*)(int, int, int, int, int, const void *))p)((int)a[0], (int)a[1], (int)a[2], (int)(a[3] & 0xff), (int)a[4], (const void *)(uintptr_t)a[5]);
    return 0;
}

NATIVE(gl_vertex_attrib_pointer_buf)
{
    FN(p, "glVertexAttribPointer");
    ((void (*)(int, int, int, int, int, const void *))p)((int)a[0], (int)a[1], (int)a[2], (int)(a[3] & 0xff), (int)a[4], buffer_ptr(t, a[5]));
    return 0;
}

NATIVE(gl_draw_elements_off)
{
    FN(p, "glDrawElements");
    (void)t;
    ((void (*)(int, int, int, const void *))p)((int)a[0], (int)a[1], (int)a[2], (const void *)(uintptr_t)a[3]);
    return 0;
}

NATIVE(gl_draw_elements_buf)
{
    FN(p, "glDrawElements");
    ((void (*)(int, int, int, const void *))p)((int)a[0], (int)a[1], (int)a[2], buffer_ptr(t, a[3]));
    return 0;
}

#define GL_GEN(jn, cn) NATIVE(jn) { FN(p, cn); ((void (*)(int, unsigned *))p)((int)a[0], (unsigned *)int_array(t, a[1], a[2])); return 0; }
GL_GEN(gl_gen_buffers, "glGenBuffers") GL_GEN(gl_gen_textures, "glGenTextures")
GL_GEN(gl_delete_buffers, "glDeleteBuffers") GL_GEN(gl_delete_textures, "glDeleteTextures")

NATIVE(gl_buffer_data)
{
    FN(p, "glBufferData");
    ((void (*)(int, long, const void *, int))p)((int)a[0], (long)(int)a[1], buffer_ptr(t, a[2]), (int)a[3]);
    return 0;
}

NATIVE(gl_buffer_sub_data)
{
    FN(p, "glBufferSubData");
    ((void (*)(int, long, long, const void *))p)((int)a[0], (long)(int)a[1], (long)(int)a[2], buffer_ptr(t, a[3]));
    return 0;
}

NATIVE(gl_tex_image2d)
{
    FN(p, "glTexImage2D");
    ((void (*)(int, int, int, int, int, int, int, int, const void *))p)((int)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (int)a[6], (int)a[7], buffer_ptr(t, a[8]));
    return 0;
}

NATIVE(gl_tex_sub_image2d)
{
    FN(p, "glTexSubImage2D");
    ((void (*)(int, int, int, int, int, int, int, int, const void *))p)((int)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (int)a[6], (int)a[7], buffer_ptr(t, a[8]));
    return 0;
}

NATIVE(gl_read_pixels)
{
    FN(p, "glReadPixels");
    ((void (*)(int, int, int, int, int, int, void *))p)((int)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], buffer_ptr(t, a[6]));
    return 0;
}

/* ---- AudioTrack, on the AAudio shim ---- */

NATIVE(audio_create)
{
    UNUSED;
    typedef int (*create_fn)(void **);
    typedef int (*open_fn)(void *, void **);
    void *create = henge_shim_lookup("AAudio_createStreamBuilder");
    void *open = henge_shim_lookup("AAudioStreamBuilder_openStream");
    if (!create || !open) return 0;
    void *builder = NULL, *stream = NULL;
    if (((create_fn)create)(&builder) != 0) return 0;
    ((void (*)(void *, int))henge_shim_lookup("AAudioStreamBuilder_setSampleRate"))(builder, (int)a[0]);
    ((void (*)(void *, int))henge_shim_lookup("AAudioStreamBuilder_setChannelCount"))(builder, (int)a[1]);
    ((void (*)(void *, int))henge_shim_lookup("AAudioStreamBuilder_setFormat"))(builder, (int)a[2]);
    int rc = ((open_fn)open)(builder, &stream);
    ((int (*)(void *))henge_shim_lookup("AAudioStreamBuilder_delete"))(builder);
    return rc == 0 ? (uint64_t)(uintptr_t)stream : 0;
}

NATIVE(audio_start) { UNUSED; void *f = henge_shim_lookup("AAudioStream_requestStart"); if (f) ((int (*)(void *))f)((void *)(uintptr_t)vm_wide(a)); return 0; }
NATIVE(audio_stop) { UNUSED; void *f = henge_shim_lookup("AAudioStream_requestStop"); if (f) ((int (*)(void *))f)((void *)(uintptr_t)vm_wide(a)); return 0; }
NATIVE(audio_close) { UNUSED; void *f = henge_shim_lookup("AAudioStream_close"); if (f) ((int (*)(void *))f)((void *)(uintptr_t)vm_wide(a)); return 0; }

static uint64_t audio_write(Thread *t, uint32_t *a, size_t elem)
{
    void *f = henge_shim_lookup("AAudioStream_write");
    Object *arr = obj(t, a[2]);
    if (!f || !arr) return 0;
    int off = (int)a[3], n = (int)a[4];
    /* the frame count depends on the channel count, which the stream knows */
    void *g = henge_shim_lookup("AAudioStream_getChannelCount");
    int ch = g ? ((int (*)(void *))g)((void *)(uintptr_t)vm_wide(a)) : 1;
    int frames = n / (ch ? ch : 1);
    int done = ((int (*)(void *, const void *, int, long long))f)((void *)(uintptr_t)vm_wide(a), arr->u.bytes + (size_t)off * elem, frames, 1000000000LL);
    return (uint32_t)(done * ch);
}

NATIVE(audio_write_shorts) { return audio_write(t, a, 2); }
NATIVE(audio_write_floats) { return audio_write(t, a, 4); }
NATIVE(audio_write_bytes) { return audio_write(t, a, 1); }

void vm_register_android_natives(VM *vm)
{
#define R(cls, name, proto, fn) vm_register_native(vm, cls, name, proto, fn)
    R("Landroid/util/Log;", "write", "(ILjava/lang/String;Ljava/lang/String;)V", log_write);
    R("Landroid/content/res/AssetManager;", "read", "(Ljava/lang/String;)[B", asset_read);
#define G(name, proto, fn) R("Landroid/opengl/GLES20;", name, proto, fn)
    G("glClear", "(I)V", gl_clear); G("glEnable", "(I)V", gl_enable); G("glDisable", "(I)V", gl_disable);
    G("glDepthFunc", "(I)V", gl_depth_func); G("glDepthMask", "(Z)V", gl_depth_mask); G("glCullFace", "(I)V", gl_cull_face);
    G("glFrontFace", "(I)V", gl_front_face); G("glUseProgram", "(I)V", gl_use_program);
    G("glCompileShader", "(I)V", gl_compile_shader); G("glLinkProgram", "(I)V", gl_link_program);
    G("glDeleteShader", "(I)V", gl_delete_shader); G("glDeleteProgram", "(I)V", gl_delete_program);
    G("glEnableVertexAttribArray", "(I)V", gl_enable_va); G("glDisableVertexAttribArray", "(I)V", gl_disable_va);
    G("glActiveTexture", "(I)V", gl_active_texture); G("glGenerateMipmap", "(I)V", gl_generate_mipmap);
    G("glBlendFunc", "(II)V", gl_blend_func); G("glAttachShader", "(II)V", gl_attach_shader);
    G("glBindBuffer", "(II)V", gl_bind_buffer); G("glBindTexture", "(II)V", gl_bind_texture);
    G("glPixelStorei", "(II)V", gl_pixel_storei); G("glUniform1i", "(II)V", gl_uniform1i);
    G("glDrawArrays", "(III)V", gl_draw_arrays); G("glTexParameteri", "(III)V", gl_tex_parameteri);
    G("glViewport", "(IIII)V", gl_viewport); G("glScissor", "(IIII)V", gl_scissor);
    G("glFinish", "()V", gl_finish); G("glFlush", "()V", gl_flush); G("glGetError", "()I", gl_get_error);
    G("glCreateProgram", "()I", gl_create_program); G("glCreateShader", "(I)I", gl_create_shader);
    G("glClearColor", "(FFFF)V", gl_clear_color); G("glLineWidth", "(F)V", gl_line_width);
    G("glUniform1f", "(IF)V", gl_uniform1f); G("glUniform2f", "(IFF)V", gl_uniform2f);
    G("glUniform3f", "(IFFF)V", gl_uniform3f); G("glUniform4f", "(IFFFF)V", gl_uniform4f);
    G("glUniform1fv", "(II[FI)V", gl_uniform1fv); G("glUniform2fv", "(II[FI)V", gl_uniform2fv);
    G("glUniform3fv", "(II[FI)V", gl_uniform3fv); G("glUniform4fv", "(II[FI)V", gl_uniform4fv);
    G("glUniformMatrix4fv", "(IIZ[FI)V", gl_uniform_matrix4fv_arr);
    G("glUniformMatrix4fv", "(IIZLjava/nio/FloatBuffer;)V", gl_uniform_matrix4fv_buf);
    G("glShaderSource", "(ILjava/lang/String;)V", gl_shader_source);
    G("glGetShaderiv", "(II[II)V", gl_get_shaderiv); G("glGetProgramiv", "(II[II)V", gl_get_programiv);
    G("glGetShaderInfoLog", "(I)Ljava/lang/String;", gl_shader_info_log);
    G("glGetProgramInfoLog", "(I)Ljava/lang/String;", gl_program_info_log);
    G("glGetString", "(I)Ljava/lang/String;", gl_get_string); G("glGetIntegerv", "(I[II)V", gl_get_integerv);
    G("glGetAttribLocation", "(ILjava/lang/String;)I", gl_get_attrib_location);
    G("glGetUniformLocation", "(ILjava/lang/String;)I", gl_get_uniform_location);
    G("glBindAttribLocation", "(IILjava/lang/String;)V", gl_bind_attrib_location);
    G("glVertexAttribPointer", "(IIIZII)V", gl_vertex_attrib_pointer_off);
    G("glVertexAttribPointer", "(IIIZILjava/nio/Buffer;)V", gl_vertex_attrib_pointer_buf);
    G("glDrawElements", "(IIII)V", gl_draw_elements_off);
    G("glDrawElements", "(IIILjava/nio/Buffer;)V", gl_draw_elements_buf);
    G("glGenBuffers", "(I[II)V", gl_gen_buffers); G("glGenTextures", "(I[II)V", gl_gen_textures);
    G("glDeleteBuffers", "(I[II)V", gl_delete_buffers); G("glDeleteTextures", "(I[II)V", gl_delete_textures);
    G("glBufferData", "(IILjava/nio/Buffer;I)V", gl_buffer_data);
    G("glBufferSubData", "(IIILjava/nio/Buffer;)V", gl_buffer_sub_data);
    G("glTexImage2D", "(IIIIIIIILjava/nio/Buffer;)V", gl_tex_image2d);
    G("glTexSubImage2D", "(IIIIIIIILjava/nio/Buffer;)V", gl_tex_sub_image2d);
    G("glReadPixels", "(IIIIIILjava/nio/Buffer;)V", gl_read_pixels);
#define AU(name, proto, fn) R("Landroid/media/AudioTrack;", name, proto, fn)
    AU("nCreate", "(III)J", audio_create); AU("nStart", "(J)V", audio_start); AU("nStop", "(J)V", audio_stop);
    AU("nClose", "(J)V", audio_close);
    AU("nWriteShorts", "(J[SII)I", audio_write_shorts); AU("nWriteFloats", "(J[FII)I", audio_write_floats);
    AU("nWriteBytes", "(J[BII)I", audio_write_bytes);
}
