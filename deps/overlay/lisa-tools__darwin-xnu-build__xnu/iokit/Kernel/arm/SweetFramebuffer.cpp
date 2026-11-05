/*
 * Built-in IOMobileFramebuffer for SWEET: the ABL's continuous-splash
 * framebuffer (1080x2400 a8r8g8b8 in RAM, the panel scans it out by
 * itself) as the main display, driven by CoreAnimation's CPU renderer.
 *
 * IOMobileFramebuffer.framework (iOS 16.7) counts /arm-io children with
 * device_type "display-subsystem", waits for that many services matching
 * IOServiceMatching("IOMobileFramebuffer"), opens each with type 0 and
 * reads canvas-width/canvas-height from the service's provider. The class
 * here (AppleCLCD, an IOMobileFramebuffer) matches the /arm-io/disp0 nub.
 *
 * User client selectors used by CoreAnimation:
 *   3  GetLayerDefaultSurface [layer, ?] -> [IOSurface port name]
 *   4  SwapBegin -> [swap id]
 *   5  SwapEnd, struct 0x46c: +0x50 swap id, +0x54 IOSurface id per layer
 *      (4 layers), +0x64 rect a / +0xc4 rect b per layer (int x,y,w,h),
 *      +0x104 enabled layer mask
 *   6  SwapWait, 7 GetID, 8 GetDisplaySize -> [w, h],
 *   12 RequestPowerChange [state]: 0 = display off (backboardd, power
 *      button / auto-lock), else on. Off fades the panel out over a few
 *      vsyncs (halving each pixel) and keeps it black until on again;
 *      the panel itself stays powered (AMOLED black pixels are off).
 *   18 IsMainDisplay,
 *   28 GetDotPitch, 72 Enable/DisableNotifications [func, refcon, type, 0]
 * Everything else is accepted and logged once.
 *
 * SwapEnd copies the enabled layers' surfaces into the panel memory (no
 * scaling, CPU copy). Vsync (notification type 5) is a 60 Hz timer that
 * sends (count, mach_absolute_time, 0) as 32-bit halves to clients that
 * enabled it, through the port they registered (IOConnectSetNotificationPort).
 *
 * The panel memory itself is published as an IOSurface and returned as the
 * default framebuffer surface. The kernel text console keeps drawing over
 * the GUI by default: boot-arg sweetfb_noconsole=1 stops drawing it from
 * the first swap on (it still reaches the persistent log; panics draw).
 */
#include "SweetSurface.h"

extern "C" int sweet_console_muted;     /* osfmk/console/serial_console.c */
extern "C" void vc_progress_set(boolean_t enable, uint32_t vc_delay);   /* osfmk/console/video_console.c */
/* bsd/dev/lisa_cpufreq.c: the GPU-present handshake and colour matrices */
extern "C" uint32_t lisa_swap_ids[8];
extern "C" uint32_t lisa_swap_next;
extern "C" uint64_t lisa_swap_last;
extern "C" uint32_t lisa_gpu_shown[4];
extern "C" int lisa_gpu_handshake;
extern "C" void lisa_display_set_awake(int awake);
extern "C" int lisa_display_hardware_brightness(void);
extern "C" int lisa_display_refresh_hz(void);
struct lisa_color_matrix {
	uint32_t gen;
	uint32_t count;
	struct { uint64_t key; uint64_t m[9]; } e[4];
};
extern "C" struct lisa_color_matrix lisa_color_matrix;
static uint32_t lisa_fb_calls_logged;
static uint64_t lisa_fb_gpu_skipped, lisa_fb_cpu_fallback;

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/system.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <sys/proc.h>

enum {
	kSweetFBGetLayerDefaultSurface  = 3,
	kSweetFBSwapBegin               = 4,
	kSweetFBSwapEnd                 = 5,
	kSweetFBSwapWait                = 6,
	kSweetFBGetID                   = 7,
	kSweetFBGetDisplaySize          = 8,
	kSweetFBRequestPowerChange      = 12,
	kSweetFBIsMainDisplay           = 18,
	kSweetFBGetDotPitch             = 28,
	kSweetFBNotifications           = 72,
};

