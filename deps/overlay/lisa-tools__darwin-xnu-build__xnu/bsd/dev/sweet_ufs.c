/*
 * SWEET internal UFS root device.
 *
 * The Qualcomm UFS host (UFSHCI 2.1) is left enabled with the link up by the
 * bootloader, which loaded our boot image from it. This driver reuses that
 * state: it points the transfer request list at its own memory (verifying the
 * host took it before ringing any doorbell) and issues polled SCSI commands,
 * one at a time, to LUN 0. It finds one GPT partition by name (boot-arg
 * ufsroot=, default "userdata") and exposes it as the BSD disk ufsroot0
 * (rufsroot0), with 512-byte logical sectors over the device's 4 KiB blocks.
 * Root on it with rd=ufsroot0.
 *
 * All DMA goes through one physically contiguous allocation (descriptors and
 * a 1 MiB bounce buffer); the host is not cache coherent, so caches are
 * cleaned/invalidated around each command.
 *
 * Writes are refused unless boot-arg ufsrw=1 is set. Every write is checked
 * against the partition bounds again right before it is issued, so nothing
 * outside the named partition can be written.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <libkern/libkern.h>
#include <machine/atomic.h>

extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);
/* osfmk/arm/machine_routines.h, not exported to the BSD layer */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
/* osfmk/arm/cpu_data.h, not exported to the BSD layer */
extern int get_preemption_level(void);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern void thread_deallocate(thread_t);
extern int hz;
static void sweet_log_start(void);
extern void sweet_sample_start(void);
extern void lisa_status_start(void);
extern void sweet_keys_init(void);
extern void lisa_cpufreq_init(void);
extern void lisa_dsi_init(void);
extern void lisa_wifi_start(void);
extern void sweet_touch_init(void);
extern void lisa_touch_init(void);
extern void lisa_adreno_probe(void);
extern void lisa_gpu_firmware_probe(void);
extern void lisa_adreno_init(void);
extern void sweet_usb_probe(void);

#define REG_CAP         0x00
#define REG_IS          0x20
#define REG_IE          0x24
#define REG_HCS         0x30
#define REG_HCE         0x34
#define REG_UTRLBA      0x50
#define REG_UTRLBAU     0x54
#define REG_UTRLDBR     0x58
#define REG_UTRLCLR     0x5c
#define REG_UTRLRSR     0x60

#define HCS_DP          (1u << 0)
#define HCS_UTRLRDY     (1u << 1)
#define CAP_64AS        (1u << 24)              /* 64-bit DMA addressing */

#define UFS_BLOCK       4096u
#define BOUNCE_SIZE     (1u << 20)
#define PRDT_CHUNK      (256u << 10)            /* PRDT byte count field: 18 bits */
#define PRDT_MAX        (BOUNCE_SIZE / PRDT_CHUNK)
#define CMD_TIMEOUT_NS  (5ull * NSEC_PER_SEC)
#define CMD_SPIN_NS     (50ull * NSEC_PER_USEC)
#define CMD_SLEEP_NS    (200ull * NSEC_PER_USEC)

struct utrd {
	uint32_t dw0, dw1, dw2, dw3;
	uint32_t ucdba, ucdbau;
	uint32_t resp;          /* [31:16] response UPIU offset, [15:0] length, DWORDs */
	uint32_t prdt;          /* [31:16] PRDT offset (DWORDs), [15:0] entries */
};

struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;
	} prdt[PRDT_MAX];
};

/* Layout of the contiguous DMA allocation. */
#define DMA_UTRL        0x0000                  /* 1 KiB aligned */
#define DMA_UCD         0x1000                  /* 128 byte aligned */
#define DMA_BOUNCE      0x4000
#define DMA_SIZE        (DMA_BOUNCE + BOUNCE_SIZE)

static struct {
	vm_offset_t regs;
	vm_offset_t dma;                        /* kernel VA of the DMA allocation */
	uint64_t dma_pa;
	uint8_t lun;
	uint8_t tag;
	uint64_t first_block, nblocks;          /* partition, in 4 KiB device blocks */
	uint64_t device_blocks;                 /* verified READ CAPACITY(10) geometry */
	uint32_t secsize;                       /* logical sector size for the BSD layer */
	bool writable;                          /* boot-arg ufsrw=1 */
	bool cache;                             /* boot-arg ufscache=1 */
	bool wait;                              /* boot-arg ufswait=1 */
	bool perf;                              /* boot-arg lisa_ufs_perf=1 */
	bool faulted;                           /* timed-out DMA: never reuse slot or buffers */
	int bmajor, cmajor;
	dev_t bdev;
	bool ready;
} su;

static LCK_GRP_DECLARE(sweet_ufs_grp, "sweet_ufs");
static LCK_MTX_DECLARE(sweet_ufs_lock, &sweet_ufs_grp);

static inline uint32_t
ufs_rd(uint32_t off)
{
	return *(volatile uint32_t *)(su.regs + off);
}

static inline void
ufs_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(su.regs + off) = v;
}

static void
dma_sync(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(su.dma_pa + off), len, TRUE);
}

static void
dma_clean(uint32_t off, uint32_t len)
{
	clean_dcache64((addr64_t)(su.dma_pa + off), len, TRUE);
}

/*
 * One SCSI command in slot 0, data (if any) to/from the bounce buffer
 * (to the device when write is set). Returns 0 or an errno.
 */
