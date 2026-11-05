/* Experimental Lisa diagnostic. Never writes storage or peripheral registers.
 * Framebuffer address is a candidate from Linux's FDT, not a confirmed ABL handoff.
 * Do not boot until ABL framebuffer ownership and image packaging are verified.
 */
#include <stdint.h>
#include "../../ios-sweet/shim/fdt.h"
#include "../../ios-sweet/shim/font.h"
#ifdef LISA_PREFLIGHT
#include "fdt_bounded.h"
#include "payload_preflight.h"
#ifdef LISA_XNU_BOOT
#include "payload_tail.h"
void lisa_xnu_load_and_enter(const void *, const struct lisa_payload_layout *);
#endif
#endif
#define FB ((volatile uint32_t *)(uintptr_t)0xe1000000UL)
#define WIDTH 1080
#define HEIGHT 2400
#define SCALE 3
#define READ_REG(name) ({ uint64_t v; __asm__ volatile("mrs %0, " name : "=r"(v)); v; })

/* PSCI SYSTEM_RESET is specified by the live FDT to use SMC. If firmware
 * rejects or fails it, halt; the physical power key remains the fallback. */
static void reset(void)
{
    register uint64_t x0 __asm__("x0") = 0x84000009UL;
    __asm__ volatile("dsb sy\n\tsmc #0" : "+r"(x0) : : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    for (;;) __asm__ volatile("wfe");
}

static void text(unsigned row, const char *s)
{
    unsigned col = 0;
    while (*s && col < 43) {
        unsigned char c = *s++;
        if (c < 32 || c > 126) c = '?';
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 8; ++x)
                for (unsigned sy = 0; sy < SCALE; ++sy)
                    for (unsigned sx = 0; sx < SCALE; ++sx)
                        FB[(24 + row * 48 + y*SCALE + sy)*WIDTH + 24 + col*24 + x*SCALE + sx] =
                            (font8x16[c-32][y] & (0x80u >> x)) ? 0xffffffffu : 0xff001020u;
        ++col;
    }
}

static void hexline(unsigned row, const char *label, uint64_t value)
{
    char line[43]; unsigned i = 0;
    while (*label && i < 24) line[i++] = *label++;
    line[i++] = ' ';
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned n = (value >> shift) & 15;
        line[i++] = n < 10 ? '0'+n : 'A'+n-10;
    }
    line[i] = 0;
    text(row, line);
}

