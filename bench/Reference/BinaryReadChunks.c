/* C reference: binary chunked read via POSIX API */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fd = open("bench/data/bench_binary.bin", O_RDONLY);
    if (fd < 0) return 1;
    char buf[65536];
    long total = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        total += n;
    close(fd);
    printf("%ld\n", total);
    return 0;
}
