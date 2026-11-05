#!/usr/bin/env python3
"""Test XNU UFS capacity and range validation against a simulated LUN."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root / 'lisa-tools/darwin-xnu-build/xnu/bsd/dev/sweet_ufs.c').read_text()
functions = s[s.index('static int\nufs_read_capacity('):s.index('\nstatic int\nufs_sync_cache(')]
preamble = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#define UFS_BLOCK 4096u
#define BOUNCE_SIZE (1u << 20)
#define DMA_BOUNCE 0
static unsigned char bounce[BOUNCE_SIZE];
static struct { uintptr_t dma; uint64_t device_blocks,first_block,nblocks; bool writable; } su;
static int calls, fail; static unsigned char capacity[8];
static int ufs_scsi(const uint8_t *cdb,unsigned length,uint32_t bytes,bool write) {
 (void)length; (void)write; calls++; if (fail) return EIO;
 if (cdb[0]==0x25) { if (bytes!=8) return EINVAL; for (int i=0;i<8;i++) bounce[i]=capacity[i]; }
 return 0;
}
'''
main = r'''
#include <assert.h>
int main(void) {
 su.dma=(uintptr_t)bounce; unsigned char valid[8]={0x01,0xd3,0x30,0xff,0,0,0x10,0};
 for(int i=0;i<8;i++) capacity[i]=valid[i];
 assert(ufs_read_capacity()==0 && su.device_blocks==0x1d33100);
 int n=calls; assert(ufs_read_blocks(su.device_blocks-1,1)==0 && calls==n+1);
 n=calls; assert(ufs_read_blocks(su.device_blocks-1,2)==EINVAL && calls==n);
 su.first_block=100; su.nblocks=10; su.writable=true;
 assert(ufs_write_blocks(109,1)==0); n=calls;
 assert(ufs_write_blocks(109,2)==EPERM && calls==n);
 capacity[4]=1; su.device_blocks=0; assert(ufs_read_capacity()==ENXIO && !su.device_blocks);
 for(int i=0;i<4;i++) capacity[i]=0xff; capacity[4]=0;
 assert(ufs_read_capacity()==ENXIO && !su.device_blocks);
 fail=1; assert(ufs_read_capacity()==EIO && !su.device_blocks);
 puts("UFS capacity and bounds: PASS"); return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    path=Path(td)/'test.c'; path.write_text(preamble+functions+main)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(path),'-o',str(Path(td)/'test')],check=True)
    subprocess.run([str(Path(td)/'test')],check=True)
