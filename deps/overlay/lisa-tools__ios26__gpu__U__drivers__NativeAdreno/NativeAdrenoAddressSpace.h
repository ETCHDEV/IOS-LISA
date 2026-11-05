/* SPDX-License-Identifier: MIT */
#ifndef NATIVE_ADRENO_ADDRESS_SPACE_H
#define NATIVE_ADRENO_ADDRESS_SPACE_H
#include <stdint.h>
#include <stddef.h>
/* One 32-bit GPU address space. Keep the first 512 MiB for board/kernel use;
 * clients own disjoint ranges. These are virtual limits, not physical reserves. */
constexpr unsigned NativeAdrenoGpuInputBits=32;
constexpr uint64_t NativeAdrenoGpuApertureBytes=UINT64_C(1)<<NativeAdrenoGpuInputBits;
constexpr uint64_t NativeAdrenoGpuClientStart=0x20000000ull;
constexpr uint64_t NativeAdrenoGpuClientBytes=NativeAdrenoGpuApertureBytes-NativeAdrenoGpuClientStart;
constexpr size_t NativeAdrenoGpuTablePages=1+4+2048;
constexpr unsigned NativeAdrenoGpuClientCount=3;
struct NativeAdrenoGpuWindow {uint64_t start,bytes;};
constexpr NativeAdrenoGpuWindow NativeAdrenoGpuWindows[NativeAdrenoGpuClientCount]={
    {0x20000000ull,0x80000000ull}, /* render server: 2 GiB */
    {0xa0000000ull,0x30000000ull}, /* other Metal client: 768 MiB */
    {0xd0000000ull,0x30000000ull}};
static_assert(NativeAdrenoGpuWindows[0].start==NativeAdrenoGpuClientStart);
static_assert(NativeAdrenoGpuWindows[0].start+NativeAdrenoGpuWindows[0].bytes==NativeAdrenoGpuWindows[1].start);
static_assert(NativeAdrenoGpuWindows[1].start+NativeAdrenoGpuWindows[1].bytes==NativeAdrenoGpuWindows[2].start);
static_assert(NativeAdrenoGpuWindows[2].start+NativeAdrenoGpuWindows[2].bytes==NativeAdrenoGpuApertureBytes);
#endif
