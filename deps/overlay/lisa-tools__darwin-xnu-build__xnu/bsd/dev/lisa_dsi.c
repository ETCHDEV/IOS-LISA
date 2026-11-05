/*
 * Lisa (SM7325) panel commands over DSI: hardware brightness (DCS 0x51) and the 60/90 Hz switch
 * (DCS 0x6C), for the CSOT k9d 42 0d 0b panel (DSC, command mode) that the boot loader leaves
 * running with continuous splash. Opt-in: boot-arg lisadsi=1.
 *
 * Panel facts, from the phone's own live device tree (lisa-firmware/live/lisa-live.dts):
 *   bl_ctrl_dcs, bl-min-level 8, normal max 0x7ff (0xfff = high-brightness mode),
 *   bl-dsc-cmd-state dsi_lp_mode, mi hbm command "39 .. 51 0f ff" (level high byte first);
 *   timing-switch-command 15 .. 02 6c 01 (90 Hz) / 6c 00 (60 Hz); controller
 *   qcom,dsi-ctrl-hw-v2.5 at 0xae94000; the SDE SMMU context (stream 0x900) has
 *   qcom,iommu-earlymap: the boot loader's identity mapping of the splash region
 *   (0xe1000000 + 0x2300000) stays, so a command buffer at its unused tail is reachable by
 *   the controller's DMA at its physical address.
 *
 * Register offsets: Linux drivers/gpu/drm/msm/dsi (dsi.xml, 6G). The controller is checked
 * (version, CTRL in command mode) before anything is sent (lisadsi_force=1 skips that); every send
 * waits for the MDP frame transfer to finish, and the first failure stops all further sends.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <stdbool.h>
#include <string.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t);
extern int lisa_display_is_awake(void);
extern int lisa_brightness_level(void);         /* lisa_cpufreq.c: kern.lisa_brightness, 20..1000 */
void lisa_dsi_init(void);

#define DSI0_PHYS               0xae94000UL
#define DSI_SIZE                0x400UL
#define CMDBUF_PHYS             0xe3200000UL    /* splash region + 0x2200000; the panel uses < 0xa00000 */

/* 6G has a hardware-version word before the common register block.
 * ALL common offsets include +4, per Linux dsi_cfg.h DSI_6G_REG_SHIFT. */
#define REG_HW_VERSION          0x000
#define REG_CTRL                0x004
#define REG_STATUS0             0x008
#define REG_FIFO_STATUS         0x00c
#define REG_CMD_DMA_CTRL        0x03c
#define REG_CMD_CFG0            0x040
#define REG_CMD_CFG1            0x044
#define REG_DMA_BASE            0x048
#define REG_DMA_LEN             0x04c
#define REG_ACK_ERR_STATUS      0x068
#define REG_TRIG_CTRL           0x084
#define REG_TRIG_DMA            0x090
#define REG_DLN0_PHY_ERR        0x0b4
#define REG_TIMEOUT_STATUS      0x0c0
#define REG_EOT_PACKET_CTRL     0x0cc
#define REG_ERR_INT_MASK0       0x10c
#define REG_INTR_CTRL           0x110
#define REG_CLK_CTRL            0x11c
#define REG_VERSION             0x1f4

#define CTRL_ENABLE             (1u << 0)
#define CTRL_CMD_MODE_EN        (1u << 2)
#define STATUS0_DMA_BUSY        (1u << 1)
#define STATUS0_MDP_BUSY        (1u << 2)
#define DMA_FROM_FRAME_BUFFER   (1u << 28)
#define DMA_LOW_POWER           (1u << 26)
#define INTR_CMD_DMA_DONE       (1u << 0)

static volatile uint8_t *dsi;
static volatile uint8_t *cmdbuf;
static int dsi_ok;
static int dsi_failed;
static int lisa_hw_brightness;  /* 1: the panel dims itself; the GPU shader must not */
static int lisa_panel_hz = 60;
int lisa_display_refresh_hz(void);
int lisa_display_refresh_hz(void) { return __atomic_load_n(&lisa_panel_hz, __ATOMIC_ACQUIRE); }
int lisa_display_hardware_brightness(void);
int lisa_display_hardware_brightness(void) { return lisa_hw_brightness; }
SYSCTL_INT(_kern, OID_AUTO, lisa_hw_brightness, CTLFLAG_RD | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    &lisa_hw_brightness, 0, "Lisa: brightness is set in the panel (DCS 0x51)");
SYSCTL_INT(_kern, OID_AUTO, lisa_panel_hz, CTLFLAG_RD | CTLFLAG_ANYBODY | CTLFLAG_LOCKED,
    &lisa_panel_hz, 0, "Lisa: panel refresh rate");

static uint32_t rd(uint32_t off) { return *(volatile uint32_t *)(dsi + off); }
static void wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(dsi + off) = v; __builtin_arm_dsb(0xf); }

