/*
 * Lisa (SM7325) CPU clocks, ported from upstream HakiOS bsd/dev/hakios.cpufreq.c (same SoC).
 * Opt-in: boot-arg lisacpufreq=1.
 *
 * The boot loader leaves each core group at a modest frequency. EPSS (qcom,cpufreq-hw-epss,
 * Lisa live DTB: 0x18591000 / 0x18592000 / 0x18593000, as on redwood) has a frequency table per
 * group and switches clock source and voltage itself when told which row is wanted; Qualcomm's
 * LMh firmware still limits it thermally. One register write per group: the top row.
 * Register layout: Linux drivers/cpufreq/qcom-cpufreq-hw.c ("epss"). No DDR/RPMh vote here
 * (upstream's ddr_raise writes RPMh TCS slots that Lisa's sweet_rpmh/Adreno code also use).
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <mach/processor_info.h>
#include <kern/sched_prim.h>

extern processor_t cpu_to_processor(int cpu);
/* Software display dimming until the CSOT DCS transport is implemented.
 * Backboardd writes this through its brightness controller; GLES applies it
 * to final panel output. This does not claim hardware backlight control. */
static int lisa_display_awake = 1;
SYSCTL_INT(_kern, OID_AUTO, lisa_display_awake, CTLFLAG_RD | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    &lisa_display_awake, 0, "Lisa framebuffer power state (not system suspend)");
void lisa_display_set_awake(int awake);
int lisa_display_is_awake(void);
int lisa_display_is_awake(void) { return __atomic_load_n(&lisa_display_awake, __ATOMIC_ACQUIRE); }
static int lisa_brightness = 1000;
SYSCTL_INT(_kern, OID_AUTO, lisa_brightness, CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    &lisa_brightness, 0, "Lisa software display brightness (20..1000)");
/* kern.lisa_say: a line from any process straight into the persistent log (independent of stdout). */
static int
sysctl_lisa_say SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char line[256];
	size_t n;
	if (req->newptr == USER_ADDR_NULL) {
		return 0;
	}
	n = req->newlen < sizeof(line) - 1 ? req->newlen : sizeof(line) - 1;
	if (SYSCTL_IN(req, line, n) != 0) {
		return EINVAL;
	}
	line[n] = 0;
	char who[32];
	proc_selfname(who, sizeof(who));
	printf("LISA SAY %s: %s\n", who, line);
	return 0;
}
SYSCTL_PROC(_kern, OID_AUTO, lisa_say, CTLTYPE_STRING | CTLFLAG_WR | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    0, 0, sysctl_lisa_say, "A", "Lisa: a line into the kernel log");
int lisa_brightness_level(void);
int lisa_brightness_level(void) { return lisa_brightness; }

/*
 * Display handshake between SweetFramebuffer (kernel) and liblisagl's GPU present (backboardd).
 * lisa_swap_ids: the last 8 distinct surfaces backboardd swapped onto the display. Only these may
 * be drawn onto the panel by the GPU: CoreAnimation also renders full-screen offscreen targets
 * (wallpaper/poster snapshots, blurs), and presenting those flickered through every wallpaper.
 * lisa_gpu_presented: written by liblisagl after it drew a surface onto the panel; a swapped
 * surface that is not among the last 4 written gets the CPU copy instead (never a stale panel).
 * lisa_color_matrix: IOMobileFramebufferSetMatrix (selector 55, Night Shift / white point),
 * applied by the GPU present shader.
 */
uint32_t lisa_swap_ids[8];
uint32_t lisa_swap_next;
uint32_t lisa_gpu_shown[4];
uint32_t lisa_gpu_shown_next;
int lisa_gpu_handshake;         /* 1 once liblisagl wrote lisa_gpu_presented */
struct lisa_color_matrix {
	uint32_t gen;
	uint32_t count;
	struct { uint64_t key; uint64_t m[9]; } e[4];
};
struct lisa_color_matrix lisa_color_matrix;
/* the surface last swapped onto the display, and how many swaps there have been: (seq << 32) | id */
uint64_t lisa_swap_last;
SYSCTL_QUAD(_kern, OID_AUTO, lisa_swap_last, CTLFLAG_RD | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    &lisa_swap_last, "last surface swapped onto the display (seq << 32 | id)");
SYSCTL_OPAQUE(_kern, OID_AUTO, lisa_swap_ids, CTLFLAG_RD | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    lisa_swap_ids, sizeof(lisa_swap_ids), "IU", "surfaces recently swapped onto the display");
SYSCTL_OPAQUE(_kern, OID_AUTO, lisa_color_matrix, CTLFLAG_RD | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    &lisa_color_matrix, sizeof(lisa_color_matrix), "S", "display colour matrices (IOMFB SetMatrix)");
static int
sysctl_lisa_gpu_presented SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1, arg2)
	int value = 0, error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == USER_ADDR_NULL) {
		return error;
	}
	uint32_t slot = __atomic_fetch_add(&lisa_gpu_shown_next, 1, __ATOMIC_RELAXED) % 4;
	__atomic_store_n(&lisa_gpu_shown[slot], (uint32_t)value, __ATOMIC_RELEASE);
	lisa_gpu_handshake = 1;
	return 0;
}
SYSCTL_PROC(_kern, OID_AUTO, lisa_gpu_presented, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    0, 0, sysctl_lisa_gpu_presented, "I", "surface the GPU just drew onto the panel");
extern kern_return_t processor_info(processor_t processor, processor_flavor_t flavor, host_t *host,
    processor_info_t info, mach_msg_type_number_t *count);
