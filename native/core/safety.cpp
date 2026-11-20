// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// SPEC 8: never inside anti-cheat games, games that foveate themselves, VRChat, or SteamVR's own processes.
#include <windows.h>
#include <sstream>
#include "fov_frame.h"

namespace fov {

namespace {
// exe names and Steam app ids listed in a profile file (`exe = "..."`, `steam_app_id = N` lines).
bool listed(const std::string& list, const std::string& exe, uint32_t app_id) {
    std::istringstream in(list);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string key = line.substr(0, line.find_first_of(" =")), value = line.substr(eq + 1);
        size_t a = value.find_first_not_of(" \t\""), b = value.find_last_not_of(" \t\r\"");
        value = a == std::string::npos ? "" : value.substr(a, b - a + 1);
        for (auto& c : value) c = (char)tolower((unsigned char)c);
        if (key == "exe" && value == exe) return true;
        if (key == "steam_app_id" && app_id && value == std::to_string(app_id)) return true;
    }
    return false;
}

bool any_file(const std::wstring& dir, const wchar_t* pattern) {
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}
}  // namespace

uint32_t safety_reason(const std::string& exe, uint32_t app_id, const std::wstring& exe_dir,
                       const std::string& anticheat_list, const std::string& native_list) {
    static const char* never[] = {"vrchat.exe",     "vrcompositor.exe", "vrserver.exe",  "vrmonitor.exe",
                                  "vrdashboard.exe", "vrwebhelper.exe",  "vrstartup.exe", "steam.exe",
                                  "steamwebhelper.exe", "foveaxr.exe"};
    for (auto n : never)
        if (exe == n) return FOV_REASON_NEVER;
    if (listed(anticheat_list, exe, app_id)) return FOV_REASON_ANTICHEAT_LIST;
    std::wstring dir = exe_dir;
    for (int up = 0; up < 3 && !dir.empty(); ++up) {
        for (auto p : {L"EasyAntiCheat*", L"start_protected_game.exe", L"BattlEye", L"BEService*", L"BEClient*"})
            if (any_file(dir, p)) return FOV_REASON_ANTICHEAT_FILES;
        size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos || slash < 3) break;  // stop at the drive root
        dir.resize(slash);
    }
    if (listed(native_list, exe, app_id)) return FOV_REASON_NATIVE_FOVEATION;
    return FOV_REASON_ACTIVE;
}

}  // namespace fov
