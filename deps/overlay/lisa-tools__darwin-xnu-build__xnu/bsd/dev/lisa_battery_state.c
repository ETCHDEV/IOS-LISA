/* Read-only ADSP battmgr -> IOPMPowerSource bridge. No charger configuration writes. */
#include <kern/clock.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <sys/redwood_input.h>
#include <sys/lisa_battery_state.h>
#include <sys/systm.h>
static LCK_GRP_DECLARE(battery_group, "lisa-battery-cache");
static LCK_MTX_DECLARE(battery_lock, &battery_group);
static struct lisa_battery_sample sample;
static uint64_t sampled_at;
_Static_assert(sizeof(struct lisa_battery_sample) == sizeof(struct redwood_battery_state), "battery sample ABI");

void
redwood_battery_report(const struct redwood_battery_state *in)
{
    if (!in || in->size != sizeof(*in) || in->present != 1 ||
        in->percent_hundredths < 0 || in->percent_hundredths > 10000 ||
        in->voltage_uv < 2800000 || in->voltage_uv > 4700000 ||
        in->temperature_hundredths < -4000 || in->temperature_hundredths > 9000 ||
        in->status < 1 || in->status > 4 || in->current_ua < -15000000 || in->current_ua > 15000000)
        return;
    lck_mtx_lock(&battery_lock);
    memcpy(&sample, in, sizeof(sample));
    sampled_at = mach_absolute_time();
    lck_mtx_unlock(&battery_lock);
}

int
lisa_battery_snapshot(struct lisa_battery_sample *out)
{
    uint64_t nanos = 0;
    lck_mtx_lock(&battery_lock);
    uint64_t stamp = sampled_at;
    if (stamp) memcpy(out, &sample, sizeof(*out));
    lck_mtx_unlock(&battery_lock);
    if (!stamp) return 0;
    absolutetime_to_nanoseconds(mach_absolute_time() - stamp, &nanos);
    return nanos < 45000000000ULL; /* never present a stale sample as live charging */
}

/* The ADSP battery/GLINK transport is usable without Lisa's audio amp driver.
 * No PCM streams or speaker supplies have been configured by this port. */
void redwood_sound_dsp_up(void);
void redwood_sound_dsp_down(void);
void redwood_sound_quiesce(void);
void redwood_mic_dsp_up(void);
void redwood_mic_dsp_down(void);
void redwood_sound_dsp_up(void) { printf("LISA ADSP: battery transport only; audio output not connected\n"); }
void redwood_sound_dsp_down(void) {}
void redwood_sound_quiesce(void) {}
void redwood_mic_dsp_up(void) {}
void redwood_mic_dsp_down(void) {}
