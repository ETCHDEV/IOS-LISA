/* SPDX-License-Identifier: APSL-2.0
 * Lisa GENI SPI FIFO transport; no direct MMIO, clocks, pinmux or power.
 * Caller must serialize transactions, establish ownership and provide barriers.
 */
#ifndef LISA_GENI_SPI_H
#define LISA_GENI_SPI_H
#include <stddef.h>
#include <stdint.h>
struct lisa_geni_spi {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    void (*delay_us)(void *, unsigned);
    uint32_t fifo_depth;
};
int lisa_geni_spi_init(struct lisa_geni_spi *, uint32_t, uint32_t, uint32_t);
int lisa_geni_spi_exchange(void *, const uint8_t *, uint8_t *, size_t);
#endif
