// Minimal debug-loop runner: launches a target exe under the debugger,
// reports the first-chance/second-chance exception address and which
// loaded module (+offset) it falls into, then lets the process die.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

struct Mod { ULONGLONG base; std::string name; };

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: crashwhere <exe> [args]\n"); return 1; }
    std::string cmd;
    for (int i = 1; i < argc; i++) { if (i > 1) cmd += " "; cmd += argv[i]; }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, (LPSTR)cmd.c_str(), nullptr, nullptr, FALSE,
                        DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &si, &pi)) {
        printf("CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }

    std::vector<Mod> mods;
    char namebuf[MAX_PATH];
    DEBUG_EVENT ev;
    bool first_bp_seen = false;
    while (WaitForDebugEvent(&ev, 60000)) {
        DWORD cont = DBG_CONTINUE;
        if (ev.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            if (GetFinalPathNameByHandleA(ev.u.CreateProcessInfo.hFile, namebuf, MAX_PATH, 0))
                mods.push_back({ (ULONGLONG)ev.u.CreateProcessInfo.lpBaseOfImage, namebuf });
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
        } else if (ev.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            if (ev.u.LoadDll.hFile &&
                GetFinalPathNameByHandleA(ev.u.LoadDll.hFile, namebuf, MAX_PATH, 0))
                mods.push_back({ (ULONGLONG)ev.u.LoadDll.lpBaseOfDll, namebuf });
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
        } else if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            auto& er = ev.u.Exception.ExceptionRecord;
            if (er.ExceptionCode == EXCEPTION_BREAKPOINT && !first_bp_seen) {
                first_bp_seen = true;  // initial loader breakpoint
            } else if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
                ULONGLONG ip = (ULONGLONG)er.ExceptionAddress;
                ULONGLONG accessed = (ULONGLONG)er.ExceptionInformation[1];
                const char* kind = er.ExceptionInformation[0] == 0 ? "read"
                                 : er.ExceptionInformation[0] == 1 ? "write" : "exec";
                Mod* best = nullptr;
                for (auto& m : mods)
                    if (m.base <= ip && (!best || m.base > best->base)) best = &m;
                printf("ACCESS_VIOLATION (%s of 0x%llx) at 0x%llx", kind, accessed, ip);
                if (best) printf("  = %s + 0x%llx", best->name.c_str(), ip - best->base);
                printf("\n");
                fflush(stdout);
                cont = DBG_EXCEPTION_NOT_HANDLED;
            } else {
                cont = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            printf("process exited rc=%lu\n", ev.u.ExitProcess.dwExitCode);
            break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
