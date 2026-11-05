/* Lisa PMK8350 power and volume-down polling. No PMIC configuration writes.
 * PON HLOS interrupt real-time status: SID 0, peripheral 0x13, offset 0x10.
 * This opt-in prototype uses only observer EXT_READL commands and stops
 * permanently on timeout.
 * Volume-up (2026-10-26): PM7325 GPIO6, SID 1 peripheral 0x8d (GPIO base 0x8800 + 5 * 0x100),
 * real-time status 0x10 bit 0 = input level (Linux pinctrl-spmi-gpio), active low. Read through
 * the same observer channel; its own failure stops only volume-up.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
extern kern_return_t thread_terminate(thread_t);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern void sweet_hid_key(int, int);
void sweet_keys_init(void);
#define CORE_PA 0x0c440000u
#define OBS_PA 0x0e600000u
#define CFG_PA 0x0c40a000u
static vm_offset_t core, obs, cfg;
static uint16_t pon_apid = 0xffffu, vup_apid = 0xffffu;
static bool vup_poisoned;
static bool started, poisoned;
static int sleep_chan;
static uint32_t rd(vm_offset_t base, unsigned off) {
 uint32_t v = *(volatile uint32_t *)(base + off);
 __asm__ volatile("dmb oshld" ::: "memory"); return v;
}
static bool obs_read(uint16_t apid, unsigned reg, uint8_t *out, bool *poison) {
 if (*poison || apid == 0xffffu) return false;
 unsigned off = 0x80u * apid;
 __asm__ volatile("dmb oshst" ::: "memory");
 *(volatile uint32_t *)(obs + off) = (1u << 27) | (reg << 4);
 __asm__ volatile("dsb osh" ::: "memory");
 for (unsigned i = 0; i < 1000; i++) {
  uint32_t status = rd(obs, off + 8);
  if (status & 1u) {
   if (status & 0x0eu) { *poison = true; return false; }
   *out = (uint8_t)rd(obs, off + 0x18); return true;
  }
  delay(1);
 }
 *poison = true; return false;
}
static bool pon_read(uint8_t *out) {
 if (poisoned || pon_apid == 0xffffu) return false;
 unsigned off = 0x80u * pon_apid;
 __asm__ volatile("dmb oshst" ::: "memory");
 *(volatile uint32_t *)(obs + off) = (1u << 27) | (0x10u << 4);
 __asm__ volatile("dsb osh" ::: "memory");
 for (unsigned i = 0; i < 1000; i++) {
  uint32_t status = rd(obs, off + 8);
  if (status & 1u) {
   if (status & 0x0eu) { poisoned = true; return false; }
   *out = (uint8_t)rd(obs, off + 0x18); return true;
  }
  delay(1);
 }
 poisoned = true; printf("LISA KEYS: observer timeout; polling stopped\n"); return false;
}
static void keys_thread(void *arg, wait_result_t result) {
 (void)arg; (void)result;
 int power = 0, down = 0, up = 0;
 while (!poisoned) {
  uint8_t status;
  if (pon_read(&status)) {
   int p = !!(status & 0x80u), d = !!(status & 0x40u);
   if (p != power) { power = p; sweet_hid_key(0, p); printf("LISA KEYS: power %d\n", p); }
   if (d != down) { down = d; sweet_hid_key(2, d); printf("LISA KEYS: volume-down %d\n", d); }
  }
  uint8_t level;
  if (obs_read(vup_apid, 0x10u, &level, &vup_poisoned)) {
   int u = !(level & 1u);
   if (u != up) { up = u; sweet_hid_key(1, u); printf("LISA KEYS: volume-up %d\n", u); }
  } else if (vup_poisoned && vup_apid != 0xffffu) {
   printf("LISA KEYS: volume-up read failed; volume-up polling stopped\n"); vup_apid = 0xffffu;
  }
  uint64_t deadline; clock_interval_to_deadline(20, NSEC_PER_MSEC, &deadline);
  (void)assert_wait_deadline(&sleep_chan, THREAD_UNINT, deadline);
  thread_block(THREAD_CONTINUE_NULL);
 }
 (void)thread_terminate(current_thread());
}
void sweet_keys_init(void) {
 int enabled = 0; thread_t thread;
 if (started) return;
 started = true;
 PE_parse_boot_argn("lisakeys", &enabled, sizeof(enabled));
 if (enabled != 1) return;
 core = ml_io_map(CORE_PA, 0x2000); cfg = ml_io_map(CFG_PA, 0x1000);
 obs = ml_io_map(OBS_PA, 0x10000);
 if (!core || !cfg || !obs) { printf("LISA KEYS: map failed\n"); return; }
 uint32_t version = rd(core, 0), count = rd(core, 4) & 0x7ffu;
 if (version < 0x50000000u || version >= 0x70000000u || !count || count > 512) {
  printf("LISA KEYS: unsupported arbiter %#x count %u\n", version, count); return;
 }
 for (unsigned i = 0; i < count; i++) {
  uint32_t map = rd(core, 0x900u + 4u * i);
  if (map && ((map >> 8) & 0xfffu) == 0x013u) {
   unsigned owner = rd(cfg, 0x700u + 4u * i) & 7u;
   if (pon_apid == 0xffffu || owner == 0) pon_apid = i;
  }
  if (map && ((map >> 8) & 0xfffu) == 0x18du) {
   unsigned owner = rd(cfg, 0x700u + 4u * i) & 7u;
   if (vup_apid == 0xffffu || owner == 0) vup_apid = i;
  }
 }
 if (pon_apid == 0xffffu) { printf("LISA KEYS: PON APID absent\n"); return; }
 uint8_t initial;
 if (!pon_read(&initial)) { printf("LISA KEYS: PON read failed\n"); return; }
 printf("LISA KEYS: PON APID %u status %#x; observer reads only\n", pon_apid, initial);
 uint8_t vlevel = 0;
 if (vup_apid == 0xffffu) printf("LISA KEYS: volume-up (PM7325 GPIO6, ppid 0x18d) APID absent\n");
 else if (obs_read(vup_apid, 0x10u, &vlevel, &vup_poisoned)) printf("LISA KEYS: volume-up APID %u level %#x\n", vup_apid, vlevel);
 else { printf("LISA KEYS: volume-up APID %u read failed\n", vup_apid); vup_apid = 0xffffu; }
 kern_return_t kr = kernel_thread_start(keys_thread, NULL, &thread);
 if (kr == KERN_SUCCESS) thread_deallocate(thread);
 else printf("LISA KEYS: thread error %d\n", kr);
}
#endif
