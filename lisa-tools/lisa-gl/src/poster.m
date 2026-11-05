#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/runtime.h>
#include <sys/sysctl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* iOS 16's PosterBoard preview uses CARenderer with a Metal command queue
 * without checking for a device. In GLES mode that queue is nil, and the
 * options dictionary throws before wallpaper configuration can complete.
 * Only this still-image preview uses CoreGraphics; backboardd keeps its
 * Adreno GLES renderer. Do not globally alter dictionaries or Metal policy. */
static UIImage *poster_preview(id self, SEL command, UIView *view)
{
    (void)self; (void)command;
    CGSize size = view.bounds.size;
    CGFloat scale = view.layer.contentsScale;
    if (!isfinite(scale) || scale <= 0) scale = 1;
    if (!isfinite(size.width) || !isfinite(size.height) ||
        size.width <= 0 || size.height <= 0 ||
        size.width * scale > 4096 || size.height * scale > 4096) return nil;
    UIGraphicsBeginImageContextWithOptions(size, NO, scale);
    CGContextRef context = UIGraphicsGetCurrentContext();
    UIImage *image = nil;
    @try {
        if (context) {
            [view.layer renderInContext:context];
            image = [UIGraphicsGetImageFromCurrentImageContext() retain];
        }
    } @finally {
        UIGraphicsEndImageContext();
    }
    static unsigned logged;
    if (logged++ < 4) dprintf(2, "LISA POSTER: CoreGraphics preview %gx%g @%g: %s\n",
        size.width, size.height, scale, image ? "created" : "unavailable");
    return [image autorelease];
}

__attribute__((constructor)) static void install_poster_preview(void)
{
    if (strcmp(getprogname(), "PosterBoard") != 0) return;
    char args[1024] = {0};
    size_t length = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &length, NULL, 0) != 0 ||
        !strstr(args, "lisagl=1")) return;
    Class cls = objc_getClass("PBFHomeScreenPreviewImageBuilder");
    SEL selector = sel_registerName("_imageForView:");
    Method method = class_getInstanceMethod(cls, selector);
    if (method) {
        method_setImplementation(method, (IMP)poster_preview);
        dprintf(2, "LISA POSTER: installed preview fallback for GLES mode\n");
    } else {
        dprintf(2, "LISA POSTER: preview class unavailable; no hook installed\n");
    }
}