static int
ufs_scsi(const uint8_t *cdb, unsigned cdb_len, uint32_t len, bool write)
{
	/* A timeout can leave the controller owning the descriptor and bounce
	 * buffer even after UTRLCLR. Do not touch either again without a full,
	 * verified controller recovery, which this driver does not implement. */
	if (su.faulted) {
		return EIO;
	}
	uint64_t perf_start = su.perf && su.ready ? mach_absolute_time() : 0;
	struct utrd *utrd = (struct utrd *)(su.dma + DMA_UTRL);
	struct ucd *ucd = (struct ucd *)(su.dma + DMA_UCD);
	uint64_t ucd_pa = su.dma_pa + DMA_UCD, bounce_pa = su.dma_pa + DMA_BOUNCE;

	assert(len <= BOUNCE_SIZE);
	bzero(utrd, sizeof(*utrd));
	bzero(ucd, sizeof(*ucd));

	ucd->cmd[0] = 0x01;                     /* COMMAND UPIU */
	ucd->cmd[1] = len ? (write ? 0x20 : 0x40) : 0x00;      /* W: data out, R: data in */
	ucd->cmd[2] = su.lun;
	ucd->cmd[3] = ++su.tag;
	ucd->cmd[12] = (uint8_t)(len >> 24);
	ucd->cmd[13] = (uint8_t)(len >> 16);
	ucd->cmd[14] = (uint8_t)(len >> 8);
	ucd->cmd[15] = (uint8_t)len;
	memcpy(&ucd->cmd[16], cdb, cdb_len);

	/* DD: 1 = memory to device, 2 = device to memory. */
	utrd->dw0 = (1u << 28) | ((len ? (write ? 1u : 2u) : 0u) << 25) | (1u << 24);
	utrd->dw2 = 0x0f;                       /* OCS invalid until updated */
	utrd->ucdba = (uint32_t)ucd_pa;
	utrd->ucdbau = (uint32_t)(ucd_pa >> 32);
	utrd->resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uint32_t n = 0;
		for (uint32_t done = 0; done < len; done += PRDT_CHUNK, n++) {
			uint32_t chunk = MIN(PRDT_CHUNK, len - done);
			ucd->prdt[n].addr_lo = (uint32_t)(bounce_pa + done);
			ucd->prdt[n].addr_hi = (uint32_t)((bounce_pa + done) >> 32);
			ucd->prdt[n].dbc = chunk - 1;
		}
		utrd->prdt = (1024u / 4) << 16 | n;
	}

	if (su.cache) {
		dma_clean(DMA_UTRL, sizeof(*utrd));
		dma_clean(DMA_UCD, 16 + cdb_len);
		dma_sync(DMA_UCD + offsetof(struct ucd, resp), sizeof(ucd->resp));
		if (len) {
			dma_clean(DMA_UCD + offsetof(struct ucd, prdt),
			    (utrd->prdt & 0xffff) * sizeof(ucd->prdt[0]));
		}
	} else {
		dma_sync(DMA_UTRL, sizeof(*utrd));
		dma_sync(DMA_UCD, sizeof(*ucd));
	}
	if (len) {
		if (su.cache && write) {
			dma_clean(DMA_BOUNCE, len);
		} else {
			dma_sync(DMA_BOUNCE, len);
		}
	}
	__builtin_arm_dsb(0xf);

	ufs_wr(REG_UTRLDBR, 1);
	uint64_t deadline;
	nanoseconds_to_absolutetime(CMD_TIMEOUT_NS, &deadline);
	deadline += mach_absolute_time();
	bool sleepable = su.wait && su.ready && ml_get_interrupts_enabled() &&
	    !ml_at_interrupt_context() && get_preemption_level() == 0;
	uint64_t spin_until = 0, sleep_interval = 0;
	if (sleepable) {
		nanoseconds_to_absolutetime(CMD_SPIN_NS, &spin_until);
		spin_until += mach_absolute_time();
		nanoseconds_to_absolutetime(CMD_SLEEP_NS, &sleep_interval);
	}
	while (ufs_rd(REG_UTRLDBR) & 1) {
		uint64_t now = mach_absolute_time();
		if (now > deadline) {
			su.faulted = true;
			ufs_wr(REG_UTRLCLR, ~1u);
			printf("LISA UFS: command 0x%02x timed out; controller quarantined\n", cdb[0]);
			return ETIMEDOUT;
		}
		if (sleepable && now >= spin_until) {
			uint64_t wake = now + MIN(sleep_interval, deadline - now);
			/* Keep the mutex: slot 0 and the bounce buffer are still owned. */
			if (assert_wait_deadline_with_leeway((event_t)&su.wait, THREAD_UNINT,
			    TIMEOUT_URGENCY_SYS_CRITICAL, wake, 0) == THREAD_WAITING) {
				thread_block(THREAD_CONTINUE_NULL);
			}
		} else {
			delay(1);
		}
	}
	__builtin_arm_dsb(0xf);
	ufs_wr(REG_IS, ufs_rd(REG_IS));         /* acknowledge completion status */

	dma_sync(DMA_UTRL, sizeof(*utrd));
	if (su.cache) {
		dma_sync(DMA_UCD + offsetof(struct ucd, resp), sizeof(ucd->resp));
	} else {
		dma_sync(DMA_UCD, sizeof(*ucd));
	}
	if (len && !(su.cache && write)) {
		dma_sync(DMA_BOUNCE, len);
	}

	uint32_t ocs = utrd->dw2 & 0xff;
	if (ocs || ucd->resp[0] != 0x21 || ucd->resp[6] || ucd->resp[7]) {
		printf("SWEET UFS: command 0x%02x failed: ocs 0x%x upiu 0x%x response 0x%x status 0x%x\n",
		    cdb[0], ocs, ucd->resp[0], ucd->resp[6], ucd->resp[7]);
		return EIO;
	}
	if (perf_start) {
		static uint64_t perf_total_us, perf_max_us, perf_bytes;
		static uint32_t perf_count;
		uint64_t ns = 0;
		absolutetime_to_nanoseconds(mach_absolute_time() - perf_start, &ns);
		uint64_t us = ns / 1000;
		perf_total_us += us;
		perf_max_us = MAX(perf_max_us, us);
		perf_bytes += len;
		if (++perf_count == 256) {
			printf("LISA UFS PERF: 256 cmds avg %llu us max %llu us bytes %llu\n",
			    perf_total_us / perf_count, perf_max_us, perf_bytes);
			perf_count = 0;
			perf_total_us = perf_max_us = perf_bytes = 0;
		}
	}
	return 0;
}

