#include "fov_core.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>
#include <string>

namespace {
constexpr long kMaxBytes = 1 << 20;

std::wstring exe_stem() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    s = s.substr(s.find_last_of(L"\\/") + 1);
    return s.substr(0, s.find_last_of(L'.'));
}

std::wstring env(const wchar_t* name) {
    std::wstring v(GetEnvironmentVariableW(name, nullptr, 0), L'\0');
    if (!v.empty()) v.resize(GetEnvironmentVariableW(name, v.data(), (DWORD)v.size()));
    return v;
}

FILE* open_log() {
    std::wstring path = env(L"FOV_LOG_FILE");
    if (path.empty()) {
        std::wstring dir = env(L"LOCALAPPDATA");
        if (dir.empty()) return nullptr;
        dir += L"\\FoveatedVR";
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t date[16];
        swprintf(date, 16, L"-%04u%02u%02u.log", t.wYear, t.wMonth, t.wDay);
        path = dir + L"\\" + exe_stem() + date;
    }
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > kMaxBytes))
        MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    // Shared so a test can read the file while the game still writes it.
    return _wfsopen(path.c_str(), L"a", _SH_DENYNO);
}
}  // namespace

extern "C" const char* fov_version(void) { return FOV_VERSION; }

extern "C" void fov_log(const char* fmt, ...) {
    static std::mutex mutex;
    static FILE* file = open_log();
    if (!file) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard lock(mutex);
    // Flushed per line: the last line before a crash must survive.
    fprintf(file, "%02u:%02u:%02u.%03u [%lu] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            GetCurrentProcessId(), buf);
    fflush(file);
}
