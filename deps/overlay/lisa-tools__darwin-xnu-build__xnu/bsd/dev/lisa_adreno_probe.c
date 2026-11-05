/*
 * Lisa Adreno 642L read-only identity probe.
 *
 * Enabled only by lisagpu_probe=1.  This never writes a MMIO register, votes
 * for a rail/clock, starts GMU firmware, changes an SMMU context, or submits a
 * GPU command.  It is the guardrail before the opt-in Redwood-derived bring-up.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/systm.h>
#include <pexpert/pexpert.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define LISA_GPUCC_PA 0x03d90000u
#define LISA_SMMU_PA  0x03da0000u
#define LISA_GPU_PA   0x03d00000u
#define GPUCC_GX_GDSCR       0x100cu
#define GPUCC_CX_GMU_CBCR    0x1098u
#define GPUCC_GMU_RCGR       0x1120u
#define GPUCC_SMMU_VOTE_CBCR 0x5000u
#define SMMU_IDR0            0x0020u
#define SMMU_IDR1            0x0024u
#define SMMU_SCR0            0x0000u

static uint32_t
lisa_gpu_rd(const volatile uint32_t *base, uint32_t offset)
{
	uint32_t value;
	/* Keep this as a plain load: Qualcomm's EL2 traps MMIO accesses. */
	__asm__ volatile("ldr %w0, [%1]" : "=r"(value) : "r"(base + offset / 4) : "memory");
	return value;
}

void
lisa_adreno_probe(void)
{
	uint32_t enabled = 0;
	PE_parse_boot_argn("lisagpu_probe", &enabled, sizeof(enabled));
	if (enabled != 1) return;

	volatile uint32_t *gpucc = (volatile uint32_t *)ml_io_map(LISA_GPUCC_PA, 0x10000);
	volatile uint32_t *smmu = (volatile uint32_t *)ml_io_map(LISA_SMMU_PA, 0x100000);
	if (gpucc == NULL || smmu == NULL) {
		printf("LISA GPU: probe map failed (read-only)\n");
		return;
	}
	printf("LISA GPU: Adreno 642L probe; no power/clock/GMU/SMMU writes\n");
	printf("LISA GPU: GPUCC gx_gdsc=%#x cx_gmu=%#x gmu_rcgr=%#x smmu_vote=%#x\n",
	    lisa_gpu_rd(gpucc, GPUCC_GX_GDSCR), lisa_gpu_rd(gpucc, GPUCC_CX_GMU_CBCR),
	    lisa_gpu_rd(gpucc, GPUCC_GMU_RCGR), lisa_gpu_rd(gpucc, GPUCC_SMMU_VOTE_CBCR));
	printf("LISA GPU: SMMU scr0=%#x idr0=%#x idr1=%#x\n",
	    lisa_gpu_rd(smmu, SMMU_SCR0), lisa_gpu_rd(smmu, SMMU_IDR0), lisa_gpu_rd(smmu, SMMU_IDR1));
}
#endif