#define SWEET_FB_SWAP_REC_SIZE          0x46c
#define SWEET_FB_LAYERS                 4
#define SWEET_FB_NOTIFY_TYPES           8
#define SWEET_FB_NOTIFY_VSYNC           5
/* Cadence follows the successfully sent CSOT mode switch; boot starts at 60. */
static uint32_t sweet_fb_vsync_us(void) { return lisa_display_refresh_hz() == 90 ? 11111 : 16667; }
#define SWEET_FB_VSYNC_US sweet_fb_vsync_us()
#define SWEET_FB_DOT_PITCH              458     /* ppi, as the iPhone X panel */
/*
 * The iOS userland only has status bar / layout data for real iPhone
 * screens (UIKit: "No status bar visual provider for screen", the red
 * rdar://45025538 bar for 360x800 points). The display is therefore
 * presented as the iPhone X panel, 1125x2436 (375x812 points @3x), and the
 * centered 1080x2400 part of it is shown on the 1080x2400 panel.
 * Boot-arg sweetfb_native=1 presents the real panel size instead.
 */
#define SWEET_FB_LOGICAL_W              1125
#define SWEET_FB_LOGICAL_H              2436
#define SWEET_FB_FORMAT_BGRA            0x42475241

class IOMobileFramebuffer : public IOService {
	OSDeclareDefaultStructors(IOMobileFramebuffer);
};

OSDefineMetaClassAndStructors(IOMobileFramebuffer, IOService);

class IOMobileFramebufferUserClient;

class AppleCLCD : public IOMobileFramebuffer {
	OSDeclareDefaultStructors(AppleCLCD);

	IOWorkLoop         *fWorkLoop;
	IOTimerEventSource *fVsync;
	uint64_t            fVsyncCount;

	static void vsyncFired(OSObject *owner, IOTimerEventSource *sender);

public:
	void resumeVsync(void);
	uint64_t      fPhys;
	uint32_t      fWidth, fHeight, fRowBytes;
	uint32_t      fLogicalW, fLogicalH;     /* size reported to userland */
	int32_t       fOffX, fOffY;             /* logical origin of the panel */
	uintptr_t     fPixels;          /* kernel mapping of the panel memory */
	IOMemoryMap  *fMap;
	SweetSurface *fSurface;         /* the panel memory as an IOSurface */
	SweetSurface *fPanelWide;       /* the same memory, two rows a row, for the GPU (lisa_gpu_present) */
	bool          fGPUPresent;      /* boot-arg lisa_gpu_present=1: the GPU draws the panel; no CPU copy */
	IOLock       *fLock;            /* swaps and client list */
	OSArray      *fClients;
	uint32_t      fSwapID;
	bool          fConsolePaused;
	uint8_t       fPending[SWEET_FB_SWAP_REC_SIZE];     /* latest swap, not yet shown */
	bool          fDirty;
	uint32_t      fFlushTicks;    /* vsync ticks between panel copies */
	bool          fPowerOff;      /* RequestPowerChange(0) */
	uint32_t      fFadeStep;      /* fade-out steps done while off */

	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void addClient(IOMobileFramebufferUserClient *client);
	void removeClient(IOMobileFramebufferUserClient *client);
	void swap(const uint8_t *rec);
	void present(const uint8_t *rec);
};

OSDefineMetaClassAndStructors(AppleCLCD, IOMobileFramebuffer);

class IOMobileFramebufferUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOMobileFramebufferUserClient);

public:
	AppleCLCD          *fFB;
	task_t              fTask;
	mach_port_t         fPort;
	io_user_reference_t fFunc[SWEET_FB_NOTIFY_TYPES];
	io_user_reference_t fRefcon[SWEET_FB_NOTIFY_TYPES];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	void notify(uint32_t type, const uint64_t *values, uint32_t count);
};

OSDefineMetaClassAndStructors(IOMobileFramebufferUserClient, IOUserClient);

/*
 * Metal discovers a device only after IOAcceleratorES is present.  The service
 * is deliberately inert until a user-space Lisa Metal/Mesa bridge opts in via
 * metal=1: publishing a fake Apple device would make backboardd crash-loop.
 */
#define kLisaMetalPluginNameKey      "MetalPluginName"
#define kLisaMetalPluginClassNameKey "MetalPluginClassName"
static bool gLisaAcceleratorCanSetProperties;

class IOAcceleratorES : public IOService {
	OSDeclareDefaultStructors(IOAcceleratorES);
public:
	IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(IOAcceleratorES, IOService);

IOReturn
IOAcceleratorES::setProperties(OSObject *properties)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
	OSCollectionIterator *keys;
	OSObject *next;
	if (!gLisaAcceleratorCanSetProperties || dict == NULL || dict->getCount() == 0)
		return kIOReturnUnsupported;
	keys = OSCollectionIterator::withCollection(dict);
	if (keys == NULL)
		return kIOReturnNoMemory;
	while ((next = keys->getNextObject()) != NULL) {
		const OSSymbol *key = OSDynamicCast(OSSymbol, next);
		if (key == NULL || (!key->isEqualTo(kLisaMetalPluginNameKey) &&
		    !key->isEqualTo(kLisaMetalPluginClassNameKey)) ||
		    OSDynamicCast(OSString, dict->getObject(key)) == NULL) {
			keys->release();
			return kIOReturnUnsupported;
		}
	}
	keys->reset();
	while ((next = keys->getNextObject()) != NULL) {
		const OSSymbol *key = OSDynamicCast(OSSymbol, next);
		OSString *value = OSDynamicCast(OSString, dict->getObject(key));
		if (value->getLength() == 0)
			removeProperty(key);
		else if (!setProperty(key, value)) {
			keys->release();
			return kIOReturnNoMemory;
		}
	}
	keys->release();
	IOLog("LISA GPU: Metal bridge properties installed\n");
	return kIOReturnSuccess;
}

