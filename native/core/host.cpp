// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// Where the core finds the app's files. On Windows: the user's AppData folders. Inside Wine/Proton the game sees a
// Windows profile of its own prefix, but the app runs natively on Linux, so the shared files live in the Linux
// home's XDG folders, reached through Wine's Z: drive (the Linux root).
#include <windows.h>
#include "fov_frame.h"

namespace {
std::wstring env(const wchar_t* name) {
    std::wstring v(GetEnvironmentVariableW(name, nullptr, 0), L'\0');
    if (!v.empty()) v.resize(GetEnvironmentVariableW(name, v.data(), (DWORD)v.size()));
    return v;
}

std::wstring backslashes(std::wstring p) {
    for (auto& c : p)
        if (c == L'/') c = L'\\';
    return p;
}

// $XDG_<kind>_HOME, else ~/<fallback>, as a Z: path; empty if the Linux home is unknown.
std::wstring xdg(const wchar_t* var, const wchar_t* fallback) {
    std::wstring dir = env(var);
    if (!dir.empty()) return L"Z:" + backslashes(dir);
    std::wstring home = env(L"HOME");
    if (!home.empty()) return L"Z:" + backslashes(home + L"/" + fallback);
    // Wine's own note of the Unix home, as \??\Z:\home\user.
    home = env(L"WINEHOMEDIR");
    if (home.rfind(L"\\??\\", 0) == 0) home = home.substr(4);
    return home.empty() ? home : home + L"\\" + backslashes(fallback);
}

std::wstring under(const std::wstring& base, const wchar_t* leaf) { return base.empty() ? base : base + leaf; }
}  // namespace

extern "C" int fov_is_wine(void) {
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}

namespace fov {

std::wstring host_dir(Host kind) {
    if (fov_is_wine()) {
        switch (kind) {
        case Host::Config: return under(xdg(L"XDG_CONFIG_HOME", L".config"), L"\\foveaxr");
        case Host::State: return under(xdg(L"XDG_STATE_HOME", L".local/state"), L"\\foveaxr");
        case Host::Install: return under(xdg(L"XDG_DATA_HOME", L".local/share"), L"\\foveaxr");
        }
    }
    switch (kind) {
    case Host::Config: return under(env(L"APPDATA"), L"\\FoveaXR");
    case Host::State: return under(env(L"LOCALAPPDATA"), L"\\FoveaXR");
    case Host::Install: return under(env(L"LOCALAPPDATA"), L"\\Programs\\FoveaXR");
    }
    return L"";
}

}  // namespace fov
