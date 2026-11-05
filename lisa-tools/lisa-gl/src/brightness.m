/* iOS 16.7 backboardd setters measured in gl-v26-codex-recovery.log.
 * Preserve each exact Objective-C ABI and the original system brightness path.
 * The kernel sends DCS brightness when available; GLES is the fallback. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <sys/sysctl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef void (*level_imp)(id, SEL, float, id, uint64_t);
typedef void (*transaction_imp)(id, SEL, float, BOOL);
static level_imp original_level;
static transaction_imp original_transaction;
static void (*original_cc_float)(id, SEL, float);
static void (*original_cc_double)(id, SEL, double);
static BOOL (*original_cc_available)(id, SEL);

static void publish(float value)
{
    static int last = -1;
    static unsigned reports;
    if (!isfinite(value) || value < 0.0f || value > 1.0f) return;
    int level = (int)(fmaxf(0.02f, value) * 1000.0f);
    if (level == last) return;
    int error = sysctlbyname("kern.lisa_brightness", NULL, NULL, &level, sizeof(level));
    if (error == 0) last = level;
    if (reports++ < 24 || error != 0)
        dprintf(2, "LISA BRIGHTNESS: display level %d, result %d\n", level, error);
}

static void level_set(id self, SEL cmd, float level, id reason, uint64_t options)
{
    original_level(self, cmd, level, reason, options);
    publish(level);
}

static void transaction_set(id self, SEL cmd, float level, BOOL permanently)
{
    original_transaction(self, cmd, level, permanently);
    publish(level);
}

/* Control Center's DisplayModule uses a BrightnessSystemClient transaction,
 * separate from the two backboardd setters above. Preserve it, and forward the
 * user's actual level to Lisa's proven panel endpoint as well. */
static void cc_float(id self, SEL cmd, float level)
{
    original_cc_float(self, cmd, level);
    publish(level);
}

static void cc_double(id self, SEL cmd, double level)
{
    original_cc_double(self, cmd, level);
    publish((float)level);
}

static BOOL cc_available(id self, SEL cmd)
{
    if (original_cc_available(self, cmd)) return YES;
    int hardware = 0;
    size_t length = sizeof(hardware);
    return sysctlbyname("kern.lisa_hw_brightness", &hardware, &length, NULL, 0) == 0 && hardware == 1;
}

static bool signature(Method m, unsigned argc, const char *types)
{
    if (!m || method_getNumberOfArguments(m) != argc) return false;
    char type[32];
    method_getReturnType(m, type, sizeof(type));
    if (type[0] != 'v') return false;
    for (unsigned i = 2; i < argc; i++) {
        method_getArgumentType(m, i, type, sizeof(type));
        if (type[0] != types[i - 2]) return false;
    }
    return true;
}

static void install_hooks(void)
{
    if (!strcmp(getprogname(), "SpringBoard")) {
        Class cls = objc_getClass("CCUIDisplayModuleViewController");
        if (!cls) return; /* Its bundle loads when Control Center needs it. */
        Method m = class_getInstanceMethod(cls, sel_registerName("_setBacklightLevel:"));
        if (!original_cc_float && !original_cc_double) {
            if (signature(m, 3, "f"))
                original_cc_float = (void (*)(id,SEL,float))method_setImplementation(m, (IMP)cc_float);
            else if (signature(m, 3, "d"))
                original_cc_double = (void (*)(id,SEL,double))method_setImplementation(m, (IMP)cc_double);
            if (original_cc_float || original_cc_double)
                dprintf(2, "LISA BRIGHTNESS: Control Center backlight hook installed\n");
        }
        if (!original_cc_available) {
            m = class_getInstanceMethod(cls, sel_registerName("_canAdjustBrightness"));
            char type[16] = {0};
            if (m) method_getReturnType(m, type, sizeof(type));
            if (m && method_getNumberOfArguments(m) == 2 && (type[0] == 'B' || type[0] == 'c'))
                original_cc_available = (BOOL (*)(id,SEL))method_setImplementation(m, (IMP)cc_available);
        }
        return;
    }
    if (!original_level) {
        Method m = class_getInstanceMethod(objc_getClass("BKDisplayBrightnessController"),
            sel_registerName("setBrightnessLevel:reason:options:"));
        if (signature(m, 5, "f@Q")) {
            original_level = (level_imp)method_setImplementation(m, (IMP)level_set);
            dprintf(2, "LISA BRIGHTNESS: display controller hook installed\n");
        }
    }
    if (!original_transaction) {
        Method m = class_getInstanceMethod(objc_getClass("BKDisplayBrightnessUpdateTransactionManager"),
            sel_registerName("setDisplayBrightness:permanently:"));
        if (signature(m, 4, "fB")) {
            original_transaction = (transaction_imp)method_setImplementation(m, (IMP)transaction_set);
            dprintf(2, "LISA BRIGHTNESS: display transaction hook installed\n");
        }
    }
}

__attribute__((constructor)) static void install(void)
{
    if (strcmp(getprogname(), "backboardd") && strcmp(getprogname(), "SpringBoard")) return;
    install_hooks();
    if (!strcmp(getprogname(), "SpringBoard")) {
        [[NSNotificationCenter defaultCenter] addObserverForName:NSBundleDidLoadNotification
            object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *notification) {
                (void)notification;
                install_hooks();
            }];
        for (unsigned seconds = 1; seconds <= 180; seconds *= 3)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)seconds * NSEC_PER_SEC),
                dispatch_get_main_queue(), ^{ install_hooks(); });
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 6 * NSEC_PER_SEC),
        dispatch_get_main_queue(), ^{ install_hooks(); });
}
