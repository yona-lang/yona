#include <windows.h>
#include <stdio.h>

#define N 20

static long results[N];

DWORD WINAPI worker(LPVOID arg) {
    long i = (long)(LONG_PTR)arg;
    results[i] = (i + 1) * (i + 1) * (i + 1);
    return 0;
}

int main(void) {
    HANDLE threads[N];
    for (long i = 0; i < N; i++) {
        threads[i] = CreateThread(NULL, 0, worker, (LPVOID)(LONG_PTR)i, 0, NULL);
    }
    WaitForMultipleObjects(N, threads, TRUE, INFINITE);
    for (int i = 0; i < N; i++) CloseHandle(threads[i]);

    printf("[");
    for (int i = 0; i < N; i++) {
        if (i > 0) printf(", ");
        printf("%ld", results[i]);
    }
    printf("]\n");
    return 0;
}
