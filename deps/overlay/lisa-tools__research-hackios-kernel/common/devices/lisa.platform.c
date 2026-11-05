/*
 * lisa: paste into xnuloader/loader.c next to platform_redwood and add &platform_lisa to platforms[].
 * Every value is explained in docs/PORTING.md ("The loader").
 */
static const struct platform platform_lisa = {
	.name = "lisa",
	.io_base = 0x00400000, .io_size = 0x3FC00000,
	.gic_dist = 0x17a00000, .gic_redist = 0x17a60000,
	.uart = 0,                                      /* phones: no PL011 */
	.log_other_cpu = 1,
	.cores = 8, .core_shift = 8,
	.small_cores = 0x0f,                         /* cpu@0..cpu@300, 0x400 DMIPS/MHz */
	.fb_base = 0xE1000000,
};