/* Validate the LUN geometry before trusting any on-disk partition metadata. */
static int
ufs_read_capacity(void)
{
	static const uint8_t cdb[10] = { 0x25 }; /* READ CAPACITY(10) */
	su.device_blocks = 0;
	int error = ufs_scsi(cdb, sizeof(cdb), 8, false);
	if (error) {
		return error;
	}
	const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
	uint32_t last = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
	    ((uint32_t)b[2] << 8) | b[3];
	uint32_t size = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) |
	    ((uint32_t)b[6] << 8) | b[7];
	/* 0xffffffff requests READ CAPACITY(16), which is not implemented. */
	if (last == UINT32_MAX || last < 2 || size != UFS_BLOCK) {
		printf("LISA UFS: unsupported LUN geometry: last LBA 0x%x block size %u\n", last, size);
		return ENXIO;
	}
	su.device_blocks = (uint64_t)last + 1;
	return 0;
}

/* READ(10) of count device blocks at lba into the bounce buffer. */
static int
ufs_read_blocks(uint64_t lba, uint32_t count)
{
	if (!count || !su.device_blocks || lba >= su.device_blocks ||
	    count > su.device_blocks - lba || lba > UINT32_MAX ||
	    count > BOUNCE_SIZE / UFS_BLOCK || count - 1 > UINT32_MAX - lba) {
		return EINVAL;
	}
	uint8_t cdb[10] = {
		0x28, 0,
		(uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
		0, (uint8_t)(count >> 8), (uint8_t)count, 0
	};
	return ufs_scsi(cdb, sizeof(cdb), count * UFS_BLOCK, false);
}

/*
 * WRITE(10) of count device blocks from the bounce buffer to lba. The range
 * must lie inside the partition: this is the only place data goes out.
 */
static int
ufs_write_blocks(uint64_t lba, uint32_t count)
{
	if (!su.writable || !su.nblocks || !su.device_blocks || lba >= su.device_blocks ||
	    count > su.device_blocks - lba || count == 0 || count > BOUNCE_SIZE / UFS_BLOCK ||
	    lba < su.first_block || lba - su.first_block >= su.nblocks ||
	    count > su.nblocks - (lba - su.first_block) || lba + count - 1 > UINT32_MAX) {
		printf("SWEET UFS: refusing write of %u blocks at 0x%llx\n", count, lba);
		return EPERM;
	}
	uint8_t cdb[10] = {
		0x2a, 0,
		(uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
		0, (uint8_t)(count >> 8), (uint8_t)count, 0
	};
	return ufs_scsi(cdb, sizeof(cdb), count * UFS_BLOCK, true);
}

static int
ufs_sync_cache(void)
{
	static const uint8_t cdb[10] = { 0x35 };       /* SYNCHRONIZE CACHE(10), whole LUN */
	return ufs_scsi(cdb, sizeof(cdb), 0, false);
}

static int
ufs_find_partition(const char *want)
{
	const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
	int error = ufs_read_blocks(1, 1);
	if (error) {
		return error;
	}
	if (memcmp(b, "EFI PART", 8) != 0) {
		printf("SWEET UFS: LUN %u has no GPT\n", su.lun);
		return ENXIO;
	}
	uint64_t entry_lba, current, backup, first_usable, last_usable;
	uint32_t nent, esz, header_size, header_crc, entries_crc;
	memcpy(&header_size, b + 12, 4);
	memcpy(&header_crc, b + 16, 4);
	memcpy(&current, b + 24, 8);
	memcpy(&backup, b + 32, 8);
	memcpy(&first_usable, b + 40, 8);
	memcpy(&last_usable, b + 48, 8);
	memcpy(&entry_lba, b + 72, 8);
	memcpy(&nent, b + 80, 4);
	memcpy(&esz, b + 84, 4);
	memcpy(&entries_crc, b + 88, 4);
	if (header_size < 92 || header_size > UFS_BLOCK || current != 1 ||
	    backup != su.device_blocks - 1 || first_usable > last_usable ||
	    last_usable >= backup || entry_lba < 2 || esz != 128 ||
	    nent == 0 || nent > BOUNCE_SIZE / 128) {
		printf("LISA UFS: invalid GPT header or entry geometry\n");
		return ENXIO;
	}
	uint32_t zero = 0;
	uint32_t crc = crc32(0, b, 16);
	crc = crc32(crc, (const uint8_t *)&zero, sizeof(zero));
	crc = crc32(crc, b + 20, header_size - 20);
	uint32_t entry_bytes = nent * esz;
	uint32_t entry_blocks = (entry_bytes + UFS_BLOCK - 1) / UFS_BLOCK;
	if (crc != header_crc || entry_lba >= first_usable ||
	    entry_blocks > first_usable - entry_lba) {
		printf("LISA UFS: GPT header checksum or range invalid\n");
		return ENXIO;
	}
	error = ufs_read_blocks(entry_lba, entry_blocks);
	if (error) {
		return error;
	}
	if (crc32(0, b, entry_bytes) != entries_crc) {
		printf("LISA UFS: GPT entry checksum invalid\n");
		return ENXIO;
	}
	bool found = false;
	uint64_t selected_first = 0, selected_last = 0;
	for (uint32_t i = 0; i < nent; i++) {
		const uint8_t *e = b + i * esz;
		char name[37];
		unsigned c;
		for (c = 0; c < 36; c++) {
			uint16_t ch = (uint16_t)(e[56 + 2 * c] | e[57 + 2 * c] << 8);
			if (ch > 0x7f) {
				name[0] = 0;
				c = 0;
				break;
			}
			if (ch == 0) {
				break;
			}
			name[c] = (char)ch;
		}
		name[c] = 0;
		if (strcmp(name, want) != 0) {
			continue;
		}
		uint64_t first, last;
		memcpy(&first, e + 32, 8);
		memcpy(&last, e + 40, 8);
		bool used = false;
		for (unsigned j = 0; j < 16; j++) {
			used |= e[j] != 0;
		}
		if (!used || found || first < first_usable || last > last_usable || last < first) {
			printf("LISA UFS: invalid or duplicate selected GPT partition\n");
			return ENXIO;
		}
		found = true;
		selected_first = first;
		selected_last = last;
	}
	if (found) {
		su.first_block = selected_first;
		su.nblocks = selected_last - selected_first + 1;
		return 0;
	}
	printf("SWEET UFS: no GPT partition named \"%s\"\n", want);
	return ENOENT;
}

static int
ufs_take_over_host(void)
{
	uint32_t hce = ufs_rd(REG_HCE), hcs = ufs_rd(REG_HCS);
	if (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {
		printf("SWEET UFS: host not ready (hce 0x%x hcs 0x%x)\n", hce, hcs);
		return ENXIO;
	}
	if (ufs_rd(REG_UTRLDBR)) {
		printf("SWEET UFS: requests outstanding from the bootloader\n");
		return EBUSY;
	}
	/* With RAM above 4 GiB (mkadt.py --highmem) the DMA memory can be there too. */
	if (((su.dma_pa + DMA_SIZE - 1) >> 32) && !(ufs_rd(REG_CAP) & CAP_64AS)) {
		printf("SWEET UFS: DMA memory at 0x%llx but the host has no 64-bit addressing\n",
		    su.dma_pa);
		return ENXIO;
	}
	ufs_wr(REG_IE, 0);                      /* polled operation */
	ufs_wr(REG_IS, ufs_rd(REG_IS));
	ufs_wr(REG_UTRLRSR, 0);
	uint64_t list = su.dma_pa + DMA_UTRL;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	/* Never ring a doorbell unless the host uses our list. */
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list || ufs_rd(REG_UTRLBAU) != (uint32_t)(list >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1)) {
		printf("SWEET UFS: host did not take our request list\n");
		return EIO;
	}
	return 0;
}

static int
sweet_ufs_open(__unused dev_t dev, int flags, __unused int devtype, __unused struct proc *p)
{
	if (!su.ready) {
		return ENXIO;
	}
	return ((flags & FWRITE) && !su.writable) ? EROFS : 0;
}

static int
sweet_ufs_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
}

static void
sweet_ufs_strategy(buf_t bp)
{
	int error = 0;
	caddr_t vaddr = NULL;
	uint64_t off = 0;
	uint32_t count = buf_count(bp);
	uint64_t part_bytes = su.nblocks * UFS_BLOCK;

	bool rd = buf_flags(bp) & B_READ;

	buf_setresid(bp, count);
	if (buf_blkno(bp) < 0 || (uint64_t)buf_blkno(bp) > UINT64_MAX / su.secsize ||
	    su.nblocks > UINT64_MAX / UFS_BLOCK) {
		error = EINVAL;
		goto done;
	}
	off = (uint64_t)buf_blkno(bp) * su.secsize;
	if (!rd && !su.writable) {
		error = EROFS;
		goto done;
	}
	if (off > part_bytes || count > part_bytes - off) {
		error = EINVAL;
		goto done;
	}
	if (count == 0) {
		goto done;
	}
	if (buf_map(bp, &vaddr)) {
		error = EIO;
		goto done;
	}

	lck_mtx_lock(&sweet_ufs_lock);
	for (uint32_t done = 0; done < count;) {
		uint64_t pos = off + done;
		uint64_t lba = pos / UFS_BLOCK;
		uint32_t skip = (uint32_t)(pos % UFS_BLOCK);
		uint32_t want = MIN(count - done, BOUNCE_SIZE - skip);
		uint32_t blocks = (skip + want + UFS_BLOCK - 1) / UFS_BLOCK;
		void *bounce = (void *)(su.dma + DMA_BOUNCE);
		if (rd) {
			error = ufs_read_blocks(su.first_block + lba, blocks);
			if (!error) {
				memcpy(vaddr + done, (const char *)bounce + skip, want);
			}
		} else {
			/* Partial device blocks at either end: read, merge, write back. */
			if (skip || (want % UFS_BLOCK)) {
				error = ufs_read_blocks(su.first_block + lba, blocks);
			}
			if (!error) {
				memcpy((char *)bounce + skip, vaddr + done, want);
				error = ufs_write_blocks(su.first_block + lba, blocks);
			}
		}
		if (error) {
			break;
		}
		done += want;
		buf_setresid(bp, count - done);
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	buf_unmap(bp);

done:
	if (error) {
		buf_seterror(bp, error);
	}
	buf_biodone(bp);
}

static int
sweet_ufs_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag,
    __unused struct proc *p, int is_char)
{
	uint32_t *f = (uint32_t *)data;
	uint64_t *o = (uint64_t *)data;

	switch (cmd) {
	case DKIOCGETBLOCKSIZE:
		*f = su.secsize;
		return 0;
	case DKIOCSETBLOCKSIZE:
		if (is_char) {
			return ENODEV;
		}
		if (*f < DEV_BSIZE || *f > UFS_BLOCK || (*f & (*f - 1))) {
			return EINVAL;
		}
		su.secsize = *f;
		return 0;
	case DKIOCGETPHYSICALBLOCKSIZE:
		*f = UFS_BLOCK;
		return 0;
	case DKIOCGETBLOCKCOUNT:
		*o = su.nblocks * UFS_BLOCK / su.secsize;
		return 0;
	case DKIOCISWRITABLE:
		*f = su.writable;
		return 0;
	case DKIOCSYNCHRONIZE:
	case DKIOCSYNCHRONIZECACHE: {
		if (!su.writable) {
			return 0;
		}
		lck_mtx_lock(&sweet_ufs_lock);
		int error = ufs_sync_cache();
		lck_mtx_unlock(&sweet_ufs_lock);
		return error;
	}
	case DKIOCGETMAXBLOCKCOUNTREAD:
		*o = BOUNCE_SIZE / su.secsize;
		return 0;
	case DKIOCGETMAXBLOCKCOUNTWRITE:
		*o = su.writable ? BOUNCE_SIZE / su.secsize : 0;
		return 0;
	case DKIOCGETMAXSEGMENTCOUNTREAD:
	case DKIOCGETMAXSEGMENTCOUNTWRITE:
		*o = 1;
		return 0;
	default:
		return ENOTTY;
	}
}

static int
sweet_ufs_bioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return sweet_ufs_ioctl(dev, cmd, data, flag, p, 0);
}

