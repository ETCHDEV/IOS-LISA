#!/usr/bin/env python3
"""Generate patches/xnu-12377/zzzzzz-lisa-touch.patch from Lisa's proven iOS 16 Goodix sources (GT9897 on GPIO SPI, event 0x10338)."""
import re
from pathlib import Path
T = Path(__file__).resolve().parents[2]
X = T / 'darwin-xnu-build/xnu/bsd/dev'
out = T / 'research-UniXNU-ios26/patches/xnu-12377/zzzzzz-lisa-touch.patch'
touch = (X / 'sweet_touch.c').read_text().split('\n')
def lines(a, b): return '\n'.join(touch[a - 1:b])
def find(prefix, start=0):
    for i in range(start, len(touch)):
        if touch[i].startswith(prefix): return i + 1
    raise SystemExit('missing ' + prefix)
def func(prefix):
    """Whole top-level definition; the return type may be on the previous line."""
    n = find(prefix)
    a = n - 1 if not re.match(r'(static|int|void)\b', touch[n - 1]) else n
    e = n
    while touch[e - 1] != '}' and not (touch[e-1].rstrip().endswith('}') and e == n): e += 1
    return lines(a, e)
body = r'''/* Lisa GT9897T touch for xnu-12377, from sweet_touch.c (iOS 16 hardware log: GPIO SPI input 52, event 0x10338).
 * Polled worker thread; publishes through UniXNU's native_touch_* seqlock (0..32767), read by kits/zeta/HidInputProbe. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "lisa_goodix_berlin.h"
#include "lisa_geni_spi.h"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wunused-function"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
extern void delay(int);
extern int sweet_rpmh_vreg_enable(const char *res, unsigned mv);
void lisa_touch_init(void);
#define TOUCH_MAX 10u
volatile unsigned int native_touch_seq, native_touch_count, native_touch_hid_reports;
volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
static void (*touch_sink)(void *);
static void *touch_sink_context;
void native_touch_set_sink(void (*sink)(void *), void *context) { touch_sink_context = context; __asm__ volatile("dmb ish" ::: "memory"); touch_sink = sink; }
static struct { uint32_t active; int x[TOUCH_MAX], y[TOUCH_MAX]; } st;
static int touch_sleep_chan;
typedef int (*lisa_goodix_read_fn)(void *, uint32_t, uint8_t *, size_t);
typedef int (*lisa_goodix_write_fn)(void *, uint32_t, const uint8_t *, size_t);
typedef int (*lisa_spi_exchange_fn)(void *, const uint8_t *, uint8_t *, size_t);
struct lisa_goodix_spi_bus { void *context; lisa_spi_exchange_fn exchange; uint32_t event_address; };
int lisa_goodix_feed(const uint8_t *data, size_t length);
int lisa_goodix_event_address(const uint8_t *data, size_t length, uint32_t *address);
'''
body += '\n' + func('touch_rd(') + '\n\n' + func('touch_wr(') + '\n\n' + func('touch_wait(') + '\n\n'
body += r'''/* The whole finger set after each frame; HidInputProbe sends tip-ups for identifiers that left it. */
static void
touch_report(uint32_t mask, const int *x, const int *y)
{
	unsigned count = 0;
	native_touch_seq = native_touch_seq + 1;
	__asm__ volatile("dmb ish" ::: "memory");
	for (unsigned id = 0; id < TOUCH_MAX; ++id) {
		if (!(mask & (1u << id))) continue;
		native_touch_ids[count] = (int)id;
		native_touch_xs[count] = x[id] * 32767 / 1079;
		native_touch_ys[count] = y[id] * 32767 / 2399;
		st.x[id] = x[id]; st.y[id] = y[id];
		count++;
	}
	native_touch_count = count;
	__asm__ volatile("dmb ish" ::: "memory");
	native_touch_seq = native_touch_seq + 1;
	bool changed = st.active != mask || count;
	st.active = mask;
	void (*sink)(void *) = touch_sink;
	if (changed && sink) sink(touch_sink_context);
}

'''
a = find('lisa_goodix_feed(') - 1          # "int" line above
b = find('static int', find('lisa_goodix_spi_discover(')) - 1
body += lines(a, b).rstrip() + '\n'
c = find('struct lisa_touch_hw {')
e = find('#endif /* ARM64_BOARD_CONFIG_SWEET */', c)
tail = lines(c, e - 1)
tail = tail.replace('\t\tstatic unsigned hot;\n', '\t\tstatic unsigned hot;\n')
# The boot-arg gate stays: lisatouch=1 opts in, as on iOS 16.
body += '\n' + tail + '\n'
# GPIO SPI over the kernel's thread: the only path proven on the phone. GENI remains first choice, as in the iOS 16 driver.
rpmh = (X / 'sweet_rpmh.c').read_text()
rpmh = rpmh.replace('#if defined(ARM64_BOARD_CONFIG_SWEET)\n', '', 1)
rpmh = re.sub(r'#endif[^\n]*\n*$', '', rpmh.rstrip() + '\n')
rpmh = '#pragma clang diagnostic ignored "-Wmissing-prototypes"\n#pragma clang diagnostic ignored "-Wsign-conversion"\n#pragma clang diagnostic ignored "-Wshorten-64-to-32"\n#pragma clang diagnostic ignored "-Wcast-qual"\n' + rpmh
cf = (X / 'lisa_cpufreq.c').read_text()
cf_core = cf[cf.index('#define EPSS_PHYS'):cf.index('/*\n * A load governor')]
cf_new = '''/* Lisa EPSS: each core group to its top row, from lisa_cpufreq.c (iOS 16, same SoC); LMh firmware still limits thermally. boot-arg lisacpufreq=1. */
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wunused-variable"
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
void lisa_cpufreq_init(void);
''' + cf_core + '''void
lisa_cpufreq_init(void)
{
	static int done;
	const ml_topology_info_t *topology = ml_get_topology_info();
	uint32_t enabled = 0, cap_mhz = 0, cores[EPSS_DOMAINS] = { 0 }, before;

	if (done || !PE_parse_boot_argn("lisacpufreq", &enabled, sizeof(enabled)) || enabled != 1) {
		return;
	}
	done = 1;
	PE_parse_boot_argn("lisacpufreq_cap", &cap_mhz, sizeof(cap_mhz));
	for (unsigned int i = 0; i < topology->num_cpus; i++) {
		cores[domain_of(topology->cpus[i].phys_id)]++;
	}
	before = loop_speed();
	for (uint32_t domain = 0; domain < EPSS_DOMAINS; domain++) {
		if (cores[domain] != 0) {
			raise_domain(domain, cores[domain], cap_mhz * 1000);
		}
	}
	printf("LISA CPUFREQ: a loop on this core ran at %u, now at %u\\n", before, loop_speed());
}
'''
keys = (X / 'sweet_keys.c').read_text().replace('#if defined(ARM64_BOARD_CONFIG_SWEET)\n', '', 1)
keys = re.sub(r'#endif[^\n]*\n*$', '', keys.rstrip() + '\n')
keys = keys.replace('extern void sweet_hid_key(int, int);', '''static void (*key_sink)(int, int);
void lisa_keys_set_sink(void (*sink)(int, int));
void lisa_keys_set_sink(void (*sink)(int, int)) { key_sink = sink; }
static void sweet_hid_key(int key, int down) { if (key_sink) key_sink(key, down); }''')
keys = '#pragma clang diagnostic ignored "-Wmissing-prototypes"\n#pragma clang diagnostic ignored "-Wsign-conversion"\n#pragma clang diagnostic ignored "-Wshorten-64-to-32"\n#pragma clang diagnostic ignored "-Wcast-qual"\n' + keys
stats = r'''/* Lisa: what the phone is doing, in the log every 10 s (boot-arg lisa-stats=1): CPU per core, memory, the busiest processes. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc_internal.h>
#include <sys/proc_info.h>
#include <sys/bsdtask_info.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/processor.h>
#include <mach/processor_info.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
extern processor_t cpu_to_processor(int cpu);
extern kern_return_t processor_info(processor_t, processor_flavor_t, host_t *, processor_info_t, mach_msg_type_number_t *);
extern unsigned int vm_page_free_count, vm_page_active_count, vm_page_inactive_count, vm_page_wire_count, vm_page_speculative_count;
void lisa_stats_init(void);
#define STAT_PIDS 2048
static uint64_t stat_last[STAT_PIDS];
struct stat_top { int pid; uint64_t ns; char name[20]; };
static struct stat_top stat_tops[5];
static int
stat_proc(proc_t p, __unused void *arg)
{
	struct proc_taskinfo_internal info;
	int pid = proc_pid(p);
	task_t task = proc_task(p);

	if (pid < 0 || pid >= STAT_PIDS || task == TASK_NULL ) {
		return PROC_RETURNED;
	}
	fill_taskprocinfo(task, &info);
	uint64_t now = info.pti_total_user + info.pti_total_system, delta = now - stat_last[pid];
	stat_last[pid] = now;
	for (int i = 0; i < 5; i++) {
		if (delta > stat_tops[i].ns) {
			memmove(&stat_tops[i + 1], &stat_tops[i], (4 - i) * sizeof(stat_tops[0]));
			stat_tops[i].pid = pid;
			stat_tops[i].ns = delta;
			proc_name(pid, stat_tops[i].name, sizeof(stat_tops[i].name));
			break;
		}
	}
	return PROC_RETURNED;
}
static void
stat_thread(__unused void *arg, __unused wait_result_t wr)
{
	static uint64_t last_busy[8], last_total[8];
	static int chan;
	for (;;) {
		char line[200];
		int used = 0;
		uint64_t deadline;
		clock_interval_to_deadline(10, NSEC_PER_SEC, &deadline);
		(void)assert_wait_deadline(&chan, THREAD_UNINT, deadline);
		thread_block(THREAD_CONTINUE_NULL);
		for (int c = 0; c < 8; c++) {
			processor_t processor = cpu_to_processor(c);
			processor_cpu_load_info_data_t load;
			mach_msg_type_number_t count = PROCESSOR_CPU_LOAD_INFO_COUNT;
			host_t host;
			if (processor == PROCESSOR_NULL || processor_info(processor, PROCESSOR_CPU_LOAD_INFO, &host,
			    (processor_info_t)&load, &count) != KERN_SUCCESS) {
				continue;
			}
			uint64_t busy = (uint64_t)load.cpu_ticks[CPU_STATE_USER] + load.cpu_ticks[CPU_STATE_SYSTEM] + load.cpu_ticks[CPU_STATE_NICE];
			uint64_t total = busy + load.cpu_ticks[CPU_STATE_IDLE];
			uint64_t db = busy - last_busy[c], dt = total - last_total[c];
			last_busy[c] = busy;
			last_total[c] = total;
			used += snprintf(line + used, sizeof(line) - used, " %llu", dt ? db * 100 / dt : 0);
		}
		printf("lisa-stat: cpu%%%s | MiB free %u active %u inactive %u spec %u wired %u\\n", line,
		    (unsigned)(vm_page_free_count * (PAGE_SIZE / 1024) / 1024), (unsigned)(vm_page_active_count * (PAGE_SIZE / 1024) / 1024),
		    (unsigned)(vm_page_inactive_count * (PAGE_SIZE / 1024) / 1024), (unsigned)(vm_page_speculative_count * (PAGE_SIZE / 1024) / 1024),
		    (unsigned)(vm_page_wire_count * (PAGE_SIZE / 1024) / 1024));
		bzero(stat_tops, sizeof(stat_tops));
		proc_iterate(PROC_ALLPROCLIST, stat_proc, NULL, NULL, NULL);
		uint64_t all = 8ull * 192000000ull;   /* the times are 19.2 MHz ticks; 100 = all 8 cores for the 10 s */
		printf("lisa-stat: top%% %s %llu %s %llu %s %llu %s %llu %s %llu\\\\n",
		    stat_tops[0].name, stat_tops[0].ns * 100 / all, stat_tops[1].name, stat_tops[1].ns * 100 / all,
		    stat_tops[2].name, stat_tops[2].ns * 100 / all, stat_tops[3].name, stat_tops[3].ns * 100 / all,
		    stat_tops[4].name, stat_tops[4].ns * 100 / all);
	}
}
void
lisa_stats_init(void)
{
	int on = 0;
	thread_t thread;
	PE_parse_boot_argn("lisa-stats", &on, sizeof(on));
	if (on == 1 && kernel_thread_start(stat_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
'''
ddr = r'''
/* Memory bandwidth: a vote through RPMh on MC0 and SH0 (hakios.cpufreq.c, same SoC: 12.4 -> 15.4 GB/s all cores). boot-arg lisa-ddr=0: none. */
#define CMD_DB_PHYS 0x80860000UL
#define CMD_DB_SIZE 0x20000UL
#define DDR_RSC_PHYS 0x18220000UL
#define BCM_COMMIT (1u << 30)
#define BCM_VALID (1u << 29)
#define BCM_VOTE(x, y) (BCM_COMMIT | BCM_VALID | ((uint32_t)(x) & 0x3fff) << 14 | ((uint32_t)(y) & 0x3fff))
static vm_offset_t ddr_rsc;
#define TCS_REG(reg) (ddr_rsc + 0xd00 + (reg))
static uint32_t
db16(volatile uint8_t *d, size_t at)
{
	return d[at] | (uint32_t)d[at + 1] << 8;
}
static uint32_t
cmd_db_bcm(volatile uint8_t *d, const char *name)
{
	char id[8] = { 0 };
	strlcpy(id, name, sizeof(id));
	for (unsigned s = 0; s < 8; s++) {
		size_t h = 8 + 16 * s;
		size_t head = 144 + db16(d, h + 2), count = db16(d, h + 6);
		if (db16(d, h) != 5 || count > 512 || head + count * 24 > CMD_DB_SIZE) {
			continue;
		}
		for (size_t e = head; e < head + count * 24; e += 24) {
			bool same = true;
			for (unsigned k = 0; k < 8 && same; k++) {
				same = d[e + k] == (uint8_t)id[k];
			}
			if (same) {
				return db16(d, e + 16) | db16(d, e + 18) << 16;
			}
		}
	}
	return 0;
}
static bool
ddr_rpmh_write(const uint32_t *address, const uint32_t *data, unsigned count)
{
	uint32_t irq_enable = *(volatile uint32_t *)TCS_REG(0x00);
	bool done = false;
	for (int i = 0; *(volatile uint32_t *)TCS_REG(0x18) == 0; i++) {
		if (i > 2000) {
			return false;
		}
		delay_for_interval(100, NSEC_PER_USEC);
	}
	*(volatile uint32_t *)TCS_REG(0x14) &= ~(1u << 24);
	*(volatile uint32_t *)TCS_REG(0x14) &= ~(1u << 16);
	*(volatile uint32_t *)TCS_REG(0x08) = 1;
	for (unsigned i = 0; i < count; i++) {
		*(volatile uint32_t *)TCS_REG(0x30 + 20 * i) = 0x10008 | 0x100;
		*(volatile uint32_t *)TCS_REG(0x34 + 20 * i) = address[i];
		*(volatile uint32_t *)TCS_REG(0x38 + 20 * i) = data[i];
	}
	*(volatile uint32_t *)TCS_REG(0x10) = (1u << count) - 1;
	*(volatile uint32_t *)TCS_REG(0x1c) = (1u << count) - 1;
	*(volatile uint32_t *)TCS_REG(0x00) = irq_enable | 1;
	*(volatile uint32_t *)TCS_REG(0x14) = 1u << 16;
	*(volatile uint32_t *)TCS_REG(0x14) = 1u << 16 | 1u << 24;
	for (int i = 0; i < 2000 && !done; i++) {
		done = (*(volatile uint32_t *)TCS_REG(0x04) & 1) != 0;
		if (!done) {
			delay_for_interval(100, NSEC_PER_USEC);
		}
	}
	*(volatile uint32_t *)TCS_REG(0x14) &= ~(1u << 24);
	*(volatile uint32_t *)TCS_REG(0x14) &= ~(1u << 16);
	*(volatile uint32_t *)TCS_REG(0x1c) = 0;
	*(volatile uint32_t *)TCS_REG(0x10) = 0;
	*(volatile uint32_t *)TCS_REG(0x08) = 1;
	*(volatile uint32_t *)TCS_REG(0x00) = irq_enable;
	return done;
}
static uint32_t
mem_mbps(void)
{
	size_t bytes = 8u << 20;
	void *a = kalloc_data(bytes, Z_WAITOK), *b = kalloc_data(bytes, Z_WAITOK);
	uint32_t result = 0;
	if (a && b) {
		memset(a, 1, bytes);
		memset(b, 2, bytes);
		uint64_t start = mach_absolute_time(), nanos;
		for (int i = 0; i < 16; i++) {
			memcpy(i & 1 ? a : b, i & 1 ? b : a, bytes);
		}
		absolutetime_to_nanoseconds(mach_absolute_time() - start, &nanos);
		result = nanos ? (uint32_t)((uint64_t)bytes * 16 * 1000 / nanos) : 0;
	}
	if (a) kfree_data(a, bytes);
	if (b) kfree_data(b, bytes);
	return result;
}
static void
ddr_raise(void)
{
	uint32_t on = 1, before = mem_mbps();
	PE_parse_boot_argn("lisa-ddr", &on, sizeof(on));
	if (on) {
		volatile uint8_t *db = (volatile uint8_t *)ml_io_map(CMD_DB_PHYS, CMD_DB_SIZE);
		uint32_t address[3] = { cmd_db_bcm(db, "MC0"), cmd_db_bcm(db, "SH0"), cmd_db_bcm(db, "ACV") };
		uint32_t data[3] = { BCM_VOTE(0x3fff, 0x3fff), BCM_VOTE(0x3fff, 0x3fff), BCM_VOTE(0, 8) };
		ddr_rsc = ml_io_map(DDR_RSC_PHYS, 0x4000);
		bool done = address[0] && address[1] && address[2] && ddr_rsc && ddr_rpmh_write(address, data, 3);
		printf("LISA DDR: vote MC0 %#x SH0 %#x ACV %#x %s\\n", address[0], address[1], address[2], done ? "done" : "FAILED");
	}
	printf("LISA DDR: copy on this core %u MB/s before, %u MB/s after\\n", before, mem_mbps());
}
'''
cf_new = cf_new.replace('void\nlisa_cpufreq_init(void)\n{', ddr + 'void\nlisa_cpufreq_init(void)\n{')
cf_new = cf_new.replace('#include <sys/systm.h>\n', '#include <sys/systm.h>\n#include <kern/kalloc.h>\n', 1)
cf_new = cf_new.replace('before, loop_speed());\n', 'before, loop_speed());\n\t{ uint64_t isar1; __asm__ volatile("mrs %0, ID_AA64ISAR1_EL1" : "=r"(isar1)); printf("LISA CPU: ISAR1 %#llx: pointer auth APA %llu API %llu GPA %llu GPI %llu\\\\n", isar1, (isar1 >> 4) & 15, (isar1 >> 8) & 15, (isar1 >> 24) & 15, (isar1 >> 28) & 15); }\n\tddr_raise();\n\tlisa_stats_init();\n')
cf_new = cf_new.replace('void lisa_cpufreq_init(void);\n', 'void lisa_cpufreq_init(void);\nextern void lisa_stats_init(void);\n')
files = {
 'bsd/dev/lisa_stats.c': stats,
 'bsd/dev/sweet_keys.c': keys,
 'bsd/dev/lisa_cpufreq.c': cf_new,
 'bsd/dev/lisa_touch.c': body,
 'bsd/dev/sweet_rpmh.c': rpmh,
 'bsd/dev/lisa_goodix_berlin.h': (X / 'lisa_goodix_berlin.h').read_text(),
 'bsd/dev/lisa_geni_spi.h': (X / 'lisa_geni_spi.h').read_text(),
 'bsd/dev/lisa_geni_spi.c': '#pragma clang diagnostic ignored "-Wmissing-prototypes"\n#pragma clang diagnostic ignored "-Wsign-conversion"\n#pragma clang diagnostic ignored "-Wshorten-64-to-32"\n' + (X / 'lisa_geni_spi.c').read_text(),
}
p = '''diff --git a/bsd/conf/files b/bsd/conf/files
--- a/bsd/conf/files
+++ b/bsd/conf/files
@@ -106,3 +106,9 @@
 bsd/dev/mem.c				standard
 bsd/dev/sweet_ufs.c			standard
+bsd/dev/lisa_touch.c			standard
+bsd/dev/lisa_cpufreq.c			standard
+bsd/dev/sweet_keys.c			standard
+bsd/dev/lisa_stats.c			standard
+bsd/dev/sweet_rpmh.c			standard
+bsd/dev/lisa_geni_spi.c			standard
 bsd/dev/munge.c 			optional config_requires_u32_munging
'''
for name, text in files.items():
    ls = text.rstrip('\n').split('\n')
    p += f'diff --git a/{name} b/{name}\nnew file mode 100644\n--- /dev/null\n+++ b/{name}\n@@ -0,0 +1,{len(ls)} @@\n' + ''.join('+' + l + '\n' for l in ls)
out.write_text(p)
print(out.name, len(body.split('\n')), 'lines of lisa_touch.c')
