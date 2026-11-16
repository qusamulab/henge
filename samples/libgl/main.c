#include <GLES3/gl3.h>

int exercise_gl(void)
{
    glBlitFramebuffer(11, 22, 33, 44, 55, 66, 77, 88, 0x4000, 0x2600);
    glTexSubImage3D(0x806f, 1, 2, 3, 4, 5, 6, 7, 0x1908, 0x1401, (void *)0x12345678);
    glUniform4f(7, 1.25f, -2.5f, 3.75f, 4.0f);
    /* Upper bits of narrow arguments are unspecified in AAPCS64. */
    __asm__ volatile("mov w0, #0x101\nmov w1, #0x100\nmov w2, #0x1ff\nmov w3, #0x100\nbl glColorMask"
                     ::: "x0", "x1", "x2", "x3", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory", "cc");
    return glGetError() == 0x500;
}