static int
sweet_ufs_cioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return sweet_ufs_ioctl(dev, cmd, data, flag, p, 1);
}

static int
sweet_ufs_psize(__unused dev_t dev)
{
	return su.ready ? (int)MIN(su.nblocks * UFS_BLOCK / su.secsize, INT_MAX) : -1;
}

static int
sweet_ufs_read(dev_t dev, struct uio *uio, __unused int ioflag)
{
	return physio(sweet_ufs_strategy, NULL, dev, B_READ, minphys, uio, (int)su.secsize);
}

static int
sweet_ufs_write(dev_t dev, struct uio *uio, __unused int ioflag)
{
	if (!su.writable) {
		return EROFS;
	}
	return physio(sweet_ufs_strategy, NULL, dev, B_WRITE, minphys, uio, (int)su.secsize);
}

static const struct bdevsw sweet_ufs_bdevsw = {
	.d_open     = sweet_ufs_open,
	.d_close    = sweet_ufs_close,
	.d_strategy = sweet_ufs_strategy,
	.d_ioctl    = sweet_ufs_bioctl,
	.d_dump     = eno_dump,
	.d_psize    = sweet_ufs_psize,
	.d_type     = D_DISK,
};

static const struct cdevsw sweet_ufs_cdevsw = {
	.d_open       = sweet_ufs_open,
	.d_close      = sweet_ufs_close,
	.d_read       = sweet_ufs_read,
	.d_write      = sweet_ufs_write,
	.d_ioctl      = sweet_ufs_cioctl,
	.d_stop       = eno_stop,
	.d_reset      = eno_reset,
	.d_ttys       = NULL,
	.d_select     = eno_select,
	.d_mmap       = eno_mmap,
	.d_strategy   = eno_strat,
	.d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc,
	.d_type       = D_DISK,
};