static void
lisa_publish_accelerator(IOService *provider)
{
	uint32_t metal = 0;
	uint32_t lisaMetal = 0;
	gLisaAcceleratorCanSetProperties = PE_parse_boot_argn("metal", &metal, sizeof(metal)) && metal != 0;
	PE_parse_boot_argn("lisametal", &lisaMetal, sizeof(lisaMetal));
	IOAcceleratorES *accelerator = OSTypeAlloc(IOAcceleratorES);
	if (accelerator != NULL && accelerator->init() && accelerator->attach(provider)) {
		accelerator->setName("sgx");
		/* Published only for an explicit boot containing the staged bundle. */
		if (gLisaAcceleratorCanSetProperties && lisaMetal != 0) {
			accelerator->setProperty(kLisaMetalPluginNameKey, "LisaMetal");
			accelerator->setProperty(kLisaMetalPluginClassNameKey, "LisaMetalDevice");
		}
		accelerator->registerService();
		IOLog("LISA GPU: IOAcceleratorES bridge published%s\n",
		    lisaMetal != 0 ? " with LisaMetal" :
		    (gLisaAcceleratorCanSetProperties ? " (metal properties enabled)" : ""));
	}
	OSSafeReleaseNULL(accelerator);
}

bool
AppleCLCD::start(IOService *provider)
{
	PE_Video video;

	if (!IOService::start(provider)) {
		return false;
	}
	PE_current_console(&video);
	fPhys = video.v_baseAddr;
	fWidth = (uint32_t)video.v_width;
	fHeight = (uint32_t)video.v_height;
	fRowBytes = (uint32_t)video.v_rowBytes;
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fRowBytes < fWidth * 4) {
		IOLog("SWEET FB: no boot framebuffer\n");
		return false;
	}
	int native = 0;
	PE_parse_boot_argn("sweetfb_native", &native, sizeof(native));
	fLogicalW = fWidth;
	fLogicalH = fHeight;
	if (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {
		fLogicalW = SWEET_FB_LOGICAL_W;
		fLogicalH = SWEET_FB_LOGICAL_H;
	}
	fOffX = (int32_t)(fLogicalW - fWidth) / 2;
	fOffY = (int32_t)(fLogicalH - fHeight) / 2;
	fLock = IOLockAlloc();
	fClients = OSArray::withCapacity(4);
	fSurface = sweet_surface_create_physical(fPhys, fWidth, fHeight, fRowBytes, SWEET_FB_FORMAT_BGRA);
	fPixels = fSurface != NULL ? fSurface->kernelAddress() : 0;
	{
		/*
		 * Lisa GPU present: the Adreno needs rows in multiples of 64 bytes; the panel's are 4320.
		 * The same memory as 2W x H/2 with 2*rowBytes rows (8640 = 135 * 64) is a surface the GPU
		 * can draw into; OpenGL ES (eagl.c, panel_present) finds it by these properties.
		 */
		int gp = 0;
		PE_parse_boot_argn("lisa_gpu_present", &gp, sizeof(gp));
		if (gp == 1 && (fHeight & 1) == 0 && ((fRowBytes * 2) & 63) == 0) {
			fPanelWide = sweet_surface_create_physical(fPhys, fWidth * 2, fHeight / 2, fRowBytes * 2, SWEET_FB_FORMAT_BGRA);
			if (fPanelWide != NULL) {
				fGPUPresent = true;
				setProperty("LisaPanelSurface", (unsigned long long)fPanelWide->id, 32);
				setProperty("LisaPanelOffset", (unsigned long long)(((uint32_t)fOffX << 16) | ((uint32_t)fOffY & 0xffff)), 32);
				IOLog("LISA FB: GPU present: panel published as surface %u (%ux%u, %u bytes a row), offset %d,%d\n",
				    fPanelWide->id, fWidth * 2, fHeight / 2, fRowBytes * 2, fOffX, fOffY);
			}
		}
	}
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fPixels == 0 || fWorkLoop == NULL) {
		IOLog("SWEET FB: setup failed\n");
		OSSafeReleaseNULL(fSurface);    /* stays published: also held by the table */
		OSSafeReleaseNULL(fWorkLoop);
		return false;
	}
	fVsync = IOTimerEventSource::timerEventSource(this, &AppleCLCD::vsyncFired);
	if (fVsync == NULL || fWorkLoop->addEventSource(fVsync) != kIOReturnSuccess) {
		IOLog("SWEET FB: no vsync timer\n");
		return false;
	}
	uint32_t hz = 10;
	PE_parse_boot_argn("sweetfb_hz", &hz, sizeof(hz));
	fFlushTicks = (hz == 0 || hz >= 60) ? 1 : 60 / hz;
	fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);

	setProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");
	setProperty("external", false);
	setProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);
	registerService();
	lisa_publish_accelerator(this);
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id, fLogicalW, fLogicalH);
	return true;
}

