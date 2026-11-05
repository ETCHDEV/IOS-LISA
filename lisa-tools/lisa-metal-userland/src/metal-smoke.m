/* Real-device Metal test: ordered buffer blits and Apple's vertex/fragment AIR.
 * No screen presentation; pixel readback is required before reporting success. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/sysctl.h>
/* every line also straight into the kernel log (kern.lisa_say): v33/v34 lost everything after 5 s */
static void say_kernel(const char *text)
{
    (void)sysctlbyname("kern.lisa_say", NULL, NULL, (void *)text, strlen(text));
}
static int lisa_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
static int lisa_printf(const char *format, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    size_t len = strlen(line);
    if (len && line[len - 1] == '\n') line[len - 1] = 0;
    say_kernel(line);
    fputs(line, stdout); fputc('\n', stdout);
    return n;
}
static int lisa_puts(const char *text) { say_kernel(text); return puts(text); }
#define printf lisa_printf
#define NSLog(...) lisa_printf("%s", [[NSString stringWithFormat:__VA_ARGS__] UTF8String])
#define puts lisa_puts

/* Match this kernel's bsd/sys/kern_memorystatus.h. Only this test process is
 * changed; system-wide jetsam protection and all other daemon limits remain. */
static void prepare_memory_limit(void)
{
    struct { int32_t active; uint32_t active_attr; int32_t inactive; uint32_t inactive_attr; } limits = {0};
    int query = (int)syscall(SYS_memorystatus_control, 8, getpid(), 0, &limits, sizeof(limits));
    printf("LISA METAL TEST: memory limits query=%d active=%dMB inactive=%dMB\n", query, limits.active, limits.inactive);
    if (query != 0 || (limits.active > 0 && limits.active < 512) ||
        (limits.inactive > 0 && limits.inactive < 512)) {
        limits.active = 512; limits.inactive = 512;
        limits.active_attr = limits.inactive_attr = 1;
        int changed = (int)syscall(SYS_memorystatus_control, 7, getpid(), 0, &limits, sizeof(limits));
        printf("LISA METAL TEST: compiler memory allowance 512MB result=%d errno=%d\n", changed, changed ? errno : 0);
    }
    /* v33: killed silently (SIGKILL) while idle in its first sleep: out of jetsam's idle band.
     * MEMORYSTATUS_CMD_SET_PRIORITY_PROPERTIES (2), JETSAM_PRIORITY_FOREGROUND (10), this process only. */
    struct { int32_t priority; uint64_t user_data; } props = { 10, 0 };
    int raised = (int)syscall(SYS_memorystatus_control, 2, getpid(), 0, &props, sizeof(props));
    printf("LISA METAL TEST: jetsam priority foreground result=%d errno=%d\n", raised, raised ? errno : 0);
}

static BOOL completed(id<MTLCommandBuffer> command)
{
    [command commit];
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusCompleted) return YES;
    NSLog(@"LISA METAL TEST: command failed status=%lu error=%@", (unsigned long)command.status, command.error);
    return NO;
}

