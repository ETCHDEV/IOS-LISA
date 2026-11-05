/* SPDX-License-Identifier: GPL-2.0-or-later
 * Protocol adapter only. Does not initialize or access GENI hardware itself.
 * Berlin-A protocol: Linux drivers/input/touchscreen/goodix_berlin_spi.c.
 */
#include "lisa_goodix_spi.h"
#define MAX_READ 1024u
#define READ_PREFIX 9u
#define WRITE_PREFIX 5u
static void address_bytes(uint8_t *p, uint32_t address) {
 p[0] = (uint8_t)(address >> 24); p[1] = (uint8_t)(address >> 16);
 p[2] = (uint8_t)(address >> 8); p[3] = (uint8_t)address;
}
int lisa_goodix_spi_read(void *ctx, uint32_t address, uint8_t *out, size_t length) {
 struct lisa_goodix_bus *bus = ctx;
 uint8_t tx[READ_PREFIX + MAX_READ] = {0}, rx[READ_PREFIX + MAX_READ];
 if (!bus || !bus->exchange || !out || !length || length > MAX_READ ||
     length - 1 > UINT32_MAX - address) return -1;
 tx[0] = 0xf1; address_bytes(tx + 1, address);
 for (unsigned i = 5; i < READ_PREFIX; i++) tx[i] = 0xff;
 if (bus->exchange(bus->context, tx, rx, READ_PREFIX + length)) return -1;
 for (size_t i = 0; i < length; i++) out[i] = rx[READ_PREFIX + i];
 return 0;
}
/* Only clear the known event register; no general-purpose device writes. */
int lisa_goodix_spi_ack(void *ctx, uint32_t address, const uint8_t *data, size_t length) {
 struct lisa_goodix_bus *bus = ctx;
 uint8_t tx[WRITE_PREFIX + 1] = {0}, rx[WRITE_PREFIX + 1];
 if (!bus || !bus->exchange || !bus->event_address || address != bus->event_address ||
     !data || length != 1 || data[0] != 0) return -1;
 tx[0] = 0xf0; address_bytes(tx + 1, address);
 return bus->exchange(bus->context, tx, rx, sizeof(tx)) ? -1 : 0;
}
