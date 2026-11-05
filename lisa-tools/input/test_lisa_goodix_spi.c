#include "lisa_goodix_spi.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
struct mock { int calls, fail; uint8_t tx[1033]; size_t n; };
static int exchange(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n) {
 struct mock *m = ctx; m->calls++; m->n = n; memcpy(m->tx, tx, n);
 if (m->fail) return -1;
 for (size_t i=0; i<n; i++) rx[i]=(uint8_t)i;
 return 0;
}
int main(void) {
 struct mock m={0}; struct lisa_goodix_bus bus={&m, exchange, 0x12345678};
 uint8_t out[2]={0};
 assert(!lisa_goodix_spi_read(&bus, 0x12345678, out, 2));
 assert(m.n==11 && m.tx[0]==0xf1 && m.tx[1]==0x12 && m.tx[4]==0x78 && m.tx[5]==0xff && m.tx[8]==0xff && out[0]==9 && out[1]==10);
 assert(lisa_goodix_spi_read(&bus, UINT32_MAX, out, 2)==-1 && m.calls==1);
 m.fail=1; out[0]=42; assert(lisa_goodix_spi_read(&bus, 1, out, 1)==-1 && out[0]==42);
 m.fail=0; uint8_t zero=0, bad=1;
 assert(lisa_goodix_spi_ack(&bus, 0x12345679, &zero, 1)==-1);
 assert(lisa_goodix_spi_ack(&bus, 0x12345678, &bad, 1)==-1);
 assert(!lisa_goodix_spi_ack(&bus, 0x12345678, &zero, 1));
 assert(m.n==6 && m.tx[0]==0xf0 && m.tx[1]==0x12 && m.tx[4]==0x78 && m.tx[5]==0);
 return 0;
}
