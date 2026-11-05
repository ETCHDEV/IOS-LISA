#include "lisa_geni_spi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct model { uint32_t regs[0x1000/4], words[300]; unsigned head, tail, writes, cancelled; size_t length; int timeout; };
static uint32_t readreg(void *p,uint32_t off) {
    struct model *m=p;
    if (off==0x800) return 0;
    if (off==0x804) {
        unsigned words=m->tail-m->head;
        return words | (words && (m->length%4) ? 0x80000000u | ((uint32_t)(m->length%4)<<28) : 0);
    }
    if (off==0x780) return m->words[m->head++];
    if (off==0x610) return m->timeout ? 0 : m->head==m->tail && m->tail ? 1 : 0;
    return m->regs[off/4];
}
static void writereg(void *p,uint32_t off,uint32_t value) {
    struct model *m=p; ++m->writes;
    if (off==0x700) m->words[m->tail++]=value;
    else if (off==0x26c) m->length=value;
    else if (off==0x604) m->cancelled++;
    else m->regs[off/4]=value;
}
static void delay(void *p,unsigned us) { (void)p; (void)us; }
int main(void) {
    struct model m={0}; struct lisa_geni_spi s={&m,readreg,writereg,delay,0};
    m.regs[0x68/4]=1u<<8; m.regs[0xe24/4]=16u<<16;
    assert(lisa_geni_spi_init(&s,19200000,0,1000000)==0 && s.fifo_depth==16);
    uint8_t tx[]={1,2,3,4,5,6,7},rx[7]={0};
    assert(lisa_geni_spi_exchange(&s,tx,rx,sizeof(tx))==0 && !memcmp(tx,rx,sizeof(tx)));
    unsigned before=m.writes;
    assert(lisa_geni_spi_exchange(&s,tx,rx,0)==-1 && m.writes==before);
    assert(lisa_geni_spi_init(&s,19200000,0,6000001)==-1 && m.writes==before);
    m.regs[0x40/4]=1;
    assert(lisa_geni_spi_exchange(&s,tx,rx,sizeof(tx))==-1 && m.writes==before);
    m.regs[0x40/4]=0; m.head=m.tail=0; m.timeout=1;
    assert(lisa_geni_spi_exchange(&s,tx,rx,sizeof(tx))==-1 && m.cancelled==1);
    puts("PASS: GENI FIFO transfers, guards and timeout");
}
