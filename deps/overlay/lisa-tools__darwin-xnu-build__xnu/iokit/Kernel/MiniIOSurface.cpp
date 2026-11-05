#include "MiniIOSurface.h"
#include <IOKit/IOLib.h>
MiniIOSurface *MiniIOSurfaceCopy(uint32_t id) {
	SweetSurface *s = sweet_surface_copy(id);
	if (!s || !s->memory) { OSSafeReleaseNULL(s); return NULL; }
	MiniIOSurface *m = (MiniIOSurface *)IOMallocZero(sizeof(*m));
	if (!m) { s->release(); return NULL; }
	m->sweet = s; m->memory = s->memory; m->memory->retain();
	m->kbase = (uint8_t *)(uintptr_t)s->kernelAddress();
	m->pixelStart = 0;	/* SweetSurface: pixels at the start of its memory */
	m->client = s->wrapped ? (void *)s : NULL;	/* a caller's own memory: not in whole pages; the GPU import refuses it */
	m->info = {s->allocSize, s->offset, s->bytesPerRow, s->width, s->height, s->pixelFormat, s->planeCount};
	return m;
}
void MiniIOSurfaceDrop(MiniIOSurface *m) {
	if (!m) return; OSSafeReleaseNULL(m->memory); OSSafeReleaseNULL(m->sweet); IOFree(m, sizeof(*m));
}
/* The CPU framebuffer remains the compositor until a display-engine bridge exists. */
extern "C" void MiniIOMobileFramebufferShow(const uint8_t *, uint32_t, uint32_t, uint32_t) {}
extern "C" void lisa_tl(const char *, ...) {}
