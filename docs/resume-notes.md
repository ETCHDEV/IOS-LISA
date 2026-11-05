---
name: ios26-port-resume
description: Lisa iOS 26 port is mid bring-up; IOS26-PORT-PLAN.md has the exact resume point and commands
metadata:
  type: project
---
Porting iOS 26.5.2 to Xiaomi Lisa (SM7325). Read IOS26-PORT-PLAN.md, section "Resume here", first. As of 2026-11-03 SpringBoard starts and needs the display driver.

**Why:** user's long-running project, continued across sessions.
**How to apply:** user wants low token use, short updates, and the most stable option for each part rather than only following the zeta port. Boot kernels with fastboot boot only; never replace the phone's root without being asked.
