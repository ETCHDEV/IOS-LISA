// SPDX-License-Identifier: BSD-2-Clause-Patent
// Lisa: nothing of the SM8350 CPU frequency, second panel, touch and GPU power probes applies to SM7325.
// The display driver only calls these; Lisa's own drivers take their place as they are ported.
#include <stdint.h>
#include <libkern/libkern.h>
extern "C" {
void lisa_cpufreq_init(void);
extern void (*proc_trace_smp_hook)(void);
extern volatile uint32_t armcpu_smp_requested __attribute__((weak_import)), armcpu_smp_running __attribute__((weak_import));
extern volatile uint32_t armcpu_smp_step[8] __attribute__((weak_import)), armcpu_smp_fail[8] __attribute__((weak_import));
extern volatile int32_t armcpu_smp_psci_rc[8] __attribute__((weak_import));
static void lisa_smp_report(void) {
    if (&armcpu_smp_running == nullptr) {printf("proc-trace: armcpu not linked\n"); return;}
    printf("proc-trace: smp requested %u running %u\n", armcpu_smp_requested, armcpu_smp_running);
    for (int c = 0; c < 8; c++) printf("proc-trace: cpu %d step %u fail %u psci %d\n", c, armcpu_smp_step[c], armcpu_smp_fail[c], armcpu_smp_psci_rc[c]);
}
void sheng_cpufreq_probe(void) {proc_trace_smp_hook = lisa_smp_report; lisa_cpufreq_init();}
void sheng_cpufreq_poll(void) {}
void zeta_cpufreq_thermal(void) {}
void zeta_soc_health(void) {}
void zeta_dsi_survey(volatile unsigned char *) {}
void zeta_touch_probe(void) {}
void zeta_shell_start(void) {}
// The display draws on the screen itself once the system has it; no boot progress page on Lisa.
int boot_progress_prepare_scanout(void) { return 1; }
void boot_progress_cancel_scanout(void) {}
char boot_progress_ext[8][48];
// No USB console.
volatile unsigned long long unixnu_dwc3_dropped;
volatile unsigned int unixnu_dwc3_home_requests;
// native_touch_* now live in bsd/dev/lisa_touch.c (patches/xnu-12377/zzzzzz-lisa-touch.patch).
}
