#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <libkern/libkern.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define LISA_GPU_FW_MAGIC "LISAGPUF"
#define LISA_GPU_FW_ALIGN 0x4000u

struct lisa_gpu_fw_header { char magic[8]; uint32_t version, count; } __attribute__((packed));
/* Matches mk_lisa_gpu_firmware.py ENTRY (<I64sQ4x), exactly 80 bytes. */
struct lisa_gpu_fw_entry {
	uint32_t name_len;
	char name[64];
	uint64_t size;
	uint8_t reserved[4];
} __attribute__((packed));

_Static_assert(sizeof(struct lisa_gpu_fw_header) == 16, "Lisa GPU firmware header ABI");
_Static_assert(sizeof(struct lisa_gpu_fw_entry) == 80, "Lisa GPU firmware entry ABI");

static uint32_t
lisa_gpu_load32(const volatile uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
lisa_gpu_load64(const volatile uint8_t *p)
{
	uint64_t value = 0;
	for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (i * 8);
	return value;
}

static int
lisa_gpu_name_equals(const volatile uint8_t *name, uint32_t name_len, const char *wanted)
{
	if (name_len == 0 || name_len >= 64) return 0;
	for (uint32_t i = 0; i < name_len; ++i) if ((uint8_t)wanted[i] != name[i]) return 0;
	return wanted[name_len] == '\0';
}

const uint8_t *
lisa_gpu_firmware_file(const char *wanted, uint64_t *length)
{
	DTEntry entry;
	const void *prop;
	unsigned size;
	uint64_t range[2], off;
	const volatile uint8_t *base, *cursor;
	uint32_t count;

	if (!wanted || !length || SecureDTLookupEntry(NULL, "/chosen/memory-map", &entry) != kSuccess ||
	    SecureDTGetProperty(entry, "LisaGPUFirmware", &prop, &size) != kSuccess || size != sizeof(range)) return NULL;
	memcpy(range, prop, sizeof(range));
	if (range[1] < 16 || range[1] > 64 * 1024 * 1024) return NULL;
	base = (const volatile uint8_t *)(uintptr_t)ml_io_map((vm_offset_t)range[0], (vm_size_t)range[1]);
	if (!base) return NULL;
	for (unsigned i = 0; i < 8; ++i) if (base[i] != (uint8_t)LISA_GPU_FW_MAGIC[i]) return NULL;
	if (lisa_gpu_load32(base + 8) != 1) return NULL;
	count = lisa_gpu_load32(base + 12);
	if (count > 16 || range[1] < 16 + (uint64_t)count * 80) return NULL;
	cursor = base + 16;
	off = ((uint64_t)(16 + count * 80) + LISA_GPU_FW_ALIGN - 1) & ~(uint64_t)(LISA_GPU_FW_ALIGN - 1);
	for (uint32_t i = 0; i < count; ++i, cursor += 80) {
		uint32_t name_len = lisa_gpu_load32(cursor);
		uint64_t file_size = lisa_gpu_load64(cursor + 68);
		if (name_len == 0 || name_len >= 64 || off > range[1] || file_size > range[1] - off) return NULL;
		if (lisa_gpu_name_equals(cursor + 4, name_len, wanted)) {
			*length = file_size;
			return (const uint8_t *)(uintptr_t)(base + off);
		}
		off = (off + file_size + LISA_GPU_FW_ALIGN - 1) & ~(uint64_t)(LISA_GPU_FW_ALIGN - 1);
	}
	return NULL;
}

/* This validates the loader-to-kernel handoff without touching GPU, GMU,
 * clock, power, TrustZone, or SMMU registers.  It is deliberately opt-in;
 * the full driver will use the same lookup path once its bring-up sequence is
 * linked. */
void
lisa_gpu_firmware_probe(void)
{
	static const char *const names[] = {
		"a660_gmu.bin", "a660_sqe.fw", "a660_zap.mdt", "a660_zap.b00",
		"a660_zap.b01", "a660_zap.b02",
	};
	uint32_t enabled = 0;
	PE_parse_boot_argn("lisagpu_fw_probe", &enabled, sizeof(enabled));
	if (enabled != 1) return;
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		uint64_t length = 0;
		if (lisa_gpu_firmware_file(names[i], &length) == NULL) {
			printf("LISA GPU FW: %s unavailable\n", names[i]);
			return;
		}
		printf("LISA GPU FW: %s %llu bytes\n", names[i], length);
	}
}
#endif
