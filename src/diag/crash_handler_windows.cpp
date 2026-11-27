// Crash handler for Windows: an unhandled-exception filter that writes the exception code and
// address, the module and offset of the faulting instruction and the call stack as module offsets,
// plus a minidump (dbghelp, a system library) for WinDbg and Visual Studio. The stack is not
// symbolised (no PDBs ship); resolve the offsets against the build named in the header.
#include "crash_internal.h"

#include "qraw/diag/crash_handler.h"
#include "qraw/diag/logging.h"

#include <QDir>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

namespace qraw::diag {

namespace {

struct State {
    bool installed = false;
    std::wstring directory;
    std::string header;
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    std::terminate_handler previousTerminate = nullptr;
};

State& state()
{
    static State instance;
    return instance;
}

std::atomic<bool> reporting{false};

std::wstring reportBaseName()
{
    SYSTEMTIME now;
    GetSystemTime(&now);
    wchar_t name[96];
    std::swprintf(name, sizeof name / sizeof name[0], L"crash-%04u%02u%02uT%02u%02u%02uZ-%lu", now.wYear, now.wMonth,
                  now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    return name;
}

void writeText(HANDLE file, const std::string& text)
{
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// "qraw.exe+0x1a2b3c" for an address inside a loaded module, the raw address otherwise.
std::string describeAddress(const void* address)
{
    char text[MAX_PATH + 48];
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module)
        && module != nullptr) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = std::strrchr(path, '\\');
        std::snprintf(text, sizeof text, "%s+0x%llx", name != nullptr ? name + 1 : path,
                      static_cast<unsigned long long>(static_cast<const char*>(address)
                                                      - reinterpret_cast<const char*>(module)));
    } else {
        std::snprintf(text, sizeof text, "0x%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)));
    }
    return text;
}

void writeReport(const std::string& reason, EXCEPTION_POINTERS* pointers)
{
    State& s = state();
    const std::wstring base = s.directory + L"\\" + reportBaseName();
    HANDLE file = CreateFileW((base + L".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        writeText(file, s.header);
        writeText(file, "Reason: " + reason + "\n");
        if (pointers != nullptr && pointers->ExceptionRecord != nullptr) {
            char line[128];
            std::snprintf(line, sizeof line, "Exception code: 0x%08lx\n",
                          static_cast<unsigned long>(pointers->ExceptionRecord->ExceptionCode));
            writeText(file, line);
            writeText(file, "Exception address: " + describeAddress(pointers->ExceptionRecord->ExceptionAddress) + "\n");
        }
        // The stack of the filter, which sits on top of the faulting frames; best effort.
        void* frames[62];
        const USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);
        writeText(file, "Call stack (innermost frame first, module+offset):\n");
        for (USHORT i = 0; i < count; ++i)
            writeText(file, "  " + describeAddress(frames[i]) + "\n");
        CloseHandle(file);
    }
    if (pointers != nullptr) {
        HANDLE dump = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info;
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = pointers;
            info.ClientPointers = FALSE;
            // Stacks and module lists only, no heap: the dump holds no document content.
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal, &info, nullptr, nullptr);
            CloseHandle(dump);
        }
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers)
{
    if (!reporting.exchange(true))
        writeReport("unhandled exception", pointers);
    State& s = state();
    // Windows Error Reporting and any debugger still see the exception.
    return s.previousFilter != nullptr ? s.previousFilter(pointers) : EXCEPTION_CONTINUE_SEARCH;
}

void onTerminate()
{
    if (!reporting.exchange(true))
        writeReport(detail::describeCurrentException(), nullptr);
    State& s = state();
    if (s.previousTerminate != nullptr && s.previousTerminate != &onTerminate)
        s.previousTerminate();
    std::abort();
}

} // namespace

bool installCrashHandler(const CrashHandlerOptions& options)
{
    State& s = state();
    if (s.installed)
        return true;
    if (!QDir().mkpath(options.directory))
        return false;
    s.directory = QDir::toNativeSeparators(options.directory).toStdWString();
    s.header = crashReportHeader(options).toUtf8().toStdString();
    s.previousFilter = SetUnhandledExceptionFilter(&onUnhandledException);
    s.previousTerminate = std::set_terminate(&onTerminate);
    s.installed = true;
    qCDebug(lcAppCrash) << "crash reports go to" << options.directory;
    return true;
}

void uninstallCrashHandler()
{
    State& s = state();
    if (!s.installed)
        return;
    SetUnhandledExceptionFilter(s.previousFilter);
    std::set_terminate(s.previousTerminate);
    s.installed = false;
    reporting = false;
}

} // namespace qraw::diag
