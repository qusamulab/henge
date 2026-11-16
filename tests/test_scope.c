/* Scopes must not change when an unrelated RTLD_LOCAL library was loaded first. */
#include "../runtime/internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    uint32_t hash[] = {1, 2, 1, 0, 0};
    uint32_t empty[] = {1, 2, 0, 0, 0};
    Elf64_Sym sym[2] = {{0}, {.st_name = 1, .st_info = (STB_GLOBAL << 4) | 2, .st_shndx = 1, .st_value = 100}};
    henge_image root = {.strtab = "\0collision", .strsz = 11, .hash = empty, .symtab = sym};
    henge_image unrelated = root, left = root, right = root, deep = root;
    unrelated.hash = left.hash = right.hash = deep.hash = hash;
    unrelated.base = 1000; left.base = 2000; right.base = 3000; deep.base = 4000;
    hr_images = &unrelated;
    henge_image *children[] = {&left, &right};
    henge_image *grandchild[] = {&deep};
    root.deps = children; root.ndeps = 2;
    left.deps = grandchild; left.ndeps = 1;
    uintptr_t addr;
    assert(hr_resolve_scope(&root, "collision", 1, NULL, NULL, &addr) == 1 && addr == 2100);
    left.hash = empty;
    assert(hr_resolve_scope(&root, "collision", 1, NULL, NULL, &addr) == 1 && addr == 3100);
    unrelated.global = 1;
    assert(hr_resolve_scope(&root, "collision", 1, NULL, NULL, &addr) == 1 && addr == 1100);
    assert(hr_resolve_scope(&root, "collision", 0, NULL, NULL, &addr) == 1 && addr == 3100);
    unrelated.global = 0;
    root.hash = hash;
    assert(hr_resolve_scope(&root, "collision", 1, NULL, NULL, &addr) == 1 && addr == 100);
    root.hash = empty;
    henge_image *shim[] = {NULL};
    root.deps = shim; root.ndeps = 1;
    assert(hr_resolve_scope(&root, "malloc", 1, NULL, NULL, &addr) == 1 && addr);
    assert(hr_resolve_scope(&root, "collision", 1, NULL, NULL, &addr) == 0);

    /* Diamond/cycle deduplication and a dependency beyond the old 256-entry cutoff. */
    henge_image *many = calloc(600, sizeof *many);
    henge_image **links = calloc(600, sizeof *links);
    assert(many && links);
    for (int i = 0; i < 600; i++) { many[i] = root; many[i].deps = NULL; many[i].ndeps = 0; links[i] = &many[i]; }
    root.deps = links; root.ndeps = 600;
    many[599].hash = hash; many[599].base = 9000;
    many[0].deps = (henge_image *[]){&root}; many[0].ndeps = 1;
    assert(hr_resolve_scope(&root, "collision", 1, NULL, NULL, &addr) == 1 && addr == 9100);
    assert(hr_resolve_scope(&root, "missing", 1, NULL, NULL, &addr) == 0);
    free(many); free(links); hr_images = NULL;
    puts("PASS: test_scope (local isolation, globals, breadth-first, system shim, cycles, 600 dependencies)");
    return 0;
}
