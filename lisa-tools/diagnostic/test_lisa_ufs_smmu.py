#!/usr/bin/env python3
"""Test the actual XNU SMMU check with synthetic register snapshots."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root / 'lisa-tools/darwin-xnu-build/xnu/bsd/dev/sweet_ufs.c').read_text()
fn = s[s.index('static int\nlisa_ufs_smmu_check('):s.index('\nstatic int\nsweet_ufs_init(')]
prefix = '#include <stdint.h>\n#include <errno.h>\n#include <assert.h>\n#include <stdio.h>\n'
main = r'''
int main(void) {
 static uint32_t r[0x100000/4];
 r[0x20/4]=0x4c017e4b; r[0x24/4]=0x6000003d;
 r[0x800/4]=0x80000080; r[0xc00/4]=3;
 r[0x83000/4]=0x0a9f00e0; r[0x83058/4]=0x400; r[0x100c/4]=0x1f000;
 assert(lisa_ufs_smmu_check(r)==0);
 r[0x800/4]=0x80010080; assert(lisa_ufs_smmu_check(r)==ENXIO); r[0x800/4]=0x80000080;
 r[0x83000/4]|=1; assert(lisa_ufs_smmu_check(r)==ENXIO); r[0x83000/4]&=~1u;
 r[0x100c/4]=0x20000; assert(lisa_ufs_smmu_check(r)==ENXIO); r[0x100c/4]=0x1f000;
 r[0x83058/4]=0x402; assert(lisa_ufs_smmu_check(r)==ENXIO); r[0x83058/4]=0x400;
 r[0x804/4]=0x80000090; r[0xc04/4]=3; assert(lisa_ufs_smmu_check(r)==ENXIO);
 r[0x804/4]=0x80000080; assert(lisa_ufs_smmu_check(r)==ENXIO);
 r[0x804/4]=0; r[0xc00/4]=0x10003; assert(lisa_ufs_smmu_check(r)==ENXIO);
 puts("SMMU guarded preflight: PASS"); return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    p=Path(td)/'test.c'; p.write_text(prefix+fn+main)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(Path(td)/'test')],check=True)
    subprocess.run([str(Path(td)/'test')],check=True)
