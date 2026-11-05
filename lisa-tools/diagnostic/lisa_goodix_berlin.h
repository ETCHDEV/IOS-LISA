/* Goodix Berlin-A event decoder for Lisa bring-up. No hardware access.
 * Format reference: Linux goodix_berlin_core.c (GPL-2.0-or-later).
 * This decoder is experimental until verified against Lisa's controller.
 */
#ifndef LISA_GOODIX_BERLIN_H
#define LISA_GOODIX_BERLIN_H
#include <stddef.h>
#include <stdint.h>

#define LISA_TOUCH_MAX 10
struct lisa_touch_point { uint8_t id; uint16_t x, y, width; };
struct lisa_touch_frame { uint8_t count; struct lisa_touch_point points[LISA_TOUCH_MAX]; };

static uint16_t lisa_touch_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

/* Berlin-A uses a full-duplex read, with four dummy bytes after the address.
 * The caller clocks exactly prefix + payload bytes in one chip-select window. */
static int lisa_touch_spi_read_frame(uint8_t *tx, size_t capacity,
    uint32_t address, size_t payload_len)
{
    const size_t prefix = 9;
    if (!tx || payload_len > capacity || capacity - payload_len < prefix) return -1;
    tx[0] = 0xf1;
    tx[1] = (uint8_t)(address >> 24);
    tx[2] = (uint8_t)(address >> 16);
    tx[3] = (uint8_t)(address >> 8);
    tx[4] = (uint8_t)address;
    for (size_t i = 5; i < prefix; i++) tx[i] = 0xff;
    for (size_t i = prefix; i < prefix + payload_len; i++) tx[i] = 0;
    return 0;
}

static int lisa_touch_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    if (len < 2) return 0;
    for (size_t i = 0; i < len - 2; i++) sum += data[i];
    return (uint16_t)sum == lisa_touch_le16(data + len - 2);
}

/* Extract the runtime event register from a complete, checksummed IC-info
 * response. The five frequency arrays have variable counts. */
static int lisa_touch_info_address(const uint8_t *data, size_t available, uint32_t *address)
{
    const size_t params_offset = 2 + 16 + 10 + 4;
    const size_t touch_offset = 44;
    if (!data || !address || available < 2) return -1;
    size_t length = lisa_touch_le16(data);
    if (length > available || length >= 1024 || length < params_offset + 5 + touch_offset + 4 + 2 ||
        !lisa_touch_checksum(data, length)) return -1;
    size_t offset = params_offset;
    for (unsigned i = 0; i < 5; i++) {
        if (offset >= length - 2) return -1;
        size_t bytes = (size_t)data[offset++] * 2;
        if (bytes > length - 2 - offset) return -1;
        offset += bytes;
    }
    if (offset > length - 2 || touch_offset + 4 > length - 2 - offset) return -1;
    const uint8_t *p = data + offset + touch_offset;
    uint32_t val = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    if (!val) return -1;
    *address = val;
    return 0;
}

/* Return 0 on a touch frame, 1 for another event, -1 for invalid data. */
static int lisa_touch_decode(const uint8_t *data, size_t len, struct lisa_touch_frame *out)
{
    if (!data || !out || len < 8 || !lisa_touch_checksum(data, 8)) return -1;
    if (!(data[0] & 0x80)) return 1;
    unsigned n = data[2] & 15;
    if (n > LISA_TOUCH_MAX || (n && (len < 8 + n * 8 + 2 ||
        !lisa_touch_checksum(data + 8, n * 8 + 2)))) return -1;
    struct lisa_touch_frame frame = {0};
    unsigned ids = 0;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *p = data + 8 + i * 8;
        unsigned id = p[0] >> 4, type = p[0] & 15;
        if (type == 1 || type == 3 || id >= LISA_TOUCH_MAX || (ids & (1u << id))) return -1;
        ids |= 1u << id;
        frame.points[i] = (struct lisa_touch_point){ id, lisa_touch_le16(p + 2),
            lisa_touch_le16(p + 4), lisa_touch_le16(p + 6) };
    }
    frame.count = (uint8_t)n;
    *out = frame;
    return 0;
}
#endif
