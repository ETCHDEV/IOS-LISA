#!/usr/bin/env python3
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / 'darwin-xnu-build/xnu/iokit/Kernel/arm/SweetBattery.cpp').read_text()
assert 'PE_parse_boot_argn("lisabattery"' in s
assert 'sweet_spmi_init() == 0' in s
assert 'fReal = lisaBattery == 1' in s
print('Lisa battery observer gate checks passed')
