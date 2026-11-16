#include "henge/henge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int seen;
static void blit(int a, int b, int c, int d, int e, int f, int g, int h, unsigned mask, unsigned filter)
{ assert(a == 11 && b == 22 && c == 33 && d == 44 && e == 55 && f == 66 && g == 77 && h == 88 && mask == 0x4000 && filter == 0x2600); seen |= 1; }
static void tex(unsigned target, int level, int x, int y, int z, int w, int h, int d, unsigned format, unsigned type, const void *ptr)
{ assert(target == 0x806f && level == 1 && x == 2 && y == 3 && z == 4 && w == 5 && h == 6 && d == 7 && format == 0x1908 && type == 0x1401 && ptr == (void *)0x12345678); seen |= 2; }
static void uniform(int location, float a, float b, float c, float d)
{ assert(location == 7 && a == 1.25f && b == -2.5f && c == 3.75f && d == 4.0f); seen |= 4; }
static void color(unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{ assert(a == 1 && b == 0 && c == 255 && d == 0); seen |= 8; }
static unsigned error(void) { return 0x500; }
static void *resolve(const char *name)
{
    if (!strcmp(name, "glBlitFramebuffer")) return (void *)blit;
    if (!strcmp(name, "glTexSubImage3D")) return (void *)tex;
    if (!strcmp(name, "glUniform4f")) return (void *)uniform;
    if (!strcmp(name, "glColorMask")) return (void *)color;
    if (!strcmp(name, "glGetError")) return (void *)error;
    return NULL;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    henge_gl_set_backend(resolve);
    assert(henge_shim_provides("glBlitFramebuffer"));
    assert(!henge_shim_provides("glMadeUpExtension"));
    assert(!henge_shim_provides("glDrawArrays")); /* absent backend function */
    char err[512];
    henge_image *im = henge_load(argv[1], err, sizeof err);
    if (!im) { puts(err); return 1; }
    int (*run)(void) = (int (*)(void))henge_dlsym(im, "exercise_gl");
    assert(run && run() == 1 && seen == 15);
    puts("PASS: test_gl (Android stack, pointers, floats, narrow arguments, backend selection)");
    return 0;
}
