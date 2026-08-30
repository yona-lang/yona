#include <windows.h>
#include <stdio.h>

typedef struct {
    const char* path;
    long len;
} read_arg_t;

DWORD WINAPI read_file_len(LPVOID p) {
    read_arg_t* arg = (read_arg_t*)p;
    FILE* f = fopen(arg->path, "rb");
    if (!f) {
        arg->len = 0;
        return 0;
    }
    fseek(f, 0, SEEK_END);
    arg->len = ftell(f);
    fclose(f);
    return 0;
}

int main(void) {
    read_arg_t args[3] = {
        {"bench/data/bench_text.txt", 0},
        {"bench/data/bench_text.txt", 0},
        {"bench/data/bench_text.txt", 0},
    };
    HANDLE t[3];
    for (int i = 0; i < 3; i++) t[i] = CreateThread(NULL, 0, read_file_len, &args[i], 0, NULL);
    WaitForMultipleObjects(3, t, TRUE, INFINITE);
    for (int i = 0; i < 3; i++) CloseHandle(t[i]);

    long total = args[0].len + args[1].len + args[2].len;
    printf("%ld\n", total);
    return 0;
}
