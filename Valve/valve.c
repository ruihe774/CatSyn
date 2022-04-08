#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

_Noreturn static void panic(const char* msg) {
    fputs(msg, stderr);
    fflush(stderr);
    abort();
}

#define BUF_SIZE 0x2000000

int main() {
    wchar_t* cmdline = GetCommandLineW();
    int bcount = 0;
    BOOL in_quotes = FALSE;
    while (*cmdline) {
        if ((*cmdline == '\t' || *cmdline == ' ') && !in_quotes)
            break;
        else if (*cmdline == '\\')
            ++bcount;
        else if (*cmdline == '\"') {
            if (!(bcount & 1))
                in_quotes = !in_quotes;
            bcount = 0;
        } else
            bcount = 0;
        ++cmdline;
    }
    while (*cmdline == '\t' || *cmdline == ' ')
        ++cmdline;

    wchar_t* sep = wcsstr(cmdline, L" --- ");
    if (sep == NULL)
        panic("no '---' found in commandline\n");
    *sep = '\0';

    HANDLE pipe1_r, pipe1_w, pipe2_r, pipe2_w;
    if (!CreatePipe(&pipe1_r, &pipe1_w, NULL, BUF_SIZE) || !CreatePipe(&pipe2_r, &pipe2_w, NULL, 0))
        panic("failed to create pipe\n");

    SetHandleInformation(pipe1_w, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    STARTUPINFOW si1 = {.cb = sizeof(STARTUPINFOW),
                        .dwFlags = STARTF_USESTDHANDLES,
                        .hStdInput = GetStdHandle(STD_INPUT_HANDLE),
                        .hStdOutput = pipe1_w,
                        .hStdError = GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION pi1;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si1, &pi1))
        panic("failed to spawn process 1\n");
    CloseHandle(pipe1_w);

    SetHandleInformation(pipe2_r, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    STARTUPINFOW si2 = {.cb = sizeof(STARTUPINFOW),
                        .dwFlags = STARTF_USESTDHANDLES,
                        .hStdInput = pipe2_r,
                        .hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE),
                        .hStdError = GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION pi2;
    if (!CreateProcessW(NULL, sep + 5, NULL, NULL, TRUE, 0, NULL, NULL, &si2, &pi2))
        panic("failed to spawn process 2\n");
    CloseHandle(pipe2_r);

    char buf[BUF_SIZE];
    for (DWORD len; ReadFile(pipe1_r, buf, BUF_SIZE, &len, NULL) && len && WriteFile(pipe2_w, buf, len, NULL, NULL);)
        ;
    CloseHandle(pipe1_r);
    CloseHandle(pipe2_w);

    HANDLE processes[] = {pi1.hProcess, pi2.hProcess};
    if (WaitForMultipleObjects(2, processes, TRUE, INFINITE) == WAIT_FAILED)
        panic("failed to wait\n");

    DWORD ec1, ec2;
    if (!GetExitCodeProcess(pi1.hProcess, &ec1) || !GetExitCodeProcess(pi2.hProcess, &ec2))
        panic("failed to get exit code\n");

    if (ec1 != 0 || ec2 != 0)
        panic("subprocess failed\n");

    return 0;
}