/* Read-only, conservative check of the firmware's visible UFS SMMU route.
 * This cannot prove hidden-stage security policy or safe controller ownership. */
static int
lisa_ufs_smmu_check(volatile const uint32_t *smmu)
{
	uint32_t id0 = smmu[0x20 / 4], id1 = smmu[0x24 / 4];
	unsigned count = id0 & 0xffu, banks = id1 & 0xffu;
	if (!(id0 & (1u << 27)) || !count || count > 128 || !banks) {
		return ENXIO;
	}
	unsigned matches = 0, bank = 0;
	for (unsigned i = 0; i < count; i++) {
		uint32_t smr = smmu[(0x800 + 4 * i) / 4];
		if (!(smr & (1u << 31))) continue;
		uint32_t mask = (smr >> 16) & 0x7fffu, id = smr & 0xffffu;
		if ((0x80u & ~mask & 0xffffu) != (id & ~mask & 0xffffu)) continue;
		if (mask != 0 || id != 0x80u) return ENXIO;
		uint32_t s2cr = smmu[(0xc00 + 4 * i) / 4];
		if (((s2cr >> 16) & 3u) != 0 || (s2cr & 0xffu) >= banks) return ENXIO;
		bank = s2cr & 0xffu;
		matches++;
	}
	if (matches != 1) return ENXIO;
	for (unsigned i = 0; i < count; i++) {
		uint32_t smr = smmu[(0x800 + 4 * i) / 4];
		if (!(smr & (1u << 31))) continue;
		uint32_t mask = (smr >> 16) & 0x7fffu, id = smr & 0xffffu;
		if ((0x80u & ~mask & 0xffffu) == (id & ~mask & 0xffffu)) continue;
		uint32_t s2cr = smmu[(0xc00 + 4 * i) / 4];
		if (((s2cr >> 16) & 3u) == 0 && (s2cr & 0xffu) == bank) return ENXIO;
	}
	unsigned shift = id1 & (1u << 31) ? 16 : 12;
	unsigned pages = 1u << (((id1 >> 28) & 7u) + 1);
	uint64_t cb = ((uint64_t)pages + bank) << shift;
	uint64_t cbar = ((uint64_t)1 << shift) + 4 * bank;
	if (cb > 0x100000u - 0x5cu || cbar > 0x100000u - 4u) return ENXIO;
	uint32_t ctl = smmu[cb / 4], fsr = smmu[(cb + 0x58) / 4];
	uint32_t cbtype = (smmu[cbar / 4] >> 16) & 3u;
	/* FSR[10:9] is format, not a fault; reject all fault and stalled bits. */
	if ((ctl & 1u) || cbtype != 1u || (fsr & 0xc00001feu)) return ENXIO;
	return 0;
}

