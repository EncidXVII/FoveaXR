// settings.toml (written by the tray app) and the profile lists: a few flat `key = value` lines, so a line reader
// does instead of a TOML library.
#include <windows.h>
#include <share.h>
#include <cstring>
#include <sstream>
#include "fov_frame.h"

namespace fov {

namespace {
std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\""), b = s.find_last_not_of(" \t\r\"");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Quoted strings of a one-line array: ["a.exe", "b.exe"].
std::vector<std::string> strings(const std::string& v) {
    std::vector<std::string> out;
    for (size_t a = v.find('"'); a != std::string::npos; a = v.find('"', a + 1)) {
        size_t b = v.find('"', a + 1);
        if (b == std::string::npos) break;
        out.push_back(v.substr(a + 1, b - a - 1));
        a = b;
    }
    return out;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
}  // namespace

void apply_settings_toml(const std::string& text, FovSettings& s, std::vector<std::string>* off_games) {
    std::istringstream in(text);
    std::string line, enabled, mode, preset, tint;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = line.substr(eq + 1);
        if (key == "enabled") enabled = trim(value);
        else if (key == "mode") mode = trim(value);
        else if (key == "preset") preset = trim(value);
        else if (key == "debug_tint") tint = trim(value);
        else if (key == "off_games" && off_games)
            for (auto& g : strings(value)) off_games->push_back(lower(g));
    }
    if (!preset.empty())
        fov_settings_default(&s, preset == "low" ? FOV_PRESET_LOW : preset == "high" ? FOV_PRESET_HIGH : FOV_PRESET_MEDIUM);
    if (!enabled.empty()) s.enabled = enabled == "true";
    if (!mode.empty()) s.mode = mode == "off" ? FOV_MODE_OFF : mode == "eye" ? FOV_MODE_EYE : FOV_MODE_FIXED;
    if (!tint.empty()) s.debug_tint = tint == "true";
}

std::string read_text(const std::wstring& path) {
    std::string text;
    if (FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO)) {
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
        fclose(f);
    }
    return text;
}

std::wstring appdata_file(const wchar_t* name) {
    std::wstring dir = host_dir(Host::Config);
    return dir.empty() ? dir : dir + L"\\" + name;
}

}  // namespace fov

extern "C" void fov_settings_load(FovSettings* out) {
    fov_settings_default(out, FOV_PRESET_MEDIUM);
    std::string text = fov::read_text(fov::appdata_file(L"settings.toml"));
    fov::apply_settings_toml(text, *out, nullptr);
}