void
AppleCLCD::vsyncFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	uint64_t now = mach_absolute_time();
	uint64_t count = ++fb->fVsyncCount;
	uint64_t values[6] = {
		(uint32_t)count, (uint32_t)(count >> 32),
		(uint32_t)now, (uint32_t)(now >> 32),
		0, 0,
	};

	IOLockLock(fb->fLock);
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		IOMobileFramebufferUserClient *client =
		    (IOMobileFramebufferUserClient *)fb->fClients->getObject(i);
        if (!fb->fPowerOff) client->notify(SWEET_FB_NOTIFY_VSYNC, values, 6);
    }
    if (fb->fPowerOff) {
        uint32_t step = fb->fFadeStep++;
        IOLockUnlock(fb->fLock);
        /* DCS now blanks the actual panel. Mutating its pixels here raced with
         * in-flight GPU frames and left a half-dark image visible on wake. */
        if (!lisa_display_hardware_brightness() && step < 2) {
            for (uint32_t y = 0; y < fb->fHeight; y++) {
                volatile uint32_t *row = (volatile uint32_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
                for (uint32_t px = 0; px < fb->fWidth; px++) row[px] = 0;
            }
        }
        sender->setTimeoutUS(100000);
        return;
    }
	bool flush = fb->fDirty && (count % fb->fFlushTicks) == 0;
	uint8_t *rec = NULL;
	if (flush) {
		rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
		if (rec != NULL) {
			memcpy(rec, fb->fPending, SWEET_FB_SWAP_REC_SIZE);
			fb->fDirty = false;
		}
	}
	IOLockUnlock(fb->fLock);
	if (rec != NULL) {
		fb->present(rec);
		IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE);
	}
	sender->setTimeoutUS(SWEET_FB_VSYNC_US);
}

void
AppleCLCD::resumeVsync(void)
{
    fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);
}

void
AppleCLCD::addClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	fClients->setObject(client);
	IOLockUnlock(fLock);
}

void
AppleCLCD::removeClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	unsigned int i = fClients->getNextIndexOfObject(client, 0);
	if (i != (unsigned int)-1) {
		fClients->removeObject(i);
	}
	IOLockUnlock(fLock);
}