static int
sweet_ufs_init(void)
{
	DTEntry node;
	uintptr_t const *reg = NULL;
	unsigned int size = 0;
	char want[64] = "userdata";
	int error;

	/* Experimental opt-in. Keep the default fully disabled. */
	uint32_t lisa_probe = 0;
	PE_parse_boot_argn("lisa_ufs_probe", &lisa_probe, sizeof(lisa_probe));
	if (lisa_probe != 1) return ENXIO;
	vm_offset_t smmu = ml_io_map(0x15000000, 0x100000);
	if (!smmu || lisa_ufs_smmu_check((volatile const uint32_t *)smmu)) {
		printf("LISA UFS: SMMU preflight rejected\n");
		return ENXIO;
	}

	if (SecureDTLookupEntry(NULL, "/sweet-ufs", &node) != kSuccess ||
	    SecureDTGetProperty(node, "reg", (void const **)&reg, &size) != kSuccess ||
	    size < 2 * sizeof(uintptr_t)) {
		printf("SWEET UFS: no /sweet-ufs node in the device tree\n");
		return ENXIO;
	}
	PE_parse_boot_argn("ufsroot", want, sizeof(want));
	uint32_t rw = 0;
	PE_parse_boot_argn("ufsrw", &rw, sizeof(rw));
	uint32_t lisa_write = 0;
	PE_parse_boot_argn("lisa_ufs_write", &lisa_write, sizeof(lisa_write));
	if (rw && (rw != 1 || lisa_write != 1 || strcmp(want, "userdata") != 0)) {
		printf("LISA UFS: writable access requires explicit userdata opt-in\n");
		return EROFS;
	}
	/* Sleepable callers yield after 50us while the command owns slot0, rather
     * than burning a core for the full device latency. ufswait=0 restores polling. */
    uint32_t cache = 0, wait = 1;
	PE_parse_boot_argn("ufscache", &cache, sizeof(cache));
	PE_parse_boot_argn("ufswait", &wait, sizeof(wait));
	su.cache = (cache == 1);
	su.wait = (wait == 1);
	uint32_t perf = 0;
	PE_parse_boot_argn("lisa_ufs_perf", &perf, sizeof(perf));
	su.perf = (perf == 1);

	su.regs = ml_io_map(reg[0], reg[1]);
	if (kmem_alloc_contig(kernel_map, &su.dma, DMA_SIZE, PAGE_MASK, 0, 0,
	    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("SWEET UFS: no contiguous DMA memory\n");
		return ENOMEM;
	}
	ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)su.dma);
	if (!pn) {
		return ENOMEM;
	}
	su.dma_pa = ptoa_64(pn);
	su.lun = 0;
	su.secsize = DEV_BSIZE;

	lck_mtx_lock(&sweet_ufs_lock);
	error = ufs_take_over_host();
	if (!error) {
		static const uint8_t tur[6] = { 0x00 };
		/* The first command after the bootloader may report a unit attention. */
		for (int i = 0; i < 3 && (error = ufs_scsi(tur, sizeof(tur), 0, false)); i++) {
		}
	}
	if (!error) {
		error = ufs_read_capacity();
	}
	if (!error) {
		error = ufs_find_partition(want);
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	if (error) {
		return error;
	}

	su.bmajor = bdevsw_add(-1, &sweet_ufs_bdevsw);
	if (su.bmajor < 0) {
		return ENXIO;
	}
	su.cmajor = cdevsw_add_with_bdev(-1, &sweet_ufs_cdevsw, su.bmajor);
	if (su.cmajor < 0) {
		return ENXIO;
	}
	su.bdev = makedev(su.bmajor, 0);
	devfs_make_node(su.bdev, DEVFS_BLOCK, UID_ROOT, GID_OPERATOR, 0640, "ufsroot0");
	devfs_make_node(makedev(su.cmajor, 0), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, 0640, "rufsroot0");
	/* Only now, with the partition bounds known, can writes be allowed. */
	su.writable = (rw != 0);
	su.ready = true;
	printf("SWEET UFS: \"%s\" on LUN %u: first block 0x%llx, %llu MiB, %s, DMA at 0x%llx\n",
	    want, su.lun, su.first_block, su.nblocks * UFS_BLOCK >> 20,
	    su.writable ? "read-write" : "read-only", su.dma_pa);
	uint32_t lisa_status = 0;
	PE_parse_boot_argn("lisa_status", &lisa_status, sizeof(lisa_status));
	if (lisa_status == 1) lisa_status_start();
	uint32_t lisa_sample = 0;
	PE_parse_boot_argn("lisa_sample", &lisa_sample, sizeof(lisa_sample));
	if (lisa_sample == 1) sweet_sample_start();
	uint32_t lisa_log = 0;
	PE_parse_boot_argn("lisa_ufs_log", &lisa_log, sizeof(lisa_log));
	if (lisa_log == 1) sweet_log_start();
	/* Opt-in Lisa PMK8350 key polling; USB and touch remain independent. */
	lisa_cpufreq_init();
	lisa_dsi_init();
	/* USB handover precedes WPSS, which allocates another apps-SMMU context. */
	sweet_usb_probe();
	lisa_wifi_start();
	sweet_keys_init();
	lisa_touch_init();
	lisa_adreno_probe();
	lisa_gpu_firmware_probe();
	/* Full GPU bring-up stays explicit.  Accept gpu=1 as the public switch used
	 * by the staged Lisa driver, rather than requiring a second private flag. */
	uint32_t lisa_adreno = 0, gpu = 0;
	PE_parse_boot_argn("lisa_adreno", &lisa_adreno, sizeof(lisa_adreno));
	PE_parse_boot_argn("gpu", &gpu, sizeof(gpu));
	if (lisa_adreno == 1 || gpu == 1) {
		printf("LISA GPU: staged A642L bring-up requested (build 20261024)\n");
		lisa_adreno_init();
	}
	return 0;
}

