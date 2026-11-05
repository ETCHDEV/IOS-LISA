#ifndef _LISA_MINI_IOSURFACE_H_
#define _LISA_MINI_IOSURFACE_H_
#include "arm/SweetSurface.h"
struct MiniIOSurfaceInfo { uint32_t allocSize, offset, bytesPerRow, width, height, pixelFormat, planeCount; };
struct MiniIOSurface { void *client; IOMemoryDescriptor *memory; uint8_t *kbase; MiniIOSurfaceInfo info; uint32_t drawn; SweetSurface *sweet;
	/* Where the pixels start in `memory`: upstream HakiOS keeps a shared-state page first (PAGE_SIZE);
	   Lisa's SweetSurface keeps state separately and its pixels at 0. */
	uint64_t pixelStart; };
MiniIOSurface *MiniIOSurfaceCopy(uint32_t id);
void MiniIOSurfaceDrop(MiniIOSurface *surface);
bool MiniIOSurfaceDrawn(MiniIOSurface *surface, uint64_t milliseconds);
#endif
