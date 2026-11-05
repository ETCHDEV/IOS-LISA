/* Read one sector using Android's own UFS stack, with direct-I/O alignment.
 * No data is printed and no device state is written. Run as rooted Android.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    void *buffer = NULL;
    if (posix_memalign(&buffer, 4096, 4096) != 0) {
        fprintf(stderr, "aligned allocation failed\n");
        return 1;
    }
    int fd = open("/dev/block/sda", O_RDONLY | O_DIRECT | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        free(buffer);
        return 1;
    }
    ssize_t n = pread(fd, buffer, 4096, 0);
    if (n != 4096) {
        if (n < 0) perror("pread");
        else fprintf(stderr, "short read: %zd\n", n);
    }
    close(fd);
    free(buffer);
    if (n != 4096) return 1;
    puts("direct read completed: 4096 bytes");
    return 0;
}
