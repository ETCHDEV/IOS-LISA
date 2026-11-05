#ifndef LISA_BATTERY_STATE_H
#define LISA_BATTERY_STATE_H
#include <stdint.h>
/* ABI shared with upstream redwood_battery_state; size is validated on receipt. */
struct lisa_battery_sample {
    uint32_t size;
    int32_t percent_hundredths, voltage_uv, current_ua, temperature_hundredths;
    int32_t present, external, status, charge_uah, full_uah, design_uah, cycles;
    int32_t usb_type, usb_voltage_uv, usb_current_ua;
};
#ifdef __cplusplus
extern "C" {
#endif
int lisa_battery_snapshot(struct lisa_battery_sample *out);
#ifdef __cplusplus
}
#endif
#endif