extern kern_return_t thread_terminate(thread_t);

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
void lisa_cpufreq_init(void);

#define EPSS_PHYS               0x18591000UL
#define EPSS_DOMAIN_SIZE        0x1000UL
#define EPSS_DOMAINS            3
#define EPSS_FREQ_TABLE         0x100   /* a word per row: [31:30] clock source, [7:0] multiple of 19.2 MHz */
#define EPSS_TABLE_ROWS         40
#define EPSS_PERF_STATE         0x320   /* the row that is wanted */
#define XO_KHZ                  19200
#define ALTERNATE_KHZ           300000

static vm_offset_t domain_base[EPSS_DOMAINS];
static uint32_t domain_khz[EPSS_DOMAINS][EPSS_TABLE_ROWS];
static uint32_t domain_rows[EPSS_DOMAINS], domain_row_now[EPSS_DOMAINS], domain_cap_row[EPSS_DOMAINS];

static uint32_t
loop_speed(void)
{
	volatile uint64_t sink = 0;
	uint64_t start = mach_absolute_time(), nanos;

	for (uint32_t i = 0; i < 2000000; i++) {
		sink += i;
	}
	absolutetime_to_nanoseconds(mach_absolute_time() - start, &nanos);
	return nanos ? (uint32_t)(2000000ULL * 1000 / nanos) : 0;
}

static uint32_t
domain_of(uint32_t phys_id)
{
	uint32_t core = (phys_id >> 8) & 0xff;

	return core < 4 ? 0 : core < 7 ? 1 : 2;
}

static void
raise_domain(uint32_t domain, uint32_t cores, uint32_t cap_khz)
{
	uint32_t top = 0, top_khz = 0, was;
	vm_offset_t base = ml_io_map(EPSS_PHYS + domain * EPSS_DOMAIN_SIZE, EPSS_DOMAIN_SIZE);

	if (base == 0) {
		printf("LISA CPUFREQ: group %u: map failed\n", domain);
		return;
	}
	domain_base[domain] = base;
	for (uint32_t row = 0; row < EPSS_TABLE_ROWS; row++) {
		uint32_t entry = *(volatile uint32_t *)(base + EPSS_FREQ_TABLE + 4 * row);
		uint32_t khz = (entry >> 30) ? XO_KHZ * (entry & 0xff) : ALTERNATE_KHZ;

		if (row > 0 && khz <= top_khz) {
			break;          /* the table ends by repeating its last row */
		}
		if (cap_khz != 0 && khz > cap_khz) {
			break;
		}
		top = row;
		top_khz = khz;
		domain_khz[domain][row] = khz;
	}
	domain_rows[domain] = top + 1;
	domain_cap_row[domain] = top;
	domain_row_now[domain] = top;
	was = *(volatile uint32_t *)(base + EPSS_PERF_STATE);
	*(volatile uint32_t *)(base + EPSS_PERF_STATE) = top;
	delay_for_interval(20, NSEC_PER_MSEC);
	printf("LISA CPUFREQ: group %u (%u cores): row %u -> %u, %u MHz\n", domain, cores, was, top, top_khz / 1000);
}


/*
 * A load governor (boot-arg lisacpufreq=2): every 100 ms, from each core's idle ticks, each group
 * goes to its top row when busy (> 60 %), to the row nearest 65 % of top under moderate load, and
 * to the row nearest 35 % of top when nearly idle (< 15 %), which is where it sits with the screen
 * asleep. Decide from the busiest core, not the group's average: one saturated UI/render
 * thread must not be diluted to 25% by three idle cores sharing the same clock.
 * lisacpufreq=1 keeps the top row always (the old behaviour).
 */
static int gov_chan;
static uint64_t input_boost_until;
static int governor_ready;
void lisa_cpufreq_input_hint(void);

void
lisa_display_set_awake(int awake)
{
    __atomic_store_n(&lisa_display_awake, awake != 0, __ATOMIC_RELEASE);
    if (!awake) __atomic_store_n(&input_boost_until, 0, __ATOMIC_RELAXED);
    if (__atomic_load_n(&governor_ready, __ATOMIC_ACQUIRE)) thread_wakeup(&gov_chan);
}

void
lisa_cpufreq_input_hint(void)
{
	if (!lisa_display_is_awake() || !__atomic_load_n(&governor_ready, __ATOMIC_ACQUIRE)) return;
	uint64_t now = mach_absolute_time(), until;
	clock_interval_to_deadline(250, NSEC_PER_MSEC, &until);
	uint64_t old = __atomic_exchange_n(&input_boost_until, until, __ATOMIC_RELAXED);
	/* Wake immediately at gesture start; later events extend the same boost
	 * without waking the governor on every I2C touch sample. */
	if (old <= now) thread_wakeup(&gov_chan);
}

