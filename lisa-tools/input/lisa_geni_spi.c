/* SPDX-License-Identifier: APSL-2.0
 * Adapted from oosexgang/hackios-xnu-kernel redwood_goodix.c (APSL-2.0).
 * FIFO mode only: clocks, power, pinmux and chip reset are caller's job.
 */
#include "lisa_geni_spi.h"
#define STATUS 0x040u
#define CLK_CFG 0x048u
#define IF_DISABLED 0x064u
#define FW_REV 0x068u
#define CLK_SEL 0x07cu
#define CMD 0x600u
#define CMD_CTRL 0x604u
#define IRQ_STATUS 0x610u
#define IRQ_EN 0x614u
#define IRQ_CLEAR 0x618u
#define RX_FIFO 0x780u
#define TX_FIFO 0x700u
#define TX_STATUS 0x800u
#define RX_STATUS 0x804u
#define HW_PARAM 0xe24u
#define DONE 1u
#define ERRORS ((1u<<1)|(1u<<2)|(1u<<3)|(1u<<24)|(1u<<25)|(1u<<28)|(1u<<29))
#define MAX_TRANSFER 1033u
static uint32_t rd(struct lisa_geni_spi *s, uint32_t off) { return s->read(s->context, off); }
static void wr(struct lisa_geni_spi *s, uint32_t off, uint32_t v) { s->write(s->context, off, v); }
int lisa_geni_spi_init(struct lisa_geni_spi *s, uint32_t source_hz, uint32_t select, uint32_t spi_hz) {
    if (!s || !s->read || !s->write || !s->delay_us || !source_hz || !spi_hz || spi_hz>6000000 || select>7) return -1;
    if (((rd(s,FW_REV)>>8)&255)!=1 || (rd(s,IF_DISABLED)&1) || (rd(s,STATUS)&1)) return -1;
    uint32_t depth=(rd(s,HW_PARAM)>>16)&63;
    uint32_t div=source_hz/spi_hz+(source_hz%spi_hz!=0);
    if (depth<4 || div==0 || div>4095) return -1;
    s->fifo_depth=depth;
    wr(s,0xe18,0); wr(s,IRQ_CLEAR,0xffffffffu); wr(s,0x648,0xffffffffu);
    wr(s,0xc44,0xffffffffu); wr(s,0xd44,0xffffffffu);
    wr(s,0x028,rd(s,0x028)|0x7f); wr(s,0xe30,rd(s,0xe30)|0xf);
    wr(s,0x024,0x7f); wr(s,0x020,1); wr(s,0x258,rd(s,0x258)&~1u);
    wr(s,0x810,depth-3); wr(s,0x814,depth-2); wr(s,0x80c,0);
    wr(s,0xe1c,rd(s,0xe1c)|0xf); wr(s,IRQ_EN,rd(s,IRQ_EN)|0x7fc0007f);
    wr(s,0x22c,0); wr(s,0x250,0); wr(s,0x224,0); wr(s,0x230,0);
    wr(s,0x24c,0); wr(s,0x25c,rd(s,0x25c)&~2u); wr(s,0x278,0);
    wr(s,0x260,0x0007f8fe); wr(s,0x264,0x000ffefe);
    wr(s,0x284,0x0007f8fe); wr(s,0x288,0x000ffefe);
    wr(s,0x254,0); wr(s,0x268,4); wr(s,CLK_SEL,select); wr(s,CLK_CFG,(div<<4)|1);
    return 0;
}
int lisa_geni_spi_exchange(void *context, const uint8_t *tx, uint8_t *rx, size_t length) {
    struct lisa_geni_spi *s=context;
    if (!s || !s->read || !s->write || !s->delay_us || s->fifo_depth<4 || !tx || !rx || !length || length>MAX_TRANSFER || (rd(s,STATUS)&1)) return -1;
    size_t sent=0, received=0; uint32_t irq=0;
    wr(s,IRQ_CLEAR,0xffffffffu); wr(s,0x26c,(uint32_t)length); wr(s,0x270,(uint32_t)length); wr(s,CMD,3u<<27);
    for (unsigned iteration=0; iteration<20000; ++iteration) {
        while (sent<length && (rd(s,TX_STATUS)&0x0fffffffu)+1<s->fifo_depth) {
            uint32_t word=0; for (unsigned j=0;j<4 && sent<length;++j,++sent) word|=(uint32_t)tx[sent]<<(8*j);
            wr(s,TX_FIFO,word);
        }
        uint32_t status=rd(s,RX_STATUS), words=status&0x01ffffffu;
        if (words>s->fifo_depth) break;
        uint32_t bytes=words*4, valid=(status>>28)&7;
        if ((status&0x80000000u) && valid && valid<4) bytes-=4-valid;
        for (uint32_t i=0;i<words;++i) {
            uint32_t word=rd(s,RX_FIFO);
            for (unsigned j=0;j<4 && bytes;++j,--bytes) { if (received<length) rx[received++]=(uint8_t)(word>>(8*j)); }
        }
        irq=rd(s,IRQ_STATUS);
        if (irq&ERRORS) break;
        if ((irq&DONE) && received==length && sent==length) { wr(s,IRQ_CLEAR,irq); return 0; }
        s->delay_us(s->context,10);
    }
    /* Cancel once; never keep retrying an uncertain controller. */
    wr(s,CMD_CTRL,1u<<2);
    for (unsigned i=0;i<2000 && !(rd(s,IRQ_STATUS)&(1u<<4));++i) s->delay_us(s->context,100);
    return -1;
}