static uint32_t
get32(const uint8_t *p, uint32_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

static uint32_t sweet_fb_swaps_logged;
static uint64_t sweet_fb_swap_count;
static uint64_t lisa_fb_presents, lisa_fb_copied, lisa_fb_default;

extern "C" uint64_t lisa_fb_present_count(void) { return __atomic_load_n(&lisa_fb_presents, __ATOMIC_RELAXED); }
extern "C" uint64_t lisa_fb_copy_count(void) { return __atomic_load_n(&lisa_fb_copied, __ATOMIC_RELAXED); }
extern "C" uint64_t lisa_fb_default_count(void) { return __atomic_load_n(&lisa_fb_default, __ATOMIC_RELAXED); }

extern "C" uint64_t lisa_fb_swap_count(void)
{
	return __atomic_load_n(&sweet_fb_swap_count, __ATOMIC_RELAXED);
}

/* Copy each enabled layer's surface into the panel memory (no scaling). */
/*
 * A swap only records the frame: copying 10 MB into the panel on every
 * swap (backboardd presents at 60 Hz) took the whole CPU. The vsync timer
 * shows the latest frame every fFlushTicks ticks (boot-arg sweetfb_hz,
 * default 10 Hz).
 */
void
AppleCLCD::swap(const uint8_t *rec)
{
	IOLockLock(fLock);
	{
		/*
		 * Where per-swap settings live (Night Shift / colour filters: CoreAnimation sends no
		 * SetMatrix after boot): log which bytes outside the layer ids/rects (0x50..0x108)
		 * differ from the previous swap, the first 64 times anything else changes.
		 */
		static uint8_t last[SWEET_FB_SWAP_REC_SIZE];
		static uint16_t stable[SWEET_FB_SWAP_REC_SIZE / 4];
		static bool have;
		static uint32_t told;
		for (uint32_t w = 0; have && w < SWEET_FB_SWAP_REC_SIZE / 4; w++) {
			uint32_t off = w * 4;
			if (off >= 0x50 && off < 0x108) {
				continue;
			}
			if (get32(last, off) == get32(rec, off)) {
				if (stable[w] < 60000) {
					stable[w]++;
				}
				continue;
			}
			/* a word that held still for 30 swaps and moved: a setting, not a counter */
			if (stable[w] >= 30 && told < 96) {
				told++;
				IOLog("LISA FB: swap record word 0x%x: %08x -> %08x (still for %u swaps)\n", off,
				    get32(last, off), get32(rec, off), stable[w]);
			}
			stable[w] = 0;
		}
		memcpy(last, rec, SWEET_FB_SWAP_REC_SIZE);
		have = true;
	}
	memcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);
	fDirty = true;
	{
		uint32_t mask = get32(rec, 0x104);
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
			uint32_t id = get32(rec, 0x54 + 4 * layer);
			bool known = false;
			if (!(mask & (1u << layer)) || id == 0) {
				continue;
			}
			for (uint32_t i = 0; i < 8; i++) {
				known |= lisa_swap_ids[i] == id;
			}
			if (!known) {
				__atomic_store_n(&lisa_swap_ids[lisa_swap_next++ % 8], id, __ATOMIC_RELEASE);
			}
			if (layer == 0) {
				uint64_t seq = (__atomic_load_n(&lisa_swap_last, __ATOMIC_RELAXED) >> 32) + 1;
				__atomic_store_n(&lisa_swap_last, (seq << 32) | id, __ATOMIC_RELEASE);
			}
		}
	}
	IOLockUnlock(fLock);
	uint64_t swaps = __atomic_add_fetch(&sweet_fb_swap_count, 1, __ATOMIC_RELAXED);
	if ((swaps % 1000) == 0) {
		IOLog("SWEET FB: %llu swaps\n", swaps);
	}
}

