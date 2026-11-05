/*
 * Lisa glue for the Wi-Fi port (bsd/dev/lisa_wifi.c and friends, from upstream HakiOS redwood_*).
 *
 *   redwood_image_file()  upstream reads fw/ out of its RAM disk; here the files come from the
 *                         iOS root file system, /usr/local/lib/lisa/<dir>/<name> (put there by
 *                         lisa-tools/lisa-gl/deploy-stage.sh from lisa-firmware/wifi), read once
 *                         into wired kernel memory and kept (the radio's driver keeps pointers).
 *   gic_v3_spi_*          Lisa's kernel has no shared-interrupt attach yet: refused, so the radio
 *                         runs polled (upstream's default without wifi_irq).
 *   loader_*              upstream's loader log / return: nothing here (the log is the ring).
 *   redwood_virtio_net_init, mdev*  QEMU / RAM disk only.
 *
 * lisa_wifi_start() (from sweet_ufs.c, boot-arg wifi=1 or wifi_step=N): a thread waits until the
 * root file system has the firmware, then runs upstream's redwood_wifi_init().
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/kalloc.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <sys/kauth.h>
#include <sys/uio.h>
#include <stdbool.h>
#include <string.h>

extern kern_return_t thread_terminate(thread_t);
extern void redwood_wifi_init(void);
extern void redwood_adsp_init(void);
extern void redwood_net_init(void);
void lisa_wifi_start(void);

#define LISA_FW_FILES   96
static LCK_GRP_DECLARE(fw_group, "lisa-firmware-cache");
static LCK_MTX_DECLARE(fw_lock, &fw_group);
static struct { char path[128]; uint8_t *data; uint64_t size; } fw_cache[LISA_FW_FILES];
static int fw_count;

const uint8_t *redwood_image_file(const char *dir, const char *name, uint64_t *size, int *disk_out);
static const uint8_t *image_file_locked(const char *, const char *, uint64_t *, int *);
const uint8_t *
redwood_image_file(const char *dir, const char *name, uint64_t *size, int *disk_out)
{
    lck_mtx_lock(&fw_lock);
    const uint8_t *data = image_file_locked(dir, name, size, disk_out);
    lck_mtx_unlock(&fw_lock);
    return data;
}
static const uint8_t *
image_file_locked(const char *dir, const char *name, uint64_t *size, int *disk_out)
{
	char path[128];
	vnode_t vp = NULLVP;
	vfs_context_t ctx = vfs_context_kernel();
	off_t length = 0;
	int error, resid = 0;

	if (disk_out != NULL) {
		*disk_out = 0;
	}
	snprintf(path, sizeof(path), "/usr/local/lib/lisa/%s/%s", dir, name);
	for (int i = 0; i < fw_count; i++) {
		if (strcmp(fw_cache[i].path, path) == 0) {
			*size = fw_cache[i].size;
			return fw_cache[i].data;
		}
	}
	if (fw_count >= LISA_FW_FILES || rootvnode == NULLVP) {
		return NULL;
	}
	error = vnode_open(path, FREAD, 0, 0, &vp, ctx);
	if (error != 0) {
		return NULL;
	}
	error = vnode_size(vp, &length, ctx);
	uint8_t *data = NULL;
	if (error == 0 && length > 0 && length < (64 << 20)) {
		data = (uint8_t *)kalloc_data((vm_size_t)length, Z_WAITOK | Z_ZERO);
		if (data != NULL) {
			error = vn_rdwr(UIO_READ, vp, (caddr_t)data, (int)length, 0, UIO_SYSSPACE, IO_NODELOCKED,
			    vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
			if (error != 0 || resid != 0) {
				kfree_data(data, (vm_size_t)length);
				data = NULL;
			}
		}
	}
	vnode_close(vp, FREAD, ctx);
	if (data == NULL) {
		printf("lisa wifi: %s could not be read (error %d)\n", path, error);
		return NULL;
	}
	strlcpy(fw_cache[fw_count].path, path, sizeof(fw_cache[fw_count].path));
	fw_cache[fw_count].data = data;
	fw_cache[fw_count].size = (uint64_t)length;
	fw_count++;
	*size = (uint64_t)length;
	return data;
}

/* No interrupt attach on Lisa yet: the radio is looked at instead (polled). */
kern_return_t gic_v3_spi_attach(uint32_t intid, bool edge, void (*handler)(void *, uint32_t), void *context);
kern_return_t gic_v3_spi_attach(__unused uint32_t intid, __unused bool edge, __unused void (*handler)(void *, uint32_t),
    __unused void *context) { return KERN_NOT_SUPPORTED; }
void gic_v3_spi_mask(uint32_t intid);
void gic_v3_spi_mask(__unused uint32_t intid) {}
void gic_v3_spi_unmask(uint32_t intid);
void gic_v3_spi_unmask(__unused uint32_t intid) {}
void gic_v3_spi_detach(uint32_t intid);
void gic_v3_spi_detach(__unused uint32_t intid) {}
uint64_t gic_v3_spi_count(uint32_t intid);
uint64_t gic_v3_spi_count(__unused uint32_t intid) { return 0; }