static uint32_t
row_near(uint32_t domain, uint32_t khz)
{
	uint32_t best = 0;

	for (uint32_t row = 0; row <= domain_cap_row[domain]; row++) {
		if (domain_khz[domain][row] <= khz) {
			best = row;
		}
	}
	return best;
}

static void
governor_thread(void *arg, wait_result_t wr)
{
	(void)arg; (void)wr;
	const ml_topology_info_t *topology = ml_get_topology_info();
	uint64_t last_busy[64] = { 0 }, last_total[64] = { 0 };
	unsigned said = 0;

	for (;;) {
		bool awake = lisa_display_is_awake();
		bool input_boost = awake && mach_absolute_time() < __atomic_load_n(&input_boost_until, __ATOMIC_RELAXED);
		uint64_t busy[EPSS_DOMAINS] = { 0 }, total[EPSS_DOMAINS] = { 0 };
		uint32_t peak[EPSS_DOMAINS] = { 0 };

		for (unsigned i = 0; i < topology->num_cpus && i < 64; i++) {
			processor_t processor = cpu_to_processor((int)topology->cpus[i].cpu_id);
			processor_cpu_load_info_data_t load;
			mach_msg_type_number_t count = PROCESSOR_CPU_LOAD_INFO_COUNT;
			host_t host;

			if (processor == PROCESSOR_NULL || processor_info(processor, PROCESSOR_CPU_LOAD_INFO, &host,
			    (processor_info_t)&load, &count) != KERN_SUCCESS) {
				continue;
			}
			uint64_t b = (uint64_t)load.cpu_ticks[CPU_STATE_USER] + load.cpu_ticks[CPU_STATE_SYSTEM] +
			    load.cpu_ticks[CPU_STATE_NICE];
			uint64_t t = b + load.cpu_ticks[CPU_STATE_IDLE];
			uint32_t d = domain_of(topology->cpus[i].phys_id);
			uint64_t delta_busy = b - last_busy[i], delta_total = t - last_total[i];
			busy[d] += delta_busy;
			total[d] += delta_total;
			if (delta_total != 0) {
				uint32_t core_pct = (uint32_t)(delta_busy * 100 / delta_total);
				if (core_pct > peak[d]) peak[d] = core_pct;
			}
			last_busy[i] = b;
			last_total[i] = t;
		}
		for (uint32_t d = 0; d < EPSS_DOMAINS; d++) {
			if (domain_base[d] == 0 || domain_rows[d] == 0 || (awake && total[d] == 0 && !input_boost)) {
				continue;
			}
			uint32_t pct = total[d] ? (uint32_t)(busy[d] * 100 / total[d]) : 0;
			uint32_t top = domain_khz[d][domain_cap_row[d]];
			uint32_t want = !awake ? 0 : input_boost || peak[d] > 60 ? domain_cap_row[d] : peak[d] > 15 ? row_near(d, top * 65 / 100) : row_near(d, top * 35 / 100);
			if (want != domain_row_now[d]) {
				*(volatile uint32_t *)(domain_base[d] + EPSS_PERF_STATE) = want;
				domain_row_now[d] = want;
				if (said < 24) {
					said++;
					printf("LISA CPUFREQ: group %u peak %u%% (mean %u%%) -> %u MHz\n", d, peak[d], pct, domain_khz[d][want] / 1000);
				}
			}
		}
		uint64_t deadline;
		clock_interval_to_deadline(100, NSEC_PER_MSEC, &deadline);
		(void)assert_wait_deadline(&gov_chan, THREAD_UNINT, deadline);
		thread_block(THREAD_CONTINUE_NULL);
	}
}

void
lisa_cpufreq_init(void)
{
	const ml_topology_info_t *topology = ml_get_topology_info();
	uint32_t enabled = 0, cap_mhz = 0, cores[EPSS_DOMAINS] = { 0 }, before;

	if (!PE_parse_boot_argn("lisacpufreq", &enabled, sizeof(enabled)) || (enabled != 1 && enabled != 2)) {
		return;
	}
	PE_parse_boot_argn("lisacpufreq_cap", &cap_mhz, sizeof(cap_mhz));     /* MHz, 0 = the top row */
	for (unsigned int i = 0; i < topology->num_cpus; i++) {
		cores[domain_of(topology->cpus[i].phys_id)]++;
	}
	before = loop_speed();
	for (uint32_t domain = 0; domain < EPSS_DOMAINS; domain++) {
		if (cores[domain] != 0) {
			raise_domain(domain, cores[domain], cap_mhz * 1000);
		}
	}
	printf("LISA CPUFREQ: a loop on this core ran at %u, now at %u\n", before, loop_speed());
	if (enabled == 2) {
		thread_t thread;
		if (kernel_thread_start(governor_thread, NULL, &thread) == KERN_SUCCESS) {
			__atomic_store_n(&governor_ready, 1, __ATOMIC_RELEASE);
			thread_deallocate(thread);
			printf("LISA CPUFREQ: load governor running (100 ms)\n");
		}
	}
}
#endif
