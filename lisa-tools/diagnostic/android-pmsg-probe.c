/* Android userspace persistence probe; does not access device registers. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

typedef ssize_t (*write_fn)(int, char, const char *, const char *, size_t);

int main(void)
{
    const char msg[] = "LISA_FORMATTED_PMSG_PROBE_20261021\n";
    void *lib = dlopen("liblog.so", RTLD_NOW);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    write_fn write_pmsg = (write_fn)dlsym(lib, "__android_log_pmsg_file_write");
    if (!write_pmsg) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }
    ssize_t n = write_pmsg(3, 4, "/lisa/pmsg-probe", msg, strlen(msg));
    printf("pmsg writer returned %zd\n", n);
    return n < 0 ? 1 : 0;
}
