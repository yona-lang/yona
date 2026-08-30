#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char* cmd;
    int len;
} exec_arg_t;

DWORD WINAPI exec_len_thread(LPVOID p) {
    exec_arg_t* arg = (exec_arg_t*)p;
    FILE* f = _popen(arg->cmd, "r");
    if (!f) {
        arg->len = 0;
        return 0;
    }
    char buf[256];
    int total = 0;
    while (fgets(buf, sizeof(buf), f)) {
        int n = (int)strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
        total += n;
    }
    _pclose(f);
    arg->len = total;
    return 0;
}

int main(void) {
    exec_arg_t args[3] = {
        {"cmd /c echo hello", 0},
        {"cmd /c echo world", 0},
        {"cmd /c echo yona", 0},
    };
    HANDLE t[3];
    for (int i = 0; i < 3; i++) t[i] = CreateThread(NULL, 0, exec_len_thread, &args[i], 0, NULL);
    WaitForMultipleObjects(3, t, TRUE, INFINITE);
    for (int i = 0; i < 3; i++) CloseHandle(t[i]);

    int total = args[0].len + args[1].len + args[2].len;
    printf("%d\n", total);
    return 0;
}
