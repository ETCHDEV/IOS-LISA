/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LISA_GOODIX_SPI_H
#define LISA_GOODIX_SPI_H
#include <stddef.h>
#include <stdint.h>
/* exchange must hold chip select throughout, and return zero only on complete success. */
typedef int (*lisa_spi_exchange)(void *, const uint8_t *, uint8_t *, size_t);
struct lisa_goodix_bus { void *context; lisa_spi_exchange exchange; uint32_t event_address; };
int lisa_goodix_spi_read(void *, uint32_t, uint8_t *, size_t);
int lisa_goodix_spi_ack(void *, uint32_t, const uint8_t *, size_t);
#endif
