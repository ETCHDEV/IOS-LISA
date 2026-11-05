/*
 * Lisa (2026-11-03): Metal on Vulkan on the phone -- UniXNU's bridge (metal2vulkan + their patches)
 * over Turnip over LisaGPU. Their own self-test: a compute kernel, a render pass, a render into
 * imported pages, and (given QuartzCore's shader library) every CoreAnimation function translated.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include "NativeMetalBridge.h"
#include "NativeMetalObjects.h"

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
say(const char *fmt, ...)
{
	char line[300];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	(void)sysctlbyname("kern.lisa_say", NULL, NULL, line, strlen(line));
	printf("%s\n", line);
	fflush(stdout);
}

static void mark(uint32_t m) { say("M2V TEST: stage %#x", m); }

static uint8_t *
slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	struct stat st;
	if (!f || stat(path, &st) != 0) { if (f) fclose(f); return NULL; }
	uint8_t *b = malloc((size_t)st.st_size);
	*size = fread(b, 1, (size_t)st.st_size, f);
	fclose(f);
	return b;
}

int
main(void)
{
	sleep(20);
	/* stderr was the console, whose writes fail (EIO) for this job: Rust's eprintln! panicked on it */
	freopen("/private/var/tmp/lisa-m2v-stderr.log", "w", stderr);
	setvbuf(stderr, NULL, _IOLBF, 0);
	unlink("/private/var/tmp/lisa-turnip.log");
	unlink("/private/var/tmp/lisa-m2v-trace.log");
	setenv("MESA_LOG_FILE", "/private/var/tmp/lisa-turnip.log", 1);
	setenv("TU_DEBUG", "startup", 1);
	setenv("RUST_BACKTRACE", "1", 1);
	setenv("NMB_LIST", "1", 1);		/* the bridge names each QuartzCore function it fails to translate */
	setenv("MESA_VK_ABORT_ON_DEVICE_LOSS", "1", 1);
	{
		/* where Turnip is, to turn crash addresses into its functions (atos on the Mac) */
		void *h = dlopen("/usr/local/lib/lisa/libvulkan_freedreno.dylib", RTLD_NOW);
		void *f = h ? dlsym(h, "vk_icdGetInstanceProcAddr") : NULL;
		Dl_info info;
		if (f && dladdr(f, &info))
			say("M2V TEST: libvulkan_freedreno loaded at %p (vk_icdGetInstanceProcAddr %p)", info.dli_fbase, f);
		else
			say("M2V TEST: libvulkan_freedreno not found by dlopen");
	}
	say("M2V TEST: start");
	mkdir("/private/var/tmp/lisa-m2v-cache", 0777);
	size_t qcsize = 0;
	const char *qcpaths[] = { "/System/Library/Frameworks/QuartzCore.framework/default.metallib",
				  "/System/Library/Frameworks/QuartzCore.framework/QuartzCore.metallib", NULL };
	uint8_t *qc = NULL;
	for (int i = 0; qcpaths[i] && !qc; i++) {
		qc = slurp(qcpaths[i], &qcsize);
		if (qc) say("M2V TEST: QuartzCore library %s, %zu bytes", qcpaths[i], qcsize);
	}
	if (!qc) say("M2V TEST: no QuartzCore metallib file found (QuartzCore step skipped)");
	struct NmbSelftest out;
	memset(&out, 0, sizeof(out));
	uint32_t r = nmb_selftest("/usr/local/lib/lisa/liblisavk.dylib", "/private/var/tmp/lisa-m2v-cache", qc, qcsize, mark, &out);
	say("M2V TEST: result %#x stage %#x: compute %u/1000 wrong, render %u/64 wrong, import %u/4096 wrong", r, out.stage,
	    out.compute_bad, out.render_bad, out.import_bad);
	say("M2V TEST: QuartzCore %u functions, kernels %u ok %u failed, render pairs %u ok %u failed", out.qc_functions,
	    out.qc_kernels_ok, out.qc_kernels_failed, out.qc_pairs_ok, out.qc_pairs_failed);
	out.message[sizeof(out.message) - 1] = 0;
	say("M2V TEST: message '%s'", out.message);
	say(out.stage == 0 && out.compute_bad == 0 && out.render_bad == 0 && out.import_bad == 0
	    ? "M2V TEST: PASS -- Metal on Vulkan on the Adreno" : "M2V TEST: FAIL");
	{
		FILE *f = fopen("/private/var/tmp/lisa-turnip.log", "r");
		char line[280];
		while (f && fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\n")] = 0;
			if (line[0])
				say("TURNIP LOG: %s", line);
		}
		if (f)
			fclose(f);
		fflush(stderr);
		int n = 0;
		f = fopen("/private/var/tmp/lisa-m2v-stderr.log", "r");
		while (f && fgets(line, sizeof(line), f) && n++ < 60) {
			line[strcspn(line, "\n")] = 0;
			if (line[0] && strncmp(line, "fn ", 3) != 0)
				say("M2V STDERR: %s", line);
		}
		if (f)
			fclose(f);
		/* each failed translation whole, in pieces a say() can carry */
		static char whole[16384];
		f = fopen("/private/var/tmp/lisa-m2v-stderr.log", "r");
		while (f && fgets(whole, sizeof(whole), f)) {
			if (strncmp(whole, "kernel ", 7) != 0 && strncmp(whole, "pair ", 5) != 0)
				continue;
			whole[strcspn(whole, "\n")] = 0;
			for (size_t at = 0, len = strlen(whole); at < len; at += 240)
				say("M2V FAILED %s %.240s", at ? "+" : ":", whole + at);
		}
		if (f)
			fclose(f);
		n = 0;
		f = fopen("/private/var/tmp/lisa-m2v-trace.log", "r");
		while (f && fgets(line, sizeof(line), f) && n++ < 120) {
			line[strcspn(line, "\n")] = 0;
			/* frames of our own code only: the names that say where */
			if (line[0] && (strstr(line, "failed") || strstr(line, "metal2vulkan") || strstr(line, "adreno_metal")))
				say("M2V TRACE: %s", line);
		}
		if (f)
			fclose(f);
		char w[400] = "";
		nmb_last_warning(w, sizeof(w));
		say("M2V TEST: last warning '%s'; last error '%s'", w, nmb_last_error() ? nmb_last_error() : "");
	}
	for (;;)
		pause();
}
