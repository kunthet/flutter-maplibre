#include "crash_logger.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <mutex>

#pragma comment(lib, "dbghelp.lib")

namespace maplibre_windows {
namespace {

std::once_flag g_install_once;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;

void WriteFrame(FILE* f, int index, DWORD64 address) {
    HMODULE module = nullptr;
    char module_path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address),
                           &module) &&
        module) {
        GetModuleFileNameA(module, module_path, MAX_PATH);
    }
    const DWORD64 base = reinterpret_cast<DWORD64>(module);
    const DWORD64 offset = base ? (address - base) : 0;

    // Try to resolve a symbol name (works for exported functions or when a PDB
    // is present next to the module); otherwise module+offset is still useful.
    char symbol_buffer[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 sym_disp = 0;
    const char* module_name = strrchr(module_path, '\\');
    module_name = module_name ? module_name + 1 : module_path;

    if (SymFromAddr(GetCurrentProcess(), address, &sym_disp, symbol)) {
        std::fprintf(f, "#%-2d 0x%016llx  %s+0x%llx  [%s+0x%llx]\n", index, address, symbol->Name, sym_disp,
                     module_name, offset);
    } else {
        std::fprintf(f, "#%-2d 0x%016llx  [%s+0x%llx]\n", index, address, module_name, offset);
    }
}

LONG WINAPI HandleException(EXCEPTION_POINTERS* info) {
    char log_path[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("TEMP", log_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        strcpy_s(log_path, "C:\\maplibre_crash.log");
    } else {
        strcat_s(log_path, "\\maplibre_crash.log");
    }

    FILE* f = nullptr;
    if (fopen_s(&f, log_path, "w") != 0 || !f) {
        return g_previous_filter ? g_previous_filter(info) : EXCEPTION_CONTINUE_SEARCH;
    }

    const auto* record = info->ExceptionRecord;
    std::fprintf(f, "=== maplibre crash ===\n");
    std::fprintf(f, "code=0x%08lx address=0x%016llx\n", record->ExceptionCode,
                 reinterpret_cast<DWORD64>(record->ExceptionAddress));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        std::fprintf(f, "access-violation %s addr 0x%016llx\n",
                     record->ExceptionInformation[0] == 0 ? "reading" : "writing",
                     static_cast<DWORD64>(record->ExceptionInformation[1]));
    }

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    std::fprintf(f, "--- faulting thread stack ---\n");
    for (int i = 0; i < 48; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }
        WriteFrame(f, i, frame.AddrPC.Offset);
    }

    std::fflush(f);
    std::fclose(f);
    SymCleanup(process);

    return g_previous_filter ? g_previous_filter(info) : EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void InstallCrashLogger() {
    std::call_once(g_install_once, [] { g_previous_filter = SetUnhandledExceptionFilter(HandleException); });
}

}  // namespace maplibre_windows
