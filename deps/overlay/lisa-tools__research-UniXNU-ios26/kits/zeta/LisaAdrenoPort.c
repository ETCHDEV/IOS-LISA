/* xnu-12377 compatibility edge for Lisa's proven SC7280 Adreno core.
 * The real LisaGPU service replaces these presentation/service shims when its
 * user-client port is linked; staged adreno=N boots stop the GPU before then.
 */
#include <libkern/libkern.h>
#include <stdint.h>
#include <sys/systm.h>

void lisa_tl(const char *format, ...) { (void)format; }

void
MiniIOMobileFramebufferShow(const uint8_t *pixels, uint32_t width,
    uint32_t height, uint32_t row_bytes)
{
	(void)pixels; (void)width; (void)height; (void)row_bytes;
}

void
LisaGPUStart(void)
{
	printf("LISA GPU: kernel core reached service handoff (user client not linked yet)\n");
}

void LisaGPUStop(void) {}
