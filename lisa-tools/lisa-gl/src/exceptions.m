/* Targeted exception evidence for PosterBoard/assetsd. Preserve runtime behaviour:
 * report throws and chain any pre-existing Objective-C exception preprocessor. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ucontext.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>
extern const void *_dyld_get_shared_cache_range(size_t *length);

typedef id (*ExceptionPreprocessor)(id);
extern ExceptionPreprocessor objc_setExceptionPreprocessor(ExceptionPreprocessor);
static ExceptionPreprocessor previous;
static __thread BOOL reporting;
static unsigned reports;

static id observe_exception(id exception)
{
    if (!reporting && __sync_fetch_and_add(&reports, 1) < 16) {
        reporting = YES;
        @try {
            const char *name = [[exception name] UTF8String];
            const char *reason = [[exception reason] UTF8String];
            dprintf(2, "LISA EXCEPTION %s: %s: %.1200s\n", getprogname(),
                name ? name : "(unnamed)", reason ? reason : "(no reason)");
        } @catch (id ignored) { (void)ignored; }
        reporting = NO;
    }
    return previous ? previous(exception) : exception;
}

/*
 * Crashes that are not Objective-C exceptions (Swift traps = SIGTRAP, faults = SIGSEGV/SIGBUS,
 * abort = SIGABRT): the signal, the faulting address, pc/lr and a backtrace, each frame as
 * "image+offset" and, inside the shared cache, as the unslid cache address (0x18xxxxxxx) for
 * `ipsw dyld a2s`. Then the default action, so the crash itself is unchanged.
 */
static uintptr_t cache_slide;
static char alt_stack[64 * 1024];

static void say_frame(unsigned i, uintptr_t pc)
{
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void *)pc, &info) && info.dli_fname) {
        const char *image = strrchr(info.dli_fname, '/');
        dprintf(2, "LISA CRASH %s:  #%u %#lx cache %#lx %s+%#lx %s\n", getprogname(), i, (unsigned long)pc,
            (unsigned long)(pc - cache_slide), image ? image + 1 : info.dli_fname,
            (unsigned long)(pc - (uintptr_t)info.dli_fbase), info.dli_sname ? info.dli_sname : "?");
    } else {
        dprintf(2, "LISA CRASH %s:  #%u %#lx cache %#lx\n", getprogname(), i, (unsigned long)pc,
            (unsigned long)(pc - cache_slide));
    }
}

static void on_crash(int sig, siginfo_t *info, void *context)
{
    static volatile int inside;
    if (!__sync_lock_test_and_set(&inside, 1)) {
        ucontext_t *uc = (ucontext_t *)context;
        uintptr_t pc = 0, lr = 0, fp = 0;
        if (uc && uc->uc_mcontext) {
            pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
            lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
            fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
        }
        dprintf(2, "LISA CRASH %s: signal %d (%s) code %d at address %p, pc %#lx lr %#lx\n", getprogname(), sig,
            strsignal(sig), info ? info->si_code : 0, info ? info->si_addr : NULL, (unsigned long)pc, (unsigned long)lr);
        say_frame(0, pc);
        say_frame(1, lr);
        /* the faulting thread's own frames, by its frame pointers (backtrace() would show the handler's) */
        for (unsigned i = 2; i < 40 && fp != 0 && (fp & 7) == 0; i++) {
            uintptr_t *frame = (uintptr_t *)fp;
            uintptr_t ret = frame[1];
            if (ret == 0) break;
            say_frame(i, ret & 0x0000000fffffffffULL);
            if (frame[0] <= fp) break;
            fp = frame[0];
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/*
 * Hangs: the watchdog kills an app whose main thread stops answering (0x8badf00d, Settings' Wi-Fi
 * page, v36: 13 times) with SIGKILL, which no handler sees. A thread of ours pings the main queue
 * every 500 ms; when it has not answered for 2.5 s, the main thread's registers and frames are
 * sampled (suspended for the moment it takes) and logged, also into the kernel log (kern.lisa_say).
 */
static mach_port_t main_thread_port;
static volatile uint64_t main_beats, main_answered;

static void say_line(const char *line)
{
    dprintf(2, "%s\n", line);
    (void)sysctlbyname("kern.lisa_say", NULL, NULL, (void *)line, strlen(line));
}

static void hang_frame(unsigned i, uintptr_t pc)
{
    char line[256];
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void *)pc, &info) && info.dli_fname) {
        const char *image = strrchr(info.dli_fname, '/');
        snprintf(line, sizeof(line), "LISA HANG %s:  #%u cache %#lx %s+%#lx %s", getprogname(), i,
            (unsigned long)(pc - cache_slide), image ? image + 1 : info.dli_fname,
            (unsigned long)(pc - (uintptr_t)info.dli_fbase), info.dli_sname ? info.dli_sname : "?");
    } else {
        snprintf(line, sizeof(line), "LISA HANG %s:  #%u %#lx", getprogname(), i, (unsigned long)pc);
    }
    say_line(line);
}