/*
 * Persistent console log ("sweetlog"). Filming the phone screen is the only
 * other way to see the console, so every character that reaches the
 * console (kernel printf and /dev/console writes of daemons, both through
 * _cnputs) is also kept in a 4 MiB ring, and once the partition is known
 * and writable a thread copies new ring blocks to the LAST 4 MiB + 4 KiB of
 * the partition every ufslogsecs seconds (default 10), far behind the HFS
 * image at its start. Device block 0 of that area is a header:
 *   "SWEETLOG" | u32 version 1 | u32 ring size | u64 total bytes written
 *   | u64 flush sequence
 * ring offset of byte n = n % ring size. Read it from a recovery with
 * tools/sweet-log-pull.sh. Disabled with boot-arg sweetlog=0.
 */
#define SWEET_LOG_SIZE          (4u << 20)
#define SWEET_LOG_BLOCKS        (1 + SWEET_LOG_SIZE / UFS_BLOCK)
#define SWEET_LOG_SECS          10u

static char sweet_log_ring[SWEET_LOG_SIZE];
static uint64_t _Atomic sweet_log_total;
static int sweet_log_chan;
static uint32_t sweet_log_secs = SWEET_LOG_SECS;

/* Called for all console output, any context: never blocks. */
void sweet_log_append(const char *c, int size);

void
sweet_log_append(const char *c, int size)
{
	if (size <= 0) {
		return;
	}
	uint64_t at = os_atomic_add_orig(&sweet_log_total, (uint64_t)size, relaxed);
	for (int i = 0; i < size; i++) {
		sweet_log_ring[(at + (uint64_t)i) % SWEET_LOG_SIZE] = c[i];
	}
}

/*
 * Copy console bytes [*pos, total) (at most max) for live log streaming
 * (sweet_usb.c). A reader that fell more than the ring behind skips ahead.
 * Returns the byte count; *pos advances.
 */
uint32_t sweet_log_copy(uint64_t *pos, char *out, uint32_t max);

uint32_t
sweet_log_copy(uint64_t *pos, char *out, uint32_t max)
{
	uint64_t total = os_atomic_load(&sweet_log_total, relaxed);
	if (total - *pos > SWEET_LOG_SIZE) {
		*pos = total - SWEET_LOG_SIZE;
	}
	uint32_t n = (uint32_t)MIN((uint64_t)max, total - *pos);
	for (uint32_t i = 0; i < n; i++) {
		out[i] = sweet_log_ring[(*pos + i) % SWEET_LOG_SIZE];
	}
	*pos += n;
	return n;
}

uint64_t sweet_log_position(void);

uint64_t
sweet_log_position(void)
{
	return os_atomic_load(&sweet_log_total, relaxed);
}

struct sweet_log_header {
	char magic[8];
	uint32_t version, size;
	uint64_t total, seq;
};

/*
 * Synchronous checkpoint for code that may deliberately exercise a secure
 * boundary before the scheduler can run sweet_log_thread().  It writes the
 * same guarded userdata tail used by the periodic logger; no filesystem
 * blocks are touched.  GPU PAS uses this immediately before an SMC so a
 * reset inside secure world leaves an actionable breadcrumb.
 */
