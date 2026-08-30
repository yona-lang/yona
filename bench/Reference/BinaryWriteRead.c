/* C reference: binary file read + write + read-back via POSIX API */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void) {
    int fd = open("bench/data/bench_binary.bin", O_RDONLY);
    if (fd < 0) return 1;
    struct stat st;
    fstat(fd, &st);
    size_t size = (size_t)st.st_size;
    char* buf = malloc(size);
    read(fd, buf, size);
    close(fd);

    int wfd = open("/tmp/c_bench_binary_wr.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(wfd, buf, size);
    close(wfd);
    free(buf);

    int rfd = open("/tmp/c_bench_binary_wr.bin", O_RDONLY);
    fstat(rfd, &st);
    size_t size2 = (size_t)st.st_size;
    char* buf2 = malloc(size2);
    read(rfd, buf2, size2);
    close(rfd);

    printf("%zu\n", size2);
    free(buf2);
    return 0;
}
