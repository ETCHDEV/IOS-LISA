/* The iOS16 Storage page force-unwraps an unavailable media-library path.
 * Only that capacity query may fall back to the existing Media directory;
 * other database consumers still receive the original answer. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
extern const void *_dyld_get_shared_cache_range(size_t *length);
static NSString *(*original)(id, SEL);
static uintptr_t slide;
static NSString *database_path(id self, SEL command)
{
    NSString *path = original(self, command);
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - slide;
    if (!path && caller >= 0x1cdc47c58ULL && caller < 0x1cdc47dacULL) {
        BOOL directory = NO;
        NSString *media = @"/var/mobile/Media";
        if ([[NSFileManager defaultManager] fileExistsAtPath:media isDirectory:&directory] && directory) {
            dprintf(2, "LISA STORAGE: media library unavailable; capacity query uses existing Media directory\n");
            return media;
        }
    }
    return path;
}
__attribute__((constructor)) static void install(void)
{
    if (strcmp(getprogname(), "Preferences")) return;
    /* Preserve exceptions, but capture the actual Wi-Fi page failure instead
     * of guessing from unsupported driver requests elsewhere in the log. */
    (void)dlopen("/usr/local/lib/lisa/liblisaexceptions.dylib", RTLD_NOW | RTLD_GLOBAL);
    size_t length = 0;
    const void *cache = _dyld_get_shared_cache_range(&length);
    if (!cache) return;
    slide = (uintptr_t)cache - 0x180000000ULL;
    Class cls = objc_getClass("MPMediaLibrary");
    Method method = class_getInstanceMethod(cls, sel_registerName("databasePath"));
    if (!method) { dprintf(2, "LISA STORAGE: databasePath unavailable\n"); return; }
    original = (NSString *(*)(id, SEL))method_setImplementation(method, (IMP)database_path);
    dprintf(2, "LISA STORAGE: installed scoped capacity-query guard\n");
}