void
AppleCLCD::present(const uint8_t *rec)
{
	uint32_t mask = get32(rec, 0x104);
	__atomic_add_fetch(&lisa_fb_presents, 1, __ATOMIC_RELAXED);


	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x54 + 4 * layer);
		const uint8_t *a = rec + 0x64 + 16 * layer;
		const uint8_t *b = rec + 0xc4 + 16 * layer;
		int32_t sx = (int32_t)get32(a, 0), sy = (int32_t)get32(a, 4);
		int32_t dx = (int32_t)get32(b, 0), dy = (int32_t)get32(b, 4);
		int32_t w = (int32_t)get32(b, 8), h = (int32_t)get32(b, 12);

		if (!(mask & (1u << layer)) || id == 0) {
			continue;
		}
		SweetSurface *s = sweet_surface_copy(id);
		if (sweet_fb_swaps_logged < 16) {
			sweet_fb_swaps_logged++;
			IOLog("SWEET FB: swap %u layer %u surface %u %ux%u fmt 0x%x bpr %u a %d,%d %ux%u b %d,%d %dx%d\n",
			    get32(rec, 0x50), layer, id, s ? s->width : 0, s ? s->height : 0,
			    s ? s->pixelFormat : 0, s ? s->bytesPerRow : 0, sx, sy, get32(a, 8),
			    get32(a, 12), dx, dy, w, h);
		}
		if (s == NULL || s == fSurface) {
			if (s == fSurface) __atomic_add_fetch(&lisa_fb_default, 1, __ATOMIC_RELAXED);
			OSSafeReleaseNULL(s);
			continue;
		}
		uintptr_t src = s->kernelAddress();
		uint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;
		/*
		 * iOS is on the screen: stop drawing the kernel console over it (sweetfb_noconsole=1).
		 * It was only paused on the partial-layer path, so with full-screen layers the log text
		 * kept flickering over iOS. The log still goes to the ring the recovery pull reads.
		 */
		if (!fConsolePaused && s->width == fLogicalW && s->height == fLogicalH) {
			int quiet = 0;
			IOLog("LISA FB: first full-screen layer: console %s\n",
			    PE_parse_boot_argn("sweetfb_noconsole", &quiet, sizeof(quiet)) && quiet != 0 ? "muted" : "kept");
			fConsolePaused = true;
			if (quiet != 0) {
				sweet_console_muted = 1;
			}
			/*
			 * The boot progress meter (osfmk/console/video_console.c) keeps redrawing its bar into the
			 * panel memory from a timer: the 1-2 pixel white mark below the centre that stayed over
			 * iOS. iOS is drawing now: stop it.
			 */
			vc_progress_set(FALSE, 0);
			IOLog("LISA FB: boot progress meter stopped\n");
		}
		/* The GPU drew this one onto the panel already (liblisagl's handshake): no CPU copy. */
		bool gpuDid = false;
		if (fGPUPresent && lisa_gpu_handshake && s->width == fLogicalW && s->height == fLogicalH) {
			for (uint32_t i = 0; i < 4; i++) {
				gpuDid |= __atomic_load_n(&lisa_gpu_shown[i], __ATOMIC_ACQUIRE) == id;
			}
		}
		if (gpuDid) {
			__atomic_add_fetch(&lisa_fb_gpu_skipped, 1, __ATOMIC_RELAXED);
			s->release();
			continue;
		}
		/*
		 * A presenter on the GPU is at work (it has answered before) but has not drawn this swap yet:
		 * wait up to two ticks for it rather than copying 14 MB with the processor meanwhile
		 * (Metal window server, liblisagl's presenter thread).
		 */
		if (fGPUPresent && lisa_gpu_handshake && s->width == fLogicalW && s->height == fLogicalH) {
			static uint32_t waited_id, waited_ticks;
			if (waited_id != id) {
				waited_id = id;
				waited_ticks = 0;
			}
			if (waited_ticks++ < 2) {
				IOLockLock(fLock);
				fDirty = true;          /* look again next tick */
				IOLockUnlock(fLock);
				s->release();
				continue;
			}
		}
		if (fGPUPresent && s->width == fLogicalW && s->height == fLogicalH) {
			uint64_t n = __atomic_add_fetch(&lisa_fb_cpu_fallback, 1, __ATOMIC_RELAXED);
			if (n <= 8 || (n % 500) == 0) {
				IOLog("LISA FB: surface %u was not drawn by the GPU (handshake %d): CPU copy #%llu, %llu GPU-drawn\n",
				    id, lisa_gpu_handshake, n, __atomic_load_n(&lisa_fb_gpu_skipped, __ATOMIC_RELAXED));
			}
		}
		if (w <= 0 || h <= 0) {
			w = (int32_t)s->width;
			h = (int32_t)s->height;
		}
		if (sx < 0 || sy < 0) {
			sx = sy = 0;
		}
		if (dx < 0 || dy < 0) {
			dx = dy = 0;
		}
		/* The full logical canvas is scaled, not cropped. Keep the CPU fallback
		 * consistent with the GLES panel shader and the digitizer mapping. */
		if (src && bpe == 4 && sx == 0 && sy == 0 && dx == 0 && dy == 0 &&
		    s->width == fLogicalW && s->height == fLogicalH &&
		    s->elementWidth == 1 && s->elementHeight == 1 && s->memory &&
		    (uint64_t)s->offset + (uint64_t)(s->height - 1) * s->bytesPerRow + (uint64_t)s->width * 4 <= s->memory->getLength()) {
			/*
			 * The GPU drew this frame into memory past the processor's cache; the cache may still hold
			 * what was there before (the zeroes of the surface's making): read it from memory.
			 * (v39m: the Metal window server's frames, copied here, came out black.)
			 */
			if (OSDynamicCast(IOBufferMemoryDescriptor, s->memory) != NULL) {
				uintptr_t from = (src + s->offset) & ~(uintptr_t)63;
				uintptr_t to = src + s->offset + (uint64_t)(s->height - 1) * s->bytesPerRow + (uint64_t)s->width * 4;
				__builtin_arm_dsb(0xf);
				for (uintptr_t line = from; line < to; line += 64) {
					__asm__ volatile ("dc civac, %0" : : "r" (line) : "memory");
				}
				__builtin_arm_dsb(0xf);
			}
			for (uint32_t y = 0; y < fHeight; y++) {
				uint32_t iy = (uint64_t)y * fLogicalH / fHeight;
				const uint32_t *input = (const uint32_t *)(src + s->offset + (uint64_t)iy * s->bytesPerRow);
				uint32_t *output = (uint32_t *)(fPixels + (uint64_t)y * fRowBytes);
				for (uint32_t x = 0; x < fWidth; x++) output[x] = input[(uint64_t)x * fLogicalW / fWidth];
			}
			__atomic_add_fetch(&lisa_fb_copied, 1, __ATOMIC_RELAXED);
			s->release();
			continue;
		}
		/* Other partial layers retain their existing clipping path. */
		dx -= fOffX;
		dy -= fOffY;
		if (dx < 0) {
			sx -= dx;
			w += dx;
			dx = 0;
		}
		if (dy < 0) {
			sy -= dy;
			h += dy;
			dy = 0;
		}
		if (w > (int32_t)s->width - sx) {
			w = (int32_t)s->width - sx;
		}
		if (h > (int32_t)s->height - sy) {
			h = (int32_t)s->height - sy;
		}
		if (w > (int32_t)fWidth - dx) {
			w = (int32_t)fWidth - dx;
		}
		if (h > (int32_t)fHeight - dy) {
			h = (int32_t)fHeight - dy;
		}
		uint64_t last = (uint64_t)s->offset + (uint64_t)(sy + h - 1) * s->bytesPerRow +
		    (uint64_t)(sx + w) * 4;
		if (w <= 0 || h <= 0 || s->elementWidth != 1 || s->elementHeight != 1 ||
		    s->memory == NULL || last > s->memory->getLength()) {
			src = 0;
		}
		if (src != 0 && bpe == 4) {
			/* Keep diagnostics visible until a real, validated layer is ready. */
			if (!fConsolePaused) {
				int quiet = 0;
				IOLog("LISA FB: first valid layer %ux%u to %d,%d size %dx%d\n",
				    s->width, s->height, dx, dy, w, h);
				fConsolePaused = true;
				if (PE_parse_boot_argn("sweetfb_noconsole", &quiet, sizeof(quiet)) && quiet != 0) {
					sweet_console_muted = 1;
				}
			}
			for (int32_t y = 0; y < h; y++) {
				memcpy((void *)(fPixels + (uint64_t)(dy + y) * fRowBytes + (uint64_t)dx * 4),
				    (const void *)(src + s->offset + (uint64_t)(sy + y) * s->bytesPerRow + (uint64_t)sx * 4),
				    (size_t)w * 4);
			}
			__atomic_add_fetch(&lisa_fb_copied, 1, __ATOMIC_RELAXED);
		}
		s->release();
	}
}