static volatile const char *stage = "start";
static void on_alarm(int sig)
{
    (void)sig;
    dprintf(1, "LISA METAL TEST: FAIL timed out after 90 s in stage: %s\n", stage);
    _exit(124);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    prepare_memory_limit();
    puts("LISA METAL TEST: launched; native device and GPU pixel checks begin in 25 seconds");
    /* Keep compilation off the critical SpringBoard startup path; say so every 5 s, so the log shows
     * whether the process lives through the wait (v19: nothing at all was said after it). */
    for (int waited = 0; waited < 25; waited += 5) {
        sleep(5);
        printf("LISA METAL TEST: alive after %d s\n", waited + 5);
    }
    signal(SIGALRM, on_alarm);
    alarm(90);
    @autoreleasepool {
        puts("LISA METAL TEST: opening native device");
        stage = "MTLCreateSystemDefaultDevice";
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        stage = "after device creation";
        printf("LISA METAL TEST: device %s\n", device ? device.name.UTF8String : "(none)");
        if (!device) { puts("LISA METAL TEST: FAIL no native device"); return 1; }
        NSLog(@"LISA METAL TEST: device=%@ class=%@", device.name, NSStringFromClass([device class]));
        if (![NSStringFromClass([device class]) isEqualToString:@"LisaMetalDevice"]) {
            puts("LISA METAL TEST: FAIL unexpected device class"); return 2;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        stage = "buffers and blits";
        id<MTLBuffer> source = [device newBufferWithLength:4096 options:0];
        id<MTLBuffer> destination = [device newBufferWithLength:4096 options:0];
        if (!queue || !source.contents || !destination.contents) {
            puts("LISA METAL TEST: FAIL buffer or queue allocation"); return 3;
        }
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!blit) { puts("LISA METAL TEST: FAIL no blit encoder"); return 4; }
        [blit fillBuffer:source range:NSMakeRange(0, 4096) value:0x5a];
        [blit fillBuffer:destination range:NSMakeRange(0, 4096) value:0x11];
        [blit copyFromBuffer:source sourceOffset:256 toBuffer:destination destinationOffset:512 size:1024];
        [blit endEncoding];
        [source release]; /* Recorded work must retain this through submission. */
        if (!completed(command)) return 5;
        const unsigned char *result = destination.contents;
        for (unsigned i = 0; i < 4096; i++) {
            unsigned char expected = i >= 512 && i < 1536 ? 0x5a : 0x11;
            if (result[i] != expected) {
                printf("LISA METAL TEST: FAIL blit byte %u: %02x expected %02x\n", i, result[i], expected);
                return 6;
            }
        }
        puts("LISA METAL TEST: PASS ordered buffer fill/copy, 4096 bytes verified");
        puts("LISA METAL TEST: compiling stock AIR vertex/fragment pipeline");
        NSError *error = nil;
        stage = "loading default.metallib (AIR)";
        id<MTLLibrary> library = [device newLibraryWithFile:@"/System/Library/Extensions/AppleMetalGLRenderer.bundle/default.metallib" error:&error];
        id<MTLFunction> vertex = [library newFunctionWithName:@"clearVertex"];
        id<MTLFunction> fragment = [library newFunctionWithName:@"clearFragment20"];
        if (!vertex || !fragment) { NSLog(@"LISA METAL TEST: FAIL stock shaders %@", error); return 7; }
        MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertex;
        pipelineDescriptor.fragmentFunction = fragment;
        id<MTLTexture> targets[3];
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Uint width:4 height:4 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        for (unsigned i = 0; i < 3; i++) {
            pipelineDescriptor.colorAttachments[i].pixelFormat = MTLPixelFormatRGBA32Uint;
            targets[i] = [device newTextureWithDescriptor:td];
            if (!targets[i]) return 8;
            pass.colorAttachments[i].texture = targets[i];
            pass.colorAttachments[i].loadAction = MTLLoadActionClear;
            /* Metal's default clear colour is (0,0,0,1): an integer target then holds alpha 1, and the
             * "not drawn" pixels below are compared with all-zero. Clear to all-zero explicitly. */
            pass.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 0);
            pass.colorAttachments[i].storeAction = MTLStoreActionStore;
        }
        stage = "compiling the render pipeline (AIR -> Mesa)";
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
        if (!pipeline) { NSLog(@"LISA METAL TEST: FAIL pipeline %@", error); return 9; }
        float quad[16] = {-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
        unsigned char uniform[256];
        for (unsigned i = 0; i < sizeof(uniform); i++) uniform[i] = (i * 7 + 13) & 255;
        id<MTLBuffer> positions = [device newBufferWithBytes:quad length:sizeof(quad) options:0];
        id<MTLBuffer> constants = [device newBufferWithBytes:uniform length:sizeof(uniform) options:0];
        if (!positions || !constants) return 10;
        command = [queue commandBuffer];
        id<MTLRenderCommandEncoder> render = [command renderCommandEncoderWithDescriptor:pass];
        if (!render) return 11;
        [render setRenderPipelineState:pipeline];
        [render setVertexBuffer:positions offset:0 atIndex:14];
        [render setFragmentBuffer:constants offset:0 atIndex:14];
        [render drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [render endEncoding];
        if (!completed(command)) return 12;
        uint32_t pixels[4 * 4 * 4] = {0}, expected[4];
        memcpy(expected, uniform + 16, sizeof(expected));
        [targets[2] getBytes:pixels bytesPerRow:4 * 16 fromRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0];
        for (unsigned i = 0; i < 16; i++) {
            if (memcmp(pixels + i * 4, expected, sizeof(expected))) {
                printf("LISA METAL TEST: FAIL AIR pixel %u: %08x %08x %08x %08x\n", i,
                    pixels[i*4], pixels[i*4+1], pixels[i*4+2], pixels[i*4+3]);
                return 13;
            }
        }
        puts("LISA METAL TEST: PASS native AIR vertex/fragment rendering, all 16 pixels verified");
        /* A clipped viewport pass, then a fresh default pass: tests both geometry
         * and that stale scissor state cannot leak into the next command buffer. */
        for (unsigned mode = 0; mode < 2; mode++) {
            command = [queue commandBuffer];
            render = [command renderCommandEncoderWithDescriptor:pass];
            [render setRenderPipelineState:pipeline];
            [render setVertexBuffer:positions offset:0 atIndex:14];
            [render setFragmentBuffer:constants offset:0 atIndex:14];
            if (mode == 0) {
                [render setViewport:(MTLViewport){1,1,2,2,0,1}];
                [render setScissorRect:(MTLScissorRect){1,1,1,2}];
            }
            [render drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [render endEncoding];
            if (!completed(command)) return 14;
            [targets[2] getBytes:pixels bytesPerRow:4*16 fromRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0];
            for (unsigned y = 0; y < 4; y++) for (unsigned x = 0; x < 4; x++) {
                BOOL drawn = mode || (x == 1 && y >= 1 && y < 3);
                uint32_t zero[4] = {0};
                if (memcmp(pixels + (y*4+x)*4, drawn ? expected : zero, sizeof(expected))) {
                    printf("LISA METAL TEST: FAIL viewport/scissor mode %u at %u,%u: %08x %08x %08x %08x (%s)\n", mode, x, y,
                        pixels[(y*4+x)*4], pixels[(y*4+x)*4+1], pixels[(y*4+x)*4+2], pixels[(y*4+x)*4+3], drawn ? "should be drawn" : "should be clear");
                    return 15;
                }
            }
        }
        puts("LISA METAL TEST: PASS viewport, scissor and default-state restoration");
        id<MTLTexture> copied = [device newTextureWithDescriptor:td];
        if (!copied) { puts("LISA METAL TEST: FAIL texture-copy allocation"); return 16; }
        memset(pixels, 0, sizeof(pixels));
        [copied replaceRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0 withBytes:pixels bytesPerRow:4*16];
        command = [queue commandBuffer];
        blit = [command blitCommandEncoder];
        [blit copyFromTexture:targets[2] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(1,1,0)
            sourceSize:MTLSizeMake(2,2,1) toTexture:copied destinationSlice:0 destinationLevel:0
            destinationOrigin:MTLOriginMake(0,1,0)];
        [blit endEncoding];
        if (!completed(command)) return 17;
        [copied getBytes:pixels bytesPerRow:4*16 fromRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0];
        for (unsigned y = 0; y < 4; y++) for (unsigned x = 0; x < 4; x++) {
            uint32_t zero[4] = {0};
            BOOL drawn = x < 2 && y >= 1 && y < 3;
            if (memcmp(pixels+(y*4+x)*4, drawn ? expected : zero, sizeof(expected))) {
                printf("LISA METAL TEST: FAIL texture copy at %u,%u\n", x, y); return 18;
            }
        }
        puts("LISA METAL TEST: PASS GPU texture-region copy, all 16 pixels verified");
        /* Only after real pixels match may a native-only wallpaper process try
         * this driver. Keep backboardd/SpringBoard on their GLES compositor. */
        NSString *selection = @"/usr/local/lib/lisa/native-metal-processes";
        NSString *names = [NSString stringWithContentsOfFile:selection encoding:NSUTF8StringEncoding error:NULL] ?: @"lisa-metal-smoke\n";
        if (![[names componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]] containsObject:@"AegirPoster"]) {
            if (![[names stringByAppendingString:@"\nAegirPoster\n"] writeToFile:selection atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
                NSLog(@"LISA METAL TEST: native wallpaper selection not saved: %@", error);
            } else puts("LISA METAL TEST: AegirPoster enabled for native Metal on its next launch");
        }
        alarm(0);
        return 0;
    }
}
