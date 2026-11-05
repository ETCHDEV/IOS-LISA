#!/usr/bin/env python3
"""Run the actual XNU frame-to-HID adapter against synthetic input."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'lisa-tools/darwin-xnu-build/xnu/bsd/dev/sweet_touch.c').read_text()
functions = src[src.index('static void\ntouch_report('):src.index('\nstatic int\ntouch_goodix(')]
prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lisa_goodix_berlin.h"
#define TOUCH_MAX 10u
typedef int (*lisa_goodix_read_fn)(void *, uint32_t, uint8_t *, size_t);
typedef int (*lisa_goodix_write_fn)(void *, uint32_t, const uint8_t *, size_t);
typedef int (*lisa_spi_exchange_fn)(void *, const uint8_t *, uint8_t *, size_t);
struct lisa_goodix_spi_bus { void *context; lisa_spi_exchange_fn exchange; uint32_t event_address; };
static struct { uint32_t active; int x[10], y[10]; } st;
static int events, last_id, last_down, last_x, last_y;
static void sweet_hid_touch(int id, int down, int x, int y) {
    events++; last_id = id; last_down = down; last_x = x; last_y = y;
}
'''
main = r'''
static void checksum(uint8_t *p, size_t n) {
    unsigned v=0; for (size_t i=0; i<n-2; i++) v+=p[i];
    p[n-2]=(uint8_t)v; p[n-1]=(uint8_t)(v>>8);
}
struct bus { uint8_t data[90]; int reads, writes, fail_write; };
static int exchange(void *context, const uint8_t *tx, uint8_t *rx, size_t length) {
    struct bus *b=context;
    uint32_t addr=((uint32_t)tx[1]<<24)|((uint32_t)tx[2]<<16)|((uint32_t)tx[3]<<8)|tx[4];
    memset(rx,0,length);
    if (addr<0x1000 || addr-0x1000>=sizeof(b->data)) return -1;
    if (tx[0]==0xf0) {
        if (length!=6 || tx[5] || b->fail_write) return -1;
        b->writes++; return 0;
    }
    if (tx[0]!=0xf1 || length<9 || tx[5]!=0xff || tx[6]!=0xff || tx[7]!=0xff || tx[8]!=0xff ||
        length-9>sizeof(b->data)-(addr-0x1000)) return -1;
    memcpy(rx+9,b->data+(addr-0x1000),length-9); b->reads++; return 0;
}
static int info_exchange(void *context, const uint8_t *tx, uint8_t *rx, size_t length) {
    uint8_t *info=context;
    if (tx[0]!=0xf1 || tx[1]!=0 || tx[2]!=1 || tx[3]!=0 || tx[4]!=0x68 ||
        length<9 || length-9>90) return -1;
    memset(rx,0,length); memcpy(rx+9,info,length-9); return 0;
}
static int rd(void *context, uint32_t addr, uint8_t *out, size_t length) {
    struct bus *b=context; if (addr<0x1000 || addr-0x1000+length>sizeof(b->data)) return -1;
    b->reads++; for (size_t i=0;i<length;i++) out[i]=b->data[addr-0x1000+i]; return 0;
}
static int wr(void *context, uint32_t addr, const uint8_t *data, size_t length) {
    struct bus *b=context; if (addr!=0x1000 || length!=1 || data[0] || b->fail_write) return -1;
    b->writes++; return 0;
}
int main(void) {
    uint8_t b[18]={0x80,0,1,0,0,0,0,0, 0x20,0,0x34,0x01,0x78,0x02,0,0,0,0};
    checksum(b,8); checksum(b+8,10);
    assert(lisa_goodix_feed(b,sizeof(b))==0 && events==1 && last_id==2 && last_down==1 && last_x==308 && last_y==632);
    assert(lisa_goodix_feed(b,sizeof(b))==0 && events==1);
    b[10]=0xff; b[11]=0xff; checksum(b+8,10);
    assert(lisa_goodix_feed(b,sizeof(b))==-1 && events==1);
    b[2]=0; checksum(b,8);
    assert(lisa_goodix_feed(b,8)==0 && events==2 && last_id==2 && last_down==0);
    struct bus bus={0}; bus.data[0]=0x80; bus.data[2]=3;
    bus.data[8]=0x10; bus.data[16]=0x20; bus.data[24]=0x30;
    checksum(bus.data,8); checksum(bus.data+8,26);
    assert(lisa_goodix_poll(&bus,0x1000,rd,wr)==0 && bus.reads==2 && bus.writes==1 && events==5);
    bus.fail_write=1;
    assert(lisa_goodix_poll(&bus,0x1000,rd,wr)==-1 && events==5);
    assert(lisa_goodix_poll(&bus,UINT32_MAX-5,rd,wr)==-1);
    bus.fail_write=0; bus.data[8]^=1;
    assert(lisa_goodix_poll(&bus,0x1000,rd,wr)==-1 && bus.writes==1 && events==5);
    bus.data[8]^=1; bus.reads=bus.writes=0;
    struct lisa_goodix_spi_bus spi={&bus,exchange,0x1000};
    assert(lisa_goodix_poll(&spi,0x1000,lisa_goodix_spi_read,lisa_goodix_spi_ack)==0);
    assert(bus.reads==2 && bus.writes==1);
    uint8_t clear=0; assert(lisa_goodix_spi_ack(&spi,0x1001,&clear,1)==-1 && bus.writes==1);
    /* Five empty variable-length lists start at offset 32; event address is at 81. */
    uint8_t full_info[90]={0}; full_info[0]=sizeof(full_info); full_info[81]=0x34;
    full_info[82]=0x12; checksum(full_info,sizeof(full_info));
    struct lisa_goodix_spi_bus probe={full_info,info_exchange,0x999};
    assert(lisa_goodix_spi_discover(&probe)==0 && probe.event_address==0x1234);
    full_info[89]^=1;
    /* A bad checksum must fail closed, clearing the old event address. */
    assert(lisa_goodix_spi_discover(&probe)==-1 && probe.event_address==0);
    puts("PASS: XNU adapter, multi-contact reads, acknowledgement and failure handling");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(prefix + functions + main)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(root / 'lisa-tools/darwin-xnu-build/xnu/bsd/dev'), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