void
sweet_log_checkpoint(void)
{
	static uint64_t seq;
	uint64_t total, pos, base;
	int error = 0;

	if (!su.ready || !su.writable || su.nblocks < 2 * SWEET_LOG_BLOCKS) {
		return;
	}
	total = os_atomic_load(&sweet_log_total, relaxed);
	if (!total) {
		return;
	}
	base = su.first_block + su.nblocks - SWEET_LOG_BLOCKS;
	pos = total > SWEET_LOG_SIZE ? total - SWEET_LOG_SIZE : 0;
	lck_mtx_lock(&sweet_ufs_lock);
	while (pos < total && !error) {
		uint32_t off = (uint32_t)(pos % SWEET_LOG_SIZE);
		uint32_t blk = off / UFS_BLOCK;
		uint64_t need = ((uint64_t)off % UFS_BLOCK + (total - pos) + UFS_BLOCK - 1) / UFS_BLOCK;
		uint32_t n = (uint32_t)MIN(MIN(need, (uint64_t)(SWEET_LOG_SIZE / UFS_BLOCK - blk)),
		    (uint64_t)(BOUNCE_SIZE / UFS_BLOCK));
		memcpy((void *)(su.dma + DMA_BOUNCE), sweet_log_ring + (uint64_t)blk * UFS_BLOCK, n * UFS_BLOCK);
		error = ufs_write_blocks(base + 1 + blk, n);
		pos += (uint64_t)(blk + n) * UFS_BLOCK - off;
	}
	if (!error) {
		struct sweet_log_header h = { .magic = { 'S', 'W', 'E', 'E', 'T', 'L', 'O', 'G' },
			.version = 1, .size = SWEET_LOG_SIZE, .total = total, .seq = ++seq };
		bzero((void *)(su.dma + DMA_BOUNCE), UFS_BLOCK);
		memcpy((void *)(su.dma + DMA_BOUNCE), &h, sizeof(h));
		error = ufs_write_blocks(base, 1);
	}
	if (!error) {
		error = ufs_sync_cache();
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	if (error) {
		printf("SWEET LOG: synchronous checkpoint failed (%d)\n", error);
	}
}

static void
sweet_log_thread(__unused void *arg, __unused wait_result_t wr)
{
	uint64_t base = su.first_block + su.nblocks - SWEET_LOG_BLOCKS;
	uint64_t flushed = 0, seq = 0;
	void *bounce = (void *)(su.dma + DMA_BOUNCE);

	printf("SWEET LOG: console log to the last %u KiB of the partition (block 0x%llx)\n",
	    SWEET_LOG_BLOCKS * UFS_BLOCK >> 10, base);
	for (;;) {
		(void)tsleep(&sweet_log_chan, PZERO, "sweetlog", hz * (int)sweet_log_secs);
		uint64_t total = os_atomic_load(&sweet_log_total, relaxed);
		if (total == flushed) {
			continue;
		}
		uint64_t pos = total - flushed >= SWEET_LOG_SIZE ? total - SWEET_LOG_SIZE : flushed;
		int error = 0;

		lck_mtx_lock(&sweet_ufs_lock);
		while (pos < total && !error) {
			uint32_t off = (uint32_t)(pos % SWEET_LOG_SIZE);
			uint32_t blk = off / UFS_BLOCK;
			uint64_t need = ((uint64_t)off % UFS_BLOCK + (total - pos) + UFS_BLOCK - 1) / UFS_BLOCK;
			uint32_t n = (uint32_t)MIN(MIN(need, (uint64_t)(SWEET_LOG_SIZE / UFS_BLOCK - blk)),
			    (uint64_t)(BOUNCE_SIZE / UFS_BLOCK));
			memcpy(bounce, sweet_log_ring + (uint64_t)blk * UFS_BLOCK, n * UFS_BLOCK);
			error = ufs_write_blocks(base + 1 + blk, n);
			pos += (uint64_t)(blk + n) * UFS_BLOCK - off;
		}
		if (!error) {
			struct sweet_log_header h = {
				.magic = { 'S', 'W', 'E', 'E', 'T', 'L', 'O', 'G' },
				.version = 1, .size = SWEET_LOG_SIZE, .total = total, .seq = ++seq,
			};
			bzero(bounce, UFS_BLOCK);
			memcpy(bounce, &h, sizeof(h));
			error = ufs_write_blocks(base, 1);
		}
		if (!error) {
			error = ufs_sync_cache();
		}
		lck_mtx_unlock(&sweet_ufs_lock);
		if (error) {
			printf("SWEET LOG: write failed (%d), stopping\n", error);
			return;
		}
		flushed = total;
	}
}

static void
sweet_log_start(void)
{
	uint32_t on = 1;
	thread_t thread;

	PE_parse_boot_argn("sweetlog", &on, sizeof(on));
	if (!on || !su.writable || su.nblocks < 2 * SWEET_LOG_BLOCKS) {
		return;
	}
	/* The ring is raw partition tail space, never inside the HFS volume.
	 * Verify the live volume header before permitting any log writes. */
	lck_mtx_lock(&sweet_ufs_lock);
	int err = ufs_read_blocks(su.first_block, 1);
	const uint8_t *h = (const uint8_t *)(su.dma + DMA_BOUNCE + 1024);
	uint32_t block = ((uint32_t)h[40] << 24) | ((uint32_t)h[41] << 16) |
	    ((uint32_t)h[42] << 8) | h[43];
	uint32_t blocks = ((uint32_t)h[44] << 24) | ((uint32_t)h[45] << 16) |
	    ((uint32_t)h[46] << 8) | h[47];
	bool safe = !err && h[0] == 'H' && (h[1] == '+' || h[1] == 'X') &&
	    block >= 512 && !(block & (block - 1)) && blocks &&
	    (uint64_t)block * blocks <= (su.nblocks - SWEET_LOG_BLOCKS) * UFS_BLOCK;
	lck_mtx_unlock(&sweet_ufs_lock);
	if (!safe) {
		printf("LISA LOG: volume bounds unavailable; logging disabled\n");
		return;
	}
	PE_parse_boot_argn("ufslogsecs", &sweet_log_secs, sizeof(sweet_log_secs));
	if (sweet_log_secs == 0 || sweet_log_secs > 60) {
		sweet_log_secs = SWEET_LOG_SECS;
	}
	if (kernel_thread_start(sweet_log_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}

/* Root device for rd=ufsroot0 (IOFindBSDRoot). */
int sweet_ufs_rootdev(dev_t *dev);

int
sweet_ufs_rootdev(dev_t *dev)
{
	if (!su.ready) {
		int error = sweet_ufs_init();
		if (error) {
			return error;
		}
	}
	*dev = su.bdev;
	return 0;
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
