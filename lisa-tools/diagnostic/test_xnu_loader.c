#include "payload_validate.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void lisa_xnu_load_and_enter(const void *, const struct lisa_payload_layout *);
static uint64_t seen_entry, seen_args;
void lisa_test_enter(uint64_t entry, uint64_t args) { seen_entry = entry; seen_args = args; }
struct seg { uint64_t va, size, off, file_size; };
struct payload {
    char magic[8]; uint32_t version, count;
    uint64_t virt, entry, adt_off, adt_size, end, phys, memory;
    char command[608]; struct seg segs[1];
};
struct video { uint64_t base, display, stride, width, height, depth; };
struct boot_args {
    uint16_t revision, version; uint64_t virt, phys, memory, top;
    struct video video; uint32_t machine; uint64_t tree; uint32_t tree_length;
    char command[608]; uint64_t flags, actual_memory;
};
int main(void)
{
    unsigned char *ram = malloc(0x10000), *src = calloc(1, 0x400);
    assert(ram && src);
    memset(ram, 0xa5, 0x10000);
    struct payload *p = (void *)src;
    p->virt = 0x100000; p->phys = (uintptr_t)ram; p->memory = 0x10000;
    p->count = 1; p->adt_off = 0x380; p->adt_size = 8;
    p->segs[0] = (struct seg){0x100000, 16, 0x300, 8};
    memcpy(p->command, "test=1", 7);
    memcpy(src + 0x300, "KERNEL!!", 8);
    memcpy(src + 0x380, "TREE!!!!", 8);
    struct lisa_payload_layout layout = {(uintptr_t)ram, (uintptr_t)ram + 0x100,
        (uintptr_t)ram + 0x4000, (uintptr_t)ram + 0x8000};
    lisa_xnu_load_and_enter(p, &layout);
    assert(!memcmp(ram, "KERNEL!!", 8));
    for (int i = 8; i < 16; ++i) assert(ram[i] == 0);
    assert(!memcmp(ram + 0x100, "TREE!!!!", 8));
    struct boot_args *b = (void *)(ram + 0x4000);
    assert(b->revision == 2 && b->version == 2 && b->phys == (uintptr_t)ram);
    assert(b->top == layout.top_pa && b->tree == p->virt + 0x100 && b->tree_length == 8);
    assert(b->video.base == 0xe1000000 && b->video.stride == 4320);
    assert(!strcmp(b->command, "test=1"));
    assert(seen_entry == layout.entry_pa && seen_args == layout.bootargs_pa);
    free(src); free(ram);
}