void loader_log_putc(char c);
void loader_log_putc(__unused char c) {}
void loader_return(void);
void loader_return(void) { printf("lisa wifi: (no loader to return to on Lisa)\n"); }
void redwood_virtio_net_init(void);
void redwood_virtio_net_init(void) {}
/* mdevlookup is supplied by bsd/dev/memdev.c; do not shadow it here. */

/*
 * Wi-Fi options: the boot arguments first, then /usr/local/lib/lisa/fw/bootargs ("name=value"
 * words; the kernel's command line has no room left), so a Wi-Fi run is changed with a userdata
 * flash, not a kernel. The upstream files call these through #define PE_parse_boot_argn.
 */
static const char *wifi_args;
static uint64_t wifi_args_size;

static const char *
wifi_arg_find(const char *name)
{
	size_t n = strlen(name);
	if (wifi_args == NULL) {
		return NULL;
	}
	for (const char *p = wifi_args, *end = wifi_args + wifi_args_size; p + n < end; p++) {
		if ((p == wifi_args || p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t') && strncmp(p, name, n) == 0 &&
		    (p[n] == '=' || p[n] == ' ' || p[n] == '\n' || p + n == end)) {
			return p + n;
		}
	}
	return NULL;
}

boolean_t lisa_wifi_argn(const char *name, void *value, int size);
boolean_t
lisa_wifi_argn(const char *name, void *value, int size)
{
	if (PE_parse_boot_argn(name, value, size)) {
		return TRUE;
	}
	const char *p = wifi_arg_find(name);
	if (p == NULL) {
		return FALSE;
	}
	uint64_t v = 1;
	if (*p == '=') {
		v = 0;
		p++;
		int base = 10;
		if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
			base = 16;
			p += 2;
		}
		for (; *p; p++) {
			int d = *p >= '0' && *p <= '9' ? *p - '0' : base == 16 && *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
			    base == 16 && *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
			if (d < 0) {
				break;
			}
			v = v * (uint64_t)base + (uint64_t)d;
		}
	}
	switch (size) {
	case 1: *(uint8_t *)value = (uint8_t)v; break;
	case 2: *(uint16_t *)value = (uint16_t)v; break;
	case 8: *(uint64_t *)value = v; break;
	default: *(uint32_t *)value = (uint32_t)v; break;
	}
	return TRUE;
}

boolean_t lisa_wifi_arg_str(const char *name, char *value, int size);
boolean_t
lisa_wifi_arg_str(const char *name, char *value, int size)
{
	if (PE_parse_boot_arg_str(name, value, size)) {
		return TRUE;
	}
	const char *p = wifi_arg_find(name);
	if (p == NULL || *p != '=' || size <= 0) {
		return FALSE;
	}
	p++;
	int i = 0;
	while (i < size - 1 && p[i] && p[i] != ' ' && p[i] != '\n') {
		value[i] = p[i];
		i++;
	}
	value[i] = 0;
	return TRUE;
}

static void
wifi_wait(uint32_t ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline((event_t)&fw_count, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
wifi_starter(__unused void *p, __unused wait_result_t w)
{
	uint64_t size = 0;
	unsigned waited = 0;

	while (redwood_image_file("fw", "wpss.mdt", &size, NULL) == NULL && waited < 240) {
		wifi_wait(1000);
		waited++;
	}
	uint32_t on = 0, steps = 0;
	if (size != 0) {
		wifi_args = (const char *)redwood_image_file("fw", "bootargs", &wifi_args_size, NULL);
		(void)lisa_wifi_argn("wifi", &on, sizeof(on));
		(void)lisa_wifi_argn("wifi_step", &steps, sizeof(steps));
	}
	if (size == 0) {
		printf("lisa wifi: no /usr/local/lib/lisa/fw/wpss.mdt after %u s: no Wi-Fi\n", waited);
	} else if (on == 0 && steps == 0) {
		printf("lisa wifi: firmware present; Wi-Fi not asked for (wifi=1 or wifi_step=N, boot-args or fw/bootargs)\n");
	} else {
		printf("lisa wifi: firmware found after %u s (wpss.mdt %llu bytes): starting upstream's bring-up\n", waited, size);
		redwood_net_init();
		redwood_wifi_init();
	}
	if (size != 0) redwood_adsp_init();
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
lisa_wifi_start(void)
{
	uint32_t off = 0;
	thread_t thread;

	/* started always: whether Wi-Fi is wanted is also in fw/bootargs, read once the root is there */
	if (PE_parse_boot_argn("nowifi", &off, sizeof(off)) && off) {
		return;
	}
	if (kernel_thread_start(wifi_starter, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
#endif