bool
IOMobileFramebufferUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	return true;
}

bool
IOMobileFramebufferUserClient::start(IOService *provider)
{
	fFB = OSDynamicCast(AppleCLCD, provider);
	if (fFB == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fFB->addClient(this);
	return true;
}

IOReturn
IOMobileFramebufferUserClient::clientClose(void)
{
	if (fFB != NULL) {
		fFB->removeClient(this);
		IOLockLock(fFB->fLock);
		mach_port_t old = fPort;
		fPort = MACH_PORT_NULL;
		IOLockUnlock(fFB->fLock);
		if (old != MACH_PORT_NULL) {
			releaseNotificationPort(old);
		}
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
IOMobileFramebufferUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type,
    __unused io_user_reference_t refCon)
{
	mach_port_t old;
	IOLockLock(fFB->fLock);
	old = fPort;
	fPort = port;
	IOLockUnlock(fFB->fLock);
	if (old != MACH_PORT_NULL) {
		releaseNotificationPort(old);
	}
	return kIOReturnSuccess;
}

/* Called with the framebuffer lock held. */
void
IOMobileFramebufferUserClient::notify(uint32_t type, const uint64_t *values, uint32_t count)
{
	OSAsyncReference64 ref;

	if (type >= SWEET_FB_NOTIFY_TYPES || fFunc[type] == 0 || fPort == MACH_PORT_NULL) {
		return;
	}
	bzero(ref, sizeof(ref));
	setAsyncReference64(ref, fPort, (mach_vm_address_t)fFunc[type], fRefcon[type], fTask);
	sendAsyncResult64WithOptions(ref, kIOReturnSuccess, (io_user_reference_t *)values, count,
	    kIOUserNotifyOptionCanDrop);
}

static uint64_t sweet_fb_logged;        /* selectors already logged (< 64) */

IOReturn
IOMobileFramebufferUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	IOReturn ret = kIOReturnSuccess;
	uint64_t out[2] = { 0, 0 };
	uint32_t outCount = 0;

	switch (selector) {
	case kSweetFBGetLayerDefaultSurface:
		out[0] = sweet_surface_port_in_task(fFB->fSurface, fTask);
		outCount = 1;
		break;
	case kSweetFBSwapBegin:
		IOLockLock(fFB->fLock);
		out[0] = ++fFB->fSwapID;
		IOLockUnlock(fFB->fLock);
		outCount = 1;
		break;
	case kSweetFBSwapEnd: {
		uint8_t rec[SWEET_FB_SWAP_REC_SIZE];
		if (args->structureInputDescriptor != NULL ||
		    args->structureInputSize < SWEET_FB_SWAP_REC_SIZE) {
			ret = kIOReturnBadArgument;
			break;
		}
		memcpy(rec, args->structureInput, sizeof(rec));
		fFB->swap(rec);
		break;
	}
	case kSweetFBGetID:
		out[0] = 0;
		outCount = 1;
		break;
	case kSweetFBGetDisplaySize:
		out[0] = fFB->fLogicalW;
		out[1] = fFB->fLogicalH;
		outCount = 2;
		break;
	case kSweetFBIsMainDisplay:
		out[0] = 1;
		outCount = 1;
		break;
	case kSweetFBGetDotPitch:
		out[0] = SWEET_FB_DOT_PITCH;
		outCount = 1;
		break;
	case kSweetFBNotifications:
		if (args->scalarInputCount >= 3 && args->scalarInput[2] < SWEET_FB_NOTIFY_TYPES) {
			uint32_t type = (uint32_t)args->scalarInput[2];
			IOLockLock(fFB->fLock);
			fFunc[type] = args->scalarInput[0];
			fRefcon[type] = args->scalarInput[1];
			IOLockUnlock(fFB->fLock);
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET FB: %s notifications type %u %s\n", name, type,
			    args->scalarInput[0] ? "on" : "off");
		}
		break;
	case kSweetFBRequestPowerChange:
		if (args->scalarInputCount >= 1) {
			bool off = args->scalarInput[0] == 0;
			IOLockLock(fFB->fLock);
			if (off != fFB->fPowerOff) {
				fFB->fPowerOff = off;
				fFB->fFadeStep = 0;
				fFB->fDirty = true;     /* on: redraw the latest frame */
			}
			IOLockUnlock(fFB->fLock);
			lisa_display_set_awake(!off);
            if (!off) fFB->resumeVsync();
            IOLog("SWEET FB: display %s\n", off ? "off" : "on");
		}
		break;
	case 55:        /* IOMobileFramebufferSetMatrix(type, double[3][3]): Night Shift, white point */
		if (args->structureInputDescriptor == NULL && args->structureInput != NULL &&
		    args->structureInputSize >= 72) {
			uint64_t key = args->scalarInputCount >= 1 ? args->scalarInput[0] : 0;
			uint32_t slot = 0;
			IOLockLock(fFB->fLock);
			while (slot < lisa_color_matrix.count && lisa_color_matrix.e[slot].key != key) {
				slot++;
			}
			if (slot >= 4) {
				slot = 3;
			}
			if (slot >= lisa_color_matrix.count) {
				lisa_color_matrix.count = slot + 1;
			}
			lisa_color_matrix.e[slot].key = key;
			memcpy(lisa_color_matrix.e[slot].m, args->structureInput, 72);
			__atomic_add_fetch(&lisa_color_matrix.gen, 1, __ATOMIC_RELEASE);
			IOLockUnlock(fFB->fLock);
			if (lisa_fb_calls_logged < 400) {
				const uint64_t *m = (const uint64_t *)args->structureInput;
				lisa_fb_calls_logged++;
				IOLog("LISA FB: SetMatrix key %llu: %llx %llx %llx / %llx %llx %llx / %llx %llx %llx\n", key,
				    m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
			}
		}
		break;
	case 17: case 50: case 66: case 68: case 69: case 84:
		if (lisa_fb_calls_logged < 96) {
			uint64_t raw[2] = { 0, 0 };
			lisa_fb_calls_logged++;
			if (args->structureInputDescriptor == NULL && args->structureInput != NULL) {
				memcpy(raw, args->structureInput, args->structureInputSize < 16 ? args->structureInputSize : 16);
			}
			IOLog("LISA FB: sel %u scalars %u [%llx %llx %llx] struct %u [%llx %llx]\n", selector,
			    args->scalarInputCount,
			    args->scalarInputCount > 0 ? args->scalarInput[0] : 0,
			    args->scalarInputCount > 1 ? args->scalarInput[1] : 0,
			    args->scalarInputCount > 2 ? args->scalarInput[2] : 0,
			    args->structureInputSize, raw[0], raw[1]);
		}
		break;
	case kSweetFBSwapWait:
	default:
		break;
	}

	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = i < outCount ? out[i] : 0;
	}
	if (args->structureOutputDescriptor == NULL && args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	if (selector >= 64 || !(sweet_fb_logged & (1ULL << selector))) {
		if (selector < 64) {
			sweet_fb_logged |= 1ULL << selector;
		}
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("SWEET FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args->scalarInputCount, args->structureInputSize,
		    args->scalarOutputCount, args->structureOutputSize, ret);
	}
	return ret;
}
#endif /* SWEET */