static void
dsi_sleep_us(uint32_t us)
{
	uint64_t deadline;
	clock_interval_to_deadline(us, NSEC_PER_USEC, &deadline);
	(void)assert_wait_deadline((event_t)&dsi, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
dsi_dump(const char *when)
{
	printf("LISA DSI: %s: hw %08x ctrl %08x status %08x fifo %08x dmactl %08x cfg0 %08x cfg1 %08x "
	    "base %08x len %08x ackerr %08x trig %08x phyerr %08x timeout %08x eot %08x errmask %08x "
	    "intr %08x clk %08x ver %08x\n", when,
	    rd(REG_HW_VERSION), rd(REG_CTRL), rd(REG_STATUS0), rd(REG_FIFO_STATUS), rd(REG_CMD_DMA_CTRL),
	    rd(REG_CMD_CFG0), rd(REG_CMD_CFG1), rd(REG_DMA_BASE), rd(REG_DMA_LEN), rd(REG_ACK_ERR_STATUS),
	    rd(REG_TRIG_CTRL), rd(REG_DLN0_PHY_ERR), rd(REG_TIMEOUT_STATUS), rd(REG_EOT_PACKET_CTRL),
	    rd(REG_ERR_INT_MASK0), rd(REG_INTR_CTRL), rd(REG_CLK_CTRL), rd(REG_VERSION));
}

/* One packet as Linux's dsi_cmd_dma_add() lays it out for the 6G controller. */
static int
dsi_send(uint8_t type, const uint8_t *payload, uint32_t n, bool lp, const char *what)
{
	uint8_t pkt[32];
	uint32_t len;
	bool is_long = type == 0x39 || type == 0x29;

	if (!dsi_ok || dsi_failed || n > sizeof(pkt) - 8) {
		return -1;
	}
	if (is_long) {
		pkt[0] = (uint8_t)n;
		pkt[1] = (uint8_t)(n >> 8);
		pkt[2] = type;
		pkt[3] = 0x80 | 0x40;
		memcpy(pkt + 4, payload, n);
		len = 4 + n;
		while (len & 3) {
			pkt[len++] = 0xff;
		}
	} else {
		pkt[0] = n > 0 ? payload[0] : 0;
		pkt[1] = n > 1 ? payload[1] : 0;
		pkt[2] = type;
		pkt[3] = 0x80;
		len = 4;
	}
	for (uint32_t i = 0; i < len; i++) {
		cmdbuf[i] = pkt[i];
	}
	__builtin_arm_dsb(0xf);

	uint32_t waited = 0;
	while ((rd(REG_STATUS0) & (STATUS0_MDP_BUSY | STATUS0_DMA_BUSY)) && waited < 40) {
		dsi_sleep_us(250);
		waited++;
	}
	if (rd(REG_STATUS0) & (STATUS0_MDP_BUSY | STATUS0_DMA_BUSY)) {
        printf("LISA DSI: controller busy; no command registers changed\n");
        dsi_failed = 1;
        return -1;
    }
    uint32_t dmactl = rd(REG_CMD_DMA_CTRL), trigger = rd(REG_TRIG_CTRL);
    uint32_t intr_mask = rd(REG_INTR_CTRL) & 0xaaaaaaaaU;
    uint32_t old_base = rd(REG_DMA_BASE), old_len = rd(REG_DMA_LEN);
    /* Preserve MDP/TE stream configuration; only the command DMA source is software (4).
     * The v26 code wrote TRIG_DMA at +0x8c (BTA trigger on 6G), never triggering DMA. */
    wr(REG_TRIG_CTRL, (trigger & ~7U) | 4U);
	wr(REG_CMD_DMA_CTRL, ((dmactl | DMA_FROM_FRAME_BUFFER) & ~DMA_LOW_POWER) | (lp ? DMA_LOW_POWER : 0));
	wr(REG_INTR_CTRL, intr_mask | (INTR_CMD_DMA_DONE << 1) | INTR_CMD_DMA_DONE);
	uint32_t ack_before = rd(REG_ACK_ERR_STATUS);
	wr(REG_DMA_BASE, (uint32_t)CMDBUF_PHYS);
	wr(REG_DMA_LEN, len);
	wr(REG_TRIG_DMA, 1);

	uint32_t spins = 0, intr = 0, status = 0;
	for (; spins < 200; spins++) {
		intr = rd(REG_INTR_CTRL);
		status = rd(REG_STATUS0);
		if ((intr & INTR_CMD_DMA_DONE) && !(status & STATUS0_DMA_BUSY)) {
			break;
		}
		dsi_sleep_us(100);
	}
	uint32_t ack = rd(REG_ACK_ERR_STATUS), timeout = rd(REG_TIMEOUT_STATUS);
	wr(REG_INTR_CTRL, intr_mask | INTR_CMD_DMA_DONE);
    wr(REG_CMD_DMA_CTRL, dmactl);
    wr(REG_TRIG_CTRL, trigger);
    /* Do not rewrite an active DMA descriptor on timeout. */
    if (!(status & STATUS0_DMA_BUSY)) {
        wr(REG_DMA_BASE, old_base);
        wr(REG_DMA_LEN, old_len);
    }
	bool good = spins < 200;
	static unsigned told;
	if (!good || told++ < 12) {
		printf("LISA DSI: %s: %u bytes [%02x %02x %02x %02x %02x %02x %02x %02x] waited %u, done after %u: "
		    "intr %08x status %08x ack %08x->%08x timeout %08x%s\n", what, len, pkt[0], pkt[1], pkt[2], pkt[3],
		    len > 4 ? pkt[4] : 0, len > 5 ? pkt[5] : 0, len > 6 ? pkt[6] : 0, len > 7 ? pkt[7] : 0,
		    waited, spins, intr, status, ack_before, ack, timeout, good ? "" : " -- NOT DONE, no more sends");
	}
	if (!good) {
		dsi_failed = 1;
		lisa_hw_brightness = 0;
		dsi_dump("after the failed send");
		return -1;
	}
	return 0;
}

static int
dsi_brightness(uint32_t dbv)
{
	uint8_t p[3] = { 0x51, (uint8_t)(dbv >> 8), (uint8_t)dbv };
	return dsi_send(0x39, p, 3, true, "brightness");
}

/* iOS's level (0..1000, perceptual) to the panel's DBV, about gamma 2.2, 8 .. 2047. */
static uint32_t
level_to_dbv(int level)
{
	uint64_t x = level < 20 ? 20 : level > 1000 ? 1000 : (uint64_t)level;
	uint64_t sq = x * x / 1000;
	uint64_t fifth = (4 * x + 1000) / 5;
	uint64_t v = sq * fifth / 1000;
	uint32_t dbv = (uint32_t)(v * 2047 / 1000);
	return dbv < 8 ? 8 : dbv > 2047 ? 2047 : dbv;
}

static void
dsi_worker(__unused void *p, __unused wait_result_t w)
{
	int last = -1, hz = 60, force = 0;

	(void)PE_parse_boot_argn("lisa_hz", &hz, sizeof(hz));
	(void)PE_parse_boot_argn("lisadsi_force", &force, sizeof(force));
	dsi_dump("at start");
	uint32_t hw = rd(REG_HW_VERSION), ctrl = rd(REG_CTRL);
	bool looks = (hw >> 16) == 0x2005 && (ctrl & CTRL_ENABLE) && (ctrl & CTRL_CMD_MODE_EN);
	printf("LISA DSI: controller %s (hw %08x, ctrl %08x%s)\n", looks ? "is a v2.5 in command mode as expected" :
	    "does NOT look as expected", hw, ctrl, force ? ", forced" : "");
	dsi_ok = looks || force;
	if (!dsi_ok) {
		printf("LISA DSI: no commands are sent; brightness stays in the GPU shader\n");
		goto out;
	}
	dsi_sleep_us(1000000);
	for (;;) {
		int level = lisa_brightness_level();
        int awake = lisa_display_is_awake();
        uint32_t dbv = awake ? level_to_dbv(level) : 0;
        if ((int)dbv != last) {
			if (dsi_brightness(dbv) != 0) {
				break;
			}
			if (!lisa_hw_brightness) {
				printf("LISA DSI: panel brightness command accepted (level %d -> DBV %u): GPU dimming off\n", level, dbv);
			}
			lisa_hw_brightness = 1;
			last = (int)dbv;
		}
        /* Lisa's CSOT timing@0/1 have the SAME 682000000 link rate,
         * PHY timings and DSC dimensions. They use command mode and external TE;
         * only panel scan/VFP changes. Never apply this to a video-mode host. */
        if (hz == 90 && lisa_panel_hz != 90 && lisa_hw_brightness &&
            (rd(REG_CTRL) & CTRL_CMD_MODE_EN) && !(rd(REG_CTRL) & 2U)) {
            uint8_t rate[2] = { 0x6c, 0x01 };
            if (dsi_send(0x15, rate, 2, true, "CSOT 90 Hz") != 0) break;
            __atomic_store_n(&lisa_panel_hz, 90, __ATOMIC_RELEASE);
            printf("LISA DSI: CSOT 90 Hz requested; unchanged link/PHY, iOS vsync 90 Hz; physical TE not yet measured\n");
        }
		dsi_sleep_us(40000);
	}
out:
	printf("LISA DSI: worker stopped\n");
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

void
lisa_dsi_init(void)
{
	int enabled = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("lisadsi", &enabled, sizeof(enabled)) || enabled == 0) {
		return;
	}
	dsi = (volatile uint8_t *)ml_io_map(DSI0_PHYS, DSI_SIZE);
	cmdbuf = (volatile uint8_t *)ml_io_map(CMDBUF_PHYS, 0x1000);
	if (dsi == NULL || cmdbuf == NULL) {
		printf("LISA DSI: could not map the controller or the command buffer\n");
		return;
	}
	if (kernel_thread_start(dsi_worker, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
#endif
