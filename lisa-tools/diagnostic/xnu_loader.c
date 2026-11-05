/* Experimental loader: caller must validate the payload and memory map first.
 * It writes kernel RAM and transfers control. It does not access storage. */
#include "payload_validate.h"
#include <stdint.h>

/* xnu-8796: 608. xnu-12377: 1024. */
#ifndef LISA_BOOT_LINE
#define LISA_BOOT_LINE 608
#endif

/* Payload's stored line; the table of segments follows it. */
#ifndef LISA_PAYLOAD_LINE
#define LISA_PAYLOAD_LINE 608
#endif

struct segment { uint64_t va, size, offset, file_size; };
struct payload {
    char magic[8]; uint32_t version, count;
    uint64_t virt, entry, adt_offset, adt_size, kernel_end, phys, memory;
    char command[LISA_PAYLOAD_LINE]; struct segment segments[];
};
struct video { uint64_t base, display, stride, width, height, depth; };
struct boot_args {
    uint16_t revision, version;
    uint64_t virt, phys, memory, top;
    struct video video;
    uint32_t machine;
    uint64_t device_tree;
    uint32_t device_tree_length;
    char command[LISA_BOOT_LINE];
    uint64_t flags, actual_memory;
};
_Static_assert(__builtin_offsetof(struct payload, segments) == 0x48 + LISA_PAYLOAD_LINE, "payload layout");
_Static_assert(__builtin_offsetof(struct boot_args, command) == 108, "boot args layout");
_Static_assert(sizeof(struct boot_args) == 128 + LISA_BOOT_LINE, "boot args size");

static void copy_words(uint64_t dest, uint64_t src, uint64_t bytes)
{
    volatile uint64_t *d = (volatile uint64_t *)(uintptr_t)dest;
    const volatile uint64_t *s = (const volatile uint64_t *)(uintptr_t)src;
    for (uint64_t i = 0; i < (bytes + 7) / 8; ++i) d[i] = s[i];
}

static void clear_words(uint64_t dest, uint64_t bytes)
{
    volatile uint64_t *d = (volatile uint64_t *)(uintptr_t)dest;
    for (uint64_t i = 0; i < bytes / 8; ++i) d[i] = 0;
}

void lisa_xnu_load_and_enter(const void *payload, const struct lisa_payload_layout *layout)
{
    const struct payload *p = payload;
    uint64_t source = (uint64_t)(uintptr_t)p;
    for (uint32_t i = 0; i < p->count; ++i) {
        const struct segment *s = &p->segments[i];
        uint64_t dst = p->phys + (s->va - p->virt);
        uint64_t copied = (s->file_size + 7) & ~7ULL;
        copy_words(dst, source + s->offset, s->file_size);
        clear_words(dst + copied, s->size - copied);
    }
    copy_words(layout->adt_pa, source + p->adt_offset, p->adt_size);
    clear_words(layout->bootargs_pa, 0x4000);
    volatile struct boot_args *ba = (volatile struct boot_args *)(uintptr_t)layout->bootargs_pa;
    ba->revision = 2; ba->version = 2;
    ba->virt = p->virt; ba->phys = p->phys; ba->memory = p->memory; ba->top = layout->top_pa;
    ba->video.base = 0xe1000000ULL; ba->video.display = 1;
    ba->video.stride = 4320; ba->video.width = 1080; ba->video.height = 2400; ba->video.depth = 32;
    ba->device_tree = p->virt + (layout->adt_pa - p->phys);
    ba->device_tree_length = (uint32_t)p->adt_size;
    for (unsigned i = 0; i < sizeof(p->command); ++i) ba->command[i] = p->command[i];
    ba->actual_memory = p->memory;
#ifdef LISA_LOADER_HOST_TEST
    extern void lisa_test_enter(uint64_t, uint64_t);
    lisa_test_enter(layout->entry_pa, layout->bootargs_pa);
#else
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    ((void (*)(uint64_t))(uintptr_t)layout->entry_pa)(layout->bootargs_pa);
#endif
}
