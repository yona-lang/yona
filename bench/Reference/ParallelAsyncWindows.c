#include <windows.h>
#include <stdio.h>

DWORD WINAPI slow_id(LPVOID arg) {
    (void)arg;
    Sleep(100);
    return 100;
}

int main(void) {
    HANDLE threads[4];
    for (int i = 0; i < 4; i++) {
        threads[i] = CreateThread(NULL, 0, slow_id, NULL, 0, NULL);
    }
    WaitForMultipleObjects(4, threads, TRUE, INFINITE);
    long total = 0;
    for (int i = 0; i < 4; i++) {
        DWORD v = 0;
        GetExitCodeThread(threads[i], &v);
        total += (long)v;
        CloseHandle(threads[i]);
    }
    printf("%ld\n", total);
    return 0;
}
