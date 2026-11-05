#ifndef LISA_GT9897_FRAME_H
#define LISA_GT9897_FRAME_H
#include <stddef.h>
#include <stdint.h>

#define GT9897_MAX_CONTACTS 10
struct gt9897_contact { uint8_t id; uint16_t x, y, width; };
struct gt9897_frame { unsigned count; struct gt9897_contact contacts[GT9897_MAX_CONTACTS]; };
/* Decode already-collected Berlin-A header and contiguous contact payload.
 * More than two contacts require caller-side normalization of SPI reads.
 * Returns 1 for a touch frame, 0 for another event, -1 for invalid input.
 * This parser does not access hardware or acknowledge events. */
int gt9897_decode(const uint8_t *header, size_t header_len,
    const uint8_t *contacts, size_t contacts_len, struct gt9897_frame *out);
#endif
