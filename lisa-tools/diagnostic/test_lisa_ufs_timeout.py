#!/usr/bin/env python3
"""Exercise the actual UFS command function with a simulated timeout."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lisa-tools/darwin-xnu-build/xnu/bsd/dev/sweet_ufs.c').read_text()
function = source[source.index('static int\nufs_scsi('):source.index('/* Validate the LUN geometry')]
preamble = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <errno.h>
#define BOUNCE_SIZE (1u << 20)
#define PRDT_CHUNK (256u << 10)
#define PRDT_MAX 4
#define DMA_UTRL 0
#define DMA_UCD 0x1000
#define DMA_BOUNCE 0x4000
#define REG_UTRLDBR 0x58
#define REG_UTRLCLR 0x5c
#define REG_IS 0x20
#define CMD_TIMEOUT_NS 5
#define CMD_SPIN_NS 1
#define CMD_SLEEP_NS 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define __builtin_arm_dsb(x) ((void)0)
#define THREAD_UNINT 0
#define TIMEOUT_URGENCY_SYS_CRITICAL 0
#define THREAD_WAITING 1
#define THREAD_CONTINUE_NULL 0
typedef void *event_t;
struct utrd { uint32_t dw0,dw1,dw2,dw3,ucdba,ucdbau,resp,prdt; };
struct ucd { uint8_t cmd[512],resp[512]; struct { uint32_t addr_lo,addr_hi,rsvd,dbc; } prdt[PRDT_MAX]; };
static struct { uintptr_t dma; uint64_t dma_pa; uint8_t lun,tag; bool cache,wait,ready,faulted; } su;
static int stalled, doorbells, clears; static uint64_t ticks;
static uint32_t ufs_rd(uint32_t off) { return off == REG_UTRLDBR && stalled ? 1 : 0; }
static void ufs_wr(uint32_t off, uint32_t value) {
 if (off == REG_UTRLCLR) { assert(value == ~1u); clears++; }
 if (off == REG_UTRLDBR) { doorbells++; if (!stalled) { ((struct utrd *)(su.dma+DMA_UTRL))->dw2=0; ((struct ucd *)(su.dma+DMA_UCD))->resp[0]=0x21; } }
}
static void dma_clean(uint32_t a,uint32_t b) {(void)a;(void)b;}
static void dma_sync(uint32_t a,uint32_t b) {(void)a;(void)b;}
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t *v) {*v=n;}
static uint64_t mach_absolute_time(void) {return ticks++;}
static int ml_get_interrupts_enabled(void) {return 0;}
static int ml_at_interrupt_context(void) {return 0;}
static int get_preemption_level(void) {return 0;}
static int assert_wait_deadline_with_leeway(event_t e,int a,int b,uint64_t c,int d) {(void)e;(void)a;(void)b;(void)c;(void)d;return 0;}
static void thread_block(int x) {(void)x;}
static void delay(int x) {(void)x;}
'''
main = r'''
int main(void) {
 static unsigned char dma[DMA_BOUNCE + BOUNCE_SIZE], snapshot[sizeof(dma)];
 uint8_t cdb[6]={0}; su.dma=(uintptr_t)dma; su.dma_pa=0x100000;
 assert(ufs_scsi(cdb, sizeof(cdb), 0, false)==0);
 assert(!su.faulted && doorbells==1 && clears==0);
 stalled=1;
 assert(ufs_scsi(cdb, sizeof(cdb), 0, false)==ETIMEDOUT);
 assert(su.faulted && doorbells==2 && clears==1);
 memcpy(snapshot,dma,sizeof(dma)); uint8_t tag=su.tag;
 assert(ufs_scsi(cdb, sizeof(cdb), 0, false)==EIO);
 assert(su.tag==tag && doorbells==2 && clears==1 && memcmp(snapshot,dma,sizeof(dma))==0);
 puts("UFS timeout quarantine: PASS"); return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    path = Path(td) / 'test.c'
    path.write_text(preamble + function + main)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(path), '-o', str(Path(td) / 'test')], check=True)
    subprocess.run([str(Path(td) / 'test')], check=True)