static void sample_main(unsigned seconds)
{
    arm_thread_state64_t state;
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    uintptr_t frames[48];
    unsigned n = 0;
    char line[160];

    if (thread_suspend(main_thread_port) != KERN_SUCCESS) return;
    if (thread_get_state(main_thread_port, ARM_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS) {
        frames[n++] = (uintptr_t)__darwin_arm_thread_state64_get_pc(state);
        frames[n++] = (uintptr_t)__darwin_arm_thread_state64_get_lr(state);
        uintptr_t fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(state);
        while (n < 48 && fp != 0 && (fp & 7) == 0) {
            uintptr_t *frame = (uintptr_t *)fp;
            uintptr_t ret = frame[1] & 0x0000000fffffffffULL;
            if (ret == 0) break;
            frames[n++] = ret;
            if (frame[0] <= fp) break;
            fp = frame[0];
        }
    }
    thread_resume(main_thread_port);
    snprintf(line, sizeof(line), "LISA HANG %s: main thread has not answered for %u s; its stack:", getprogname(), seconds);
    say_line(line);
    for (unsigned i = 0; i < n; i++) hang_frame(i, frames[i]);
}

static void *hang_watch(void *unused)
{
    (void)unused;
    unsigned stale = 0, reported = 0;
    for (;;) {
        usleep(500000);
        uint64_t asked = __sync_add_and_fetch(&main_beats, 1);
        dispatch_async(dispatch_get_main_queue(), ^{ main_answered = asked; });
        if (main_answered + 1 < asked) {
            stale++;
            if (stale == 5 || stale == 16) {           /* 2.5 s, and again at 8 s */
                if (reported++ < 6) sample_main(stale / 2);
            }
        } else {
            stale = 0;
        }
    }
    return NULL;
}

static void install_hang_watch(void)
{
    if (strcmp(getprogname(), "Preferences") != 0 && strcmp(getprogname(), "SpringBoard") != 0) return;
    if (!pthread_main_np()) return;
    main_thread_port = mach_thread_self();
    pthread_t thread;
    pthread_create(&thread, NULL, hang_watch, NULL);
    pthread_detach(thread);
}

static void install_crash_handler(void)
{
    size_t length = 0;
    const void *cache = _dyld_get_shared_cache_range(&length);
    cache_slide = cache ? (uintptr_t)cache - 0x180000000ULL : 0;
    stack_t ss = { .ss_sp = alt_stack, .ss_size = sizeof(alt_stack), .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        sigaction(sigs[i], &sa, NULL);
    }
}

__attribute__((constructor)) static void install_observer(void)
{
    install_crash_handler();
    install_hang_watch();
    previous = objc_setExceptionPreprocessor(observe_exception);
    dprintf(2, "LISA EXCEPTION observer active in %s\n", getprogname());
}
