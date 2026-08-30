/* Windows C reference: binary read/write/read-back */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char* out_path = "bench/data/c_bench_binary_wr.bin";
    FILE* f = fopen("bench/data/bench_binary.bin", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    rewind(f);
    char* buf = (char*)malloc(size);
    if (!buf) return 1;
    fread(buf, 1, size, f);
    fclose(f);

    FILE* wf = fopen(out_path, "wb");
    if (!wf) return 1;
    fwrite(buf, 1, size, wf);
    fclose(wf);
    free(buf);

    FILE* rf = fopen(out_path, "rb");
    if (!rf) return 1;
    fseek(rf, 0, SEEK_END);
    size_t size2 = (size_t)ftell(rf);
    fclose(rf);
    printf("%zu\n", size2);
    return 0;
}
