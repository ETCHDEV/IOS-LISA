#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lisa_goodix_berlin.h"

static void checksum(uint8_t *data, size_t len)
{
    unsigned sum = 0;
    for (size_t i = 0; i + 2 < len; i++) sum += data[i];
    data[len - 2] = sum & 255;
    data[len - 1] = (sum >> 8) & 255;
}

int main(void)
{
    uint8_t command[16];
    const uint8_t expected[] = {0xf1, 0x12, 0x34, 0x56, 0x78, 0xff, 0xff, 0xff, 0xff, 0, 0};
    assert(lisa_touch_spi_read_frame(command, sizeof(command), 0x12345678, 2) == 0);
    assert(memcmp(command, expected, sizeof(expected)) == 0);
    assert(lisa_touch_spi_read_frame(command, 10, 0, 2) == -1);
    assert(lisa_touch_spi_read_frame(command, sizeof(command), 0, SIZE_MAX) == -1);
    uint8_t info[96] = {0};
    info[0] = sizeof(info);
    info[81] = 0x78; info[82] = 0x56; info[83] = 0x34; info[84] = 0x12;
    checksum(info, sizeof(info));
    uint32_t address = 0;
    assert(lisa_touch_info_address(info, sizeof(info), &address) == 0 && address == 0x12345678);
    assert(lisa_touch_info_address(info, 95, &address) == -1);
    info[32] = 255;
    checksum(info, sizeof(info));
    assert(lisa_touch_info_address(info, sizeof(info), &address) == -1);
    uint8_t event[26] = {0x80, 0, 2, 0, 0, 0, 0, 0,
                         0x20, 0, 0x34, 0x01, 0x78, 0x02, 7, 0,
                         0x50, 0, 0x55, 0x00, 0xaa, 0x00, 8, 0, 0, 0};
    struct lisa_touch_frame frame;
    checksum(event, 8);
    checksum(event + 8, 18);
    assert(lisa_touch_decode(event, sizeof(event), &frame) == 0);
    assert(frame.count == 2 && frame.points[0].id == 2 && frame.points[0].x == 308 &&
           frame.points[0].y == 632 && frame.points[1].id == 5);
    assert(lisa_touch_decode(event, 25, &frame) == -1);
    event[8] ^= 1;
    assert(lisa_touch_decode(event, sizeof(event), &frame) == -1);
    event[8] ^= 1;
    event[16] = event[8];
    checksum(event + 8, 18);
    assert(lisa_touch_decode(event, sizeof(event), &frame) == -1);
    event[2] = 11;
    checksum(event, 8);
    assert(lisa_touch_decode(event, sizeof(event), &frame) == -1);
    puts("PASS: Berlin SPI frame, IC info, touch frame, and malformed-input checks");
}