void shim_main(uint64_t fdt)
{
    text(0, "LISA EARLY DIAGNOSTIC");
    hexline(1, "FDT", fdt);
    hexline(2, "CURRENT EL", READ_REG("CurrentEL"));
    hexline(3, "SCTLR", READ_REG("SCTLR_EL1"));
    hexline(4, "MPIDR", READ_REG("MPIDR_EL1"));
    hexline(5, "MMFR0", READ_REG("ID_AA64MMFR0_EL1"));
    hexline(6, "PFR0", READ_REG("ID_AA64PFR0_EL1"));
    hexline(7, "CNTFRQ", READ_REG("CNTFRQ_EL0"));
    uint64_t rd_start = 0, rd_end = 0;
#ifdef LISA_PREFLIGHT
    /* Restrict reads to the first bank observed in the live Lisa FDT.
     * This is an experiment, not proof of accessibility on later boots. */
    const uint64_t bank_start = 0x80000000ULL, bank_end = 0xb9700000ULL;
    size_t fdt_limit = fdt >= bank_start && fdt <= bank_end - 40 ? (size_t)(bank_end - fdt) : 0;
    if (fdt_limit > 16 * 1024 * 1024) fdt_limit = 16 * 1024 * 1024;
    int found = fdt_limit &&
        lisa_fdt_chosen_u64((const void *)(uintptr_t)fdt, fdt_limit, "linux,initrd-start", &rd_start) &&
        lisa_fdt_chosen_u64((const void *)(uintptr_t)fdt, fdt_limit, "linux,initrd-end", &rd_end);
    if (found && rd_start >= bank_start && rd_end > rd_start && rd_end <= bank_end &&
        rd_end - rd_start <= 512ULL * 1024 * 1024) {
#ifndef LISA_RAM_SIZE
#define LISA_RAM_SIZE 0x20000000ULL
#endif
#ifndef LISA_RAM_BASE
#define LISA_RAM_BASE 0x180000000ULL
#endif
        struct lisa_range allowed = {LISA_RAM_BASE, LISA_RAM_SIZE};
        struct lisa_range protected[] = {{0xe1000000ULL, 0x2300000ULL}};
        struct lisa_payload_layout layout;
        hexline(8, "INITRD START", rd_start);
        hexline(9, "INITRD END", rd_end);
        if (!lisa_payload_preflight((const void *)(uintptr_t)rd_start, rd_start,
                (size_t)(rd_end - rd_start), allowed, protected, 1, &layout)) {
#ifdef LISA_XNU_BOOT
            uint64_t payload_size;
            const void *payload = lisa_find_payload((const void *)(uintptr_t)rd_start,
                rd_end - rd_start, &payload_size);
            if (payload) {
                text(10, "LOADING XNU - NO AUTO RESTART");
                __asm__ volatile("dsb sy" ::: "memory");
                lisa_xnu_load_and_enter(payload, &layout);
                text(12, "XNU RETURNED");
            } else text(10, "PAYLOAD LOCATOR FAILED");
#else
            text(10, "PREFLIGHT PASSED - NO XNU BOOT");
            hexline(12, "ENTRY", layout.entry_pa);
#endif
        } else text(10, "PREFLIGHT REJECTED");
    } else text(8, "INITRD OR FDT RANGE INVALID");
#else
    if (fdt_chosen_u64((uintptr_t)fdt, "linux,initrd-start", &rd_start) &&
        fdt_chosen_u64((uintptr_t)fdt, "linux,initrd-end", &rd_end)) {
        hexline(8, "INITRD START", rd_start);
        hexline(9, "INITRD END", rd_end);
    } else {
        text(8, "INITRD LOCATION NOT FOUND");
    }
#endif
#ifdef LISA_SMMU_PROBE
    /* Read only: firmware's SMMUv2 stream-matching view, no DMA or writes.
     * Secure firmware may hide registers; a blocked MMIO read may hang/reset. */
    volatile const uint32_t *smmu = (volatile const uint32_t *)(uintptr_t)0x15000000ULL;
    uint32_t id0 = smmu[0x20 / 4], id1 = smmu[0x24 / 4];
    unsigned count = id0 & 0xffu, ufs_matches = 0, usb_matches = 0;
    uint32_t ufs_smr = 0, ufs_s2cr = 0, usb_smr = 0, usb_s2cr = 0;
    text(13, "SMMU READ ONLY: UFS SID 80 USB SID E0");
    hexline(14, "SMMU ID0", id0);
    hexline(15, "SMMU ID1", id1);
    if ((id0 & (1u << 27)) && count && count <= 128) {
        for (unsigned i = 0; i < count; i++) {
            uint32_t smr = smmu[(0x800 + 4 * i) / 4];
            if (!(smr & (1u << 31))) continue;
            uint32_t mask = (smr >> 16) & 0x7fffu, id = smr & 0xffffu;
            uint32_t s2cr = smmu[(0xc00 + 4 * i) / 4];
            if ((0x80u & ~mask & 0xffffu) == (id & ~mask & 0xffffu)) {
                ufs_matches++; ufs_smr = smr; ufs_s2cr = s2cr;
            }
            if ((0xe0u & ~mask & 0xffffu) == (id & ~mask & 0xffffu)) {
                usb_matches++; usb_smr = smr; usb_s2cr = s2cr;
            }
        }
        hexline(16, "UFS MATCHES", ufs_matches);
        hexline(17, "UFS SMR", ufs_smr);
        hexline(18, "UFS S2CR", ufs_s2cr);
        hexline(19, "USB MATCHES", usb_matches);
        hexline(20, "USB SMR", usb_smr);
        hexline(21, "USB S2CR", usb_s2cr);
        /* Count other valid translating streams sharing UFS's context bank.
         * Do not infer ownership or safety from a zero count. */
        if (ufs_matches == 1 && ((ufs_s2cr >> 16) & 3u) == 0 &&
            (ufs_s2cr & 0xffu) < (id1 & 0xffu)) {
            unsigned shared = 0;
            for (unsigned i = 0; i < count; i++) {
                uint32_t smr = smmu[(0x800 + 4 * i) / 4];
                if (!(smr & (1u << 31))) continue;
                uint32_t mask = (smr >> 16) & 0x7fffu, id = smr & 0xffffu;
                if ((0x80u & ~mask & 0xffffu) == (id & ~mask & 0xffffu)) continue;
                uint32_t s2cr = smmu[(0xc00 + 4 * i) / 4];
                if (((s2cr >> 16) & 3u) == 0 && (s2cr & 0xffu) == (ufs_s2cr & 0xffu)) shared++;
            }
            hexline(22, "UFS SHARED ENTRIES", shared);
            /* Bound context-bank offset against Lisa's 1 MiB SMMU region. */
            unsigned shift = (id1 & (1u << 31)) ? 16 : 12;
            unsigned pages = 1u << (((id1 >> 28) & 7u) + 1);
            uint64_t cb_off = ((uint64_t)pages + (ufs_s2cr & 0xffu)) << shift;
            if (cb_off <= 0x100000u - 0x5cu) {
                hexline(23, "UFS CB SCTLR", smmu[cb_off / 4]);
                hexline(24, "UFS CB FSR", smmu[(cb_off + 0x58) / 4]);
                uint64_t gr1_off = (uint64_t)1 << shift;
                hexline(25, "UFS CBAR", smmu[(gr1_off + 4 * (ufs_s2cr & 0xffu)) / 4]);
            } else text(23, "UFS CONTEXT OUTSIDE REGION");
        }
    } else text(16, "SMMU TABLE NOT ACCESSIBLE OR UNSUPPORTED");
#endif
#ifdef LISA_UFS_PROBE
    /* Status reads only: no controller setup, DMA, SCSI or storage writes.
     * A gated or inaccessible block may still stall the CPU. */
    volatile const uint32_t *ufs = (volatile const uint32_t *)(uintptr_t)0x01d84000ULL;
    text(13, "UFS STATUS READ ONLY");
    hexline(14, "UFS CAP", ufs[0x00 / 4]);
    hexline(15, "UFS HCS", ufs[0x30 / 4]);
    hexline(16, "UFS HCE", ufs[0x34 / 4]);
    hexline(17, "UFS DOORBELL", ufs[0x58 / 4]);
#endif
    text(11, "AUTO RESTART IN ABOUT 30 SECONDS");
    __asm__ volatile("dsb sy" ::: "memory");
    uint64_t frequency = READ_REG("CNTFRQ_EL0");
    uint64_t start = READ_REG("CNTPCT_EL0");
    /* Reject nonsensical frequency readings instead of waiting indefinitely. */
    if (frequency >= 1000000 && frequency <= 1000000000) {
        while (READ_REG("CNTPCT_EL0") - start < frequency * 30) {}
    }
    reset();
}

void secondary_main(uint64_t unused) { (void)unused; for (;;) __asm__ volatile("wfe"); }
void exc_handler(uint64_t slot, uint64_t *regs) { (void)slot; (void)regs; reset(); }
