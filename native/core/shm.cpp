// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fov_core.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>
#include "fov_frame.h"

namespace {
FovShm* g_shm = nullptr;
constexpr int kRetries = 64;

std::atomic_ref<uint32_t> seq(uint32_t& s) { return std::atomic_ref<uint32_t>(s); }

bool usable() { return fov_shm_connect() && g_shm->magic == FOV_SHM_MAGIC && g_shm->version == FOV_SHM_VERSION; }

template <typename T>
bool read_block(uint32_t& counter, const T& block, T* out) {
    for (int i = 0; i < kRetries; ++i) {
        uint32_t before = seq(counter).load(std::memory_order_acquire);
        if (before & 1) {
            YieldProcessor();
            continue;
        }
        std::memcpy(out, &block, sizeof(T));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq(counter).load(std::memory_order_relaxed) == before) return true;
    }
    return false;
}

template <typename T>
void write_block(uint32_t& counter, T& block, const T& in) {
    seq(counter).fetch_add(1, std::memory_order_acq_rel);
    std::memcpy(&block, &in, sizeof(T));
    seq(counter).fetch_add(1, std::memory_order_release);
}
}  // namespace

extern "C" int fov_shm_connect(void) {
    static bool tried = false;
    if (g_shm || tried) return g_shm != nullptr;
    tried = true;
    // Inside Wine a named section is invisible to Linux processes: the app there maps /dev/shm/FoveaXR.v1, which
    // Wine shows as Z:\dev\shm\FoveaXR.v1, and a file mapping of it shares the same pages. FOV_SHM_FILE forces
    // the file path anywhere (tests on Windows). Flatpak and Snap Steam have a /dev/shm of their own: there the file
    // is FoveaXR.v1 in the logs' folder under the sandbox's home, where the app maps it too.
    wchar_t file_path[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"FOV_SHM_FILE", file_path, MAX_PATH) && fov_is_wine()) {
        if (!fov::in_sandbox()) {
            wcscpy_s(file_path, L"Z:\\dev\\shm\\FoveaXR.v1");
        } else if (std::wstring dir = fov::host_dir(fov::Host::State); !dir.empty()) {
            std::error_code ignored;
            std::filesystem::create_directories(dir, ignored);
            wcsncpy_s(file_path, (dir + L"\\FoveaXR.v1").c_str(), _TRUNCATE);
        }
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    if (file_path[0]) {
        file = CreateFileW(file_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size;
        if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)) {
            fov_log("shm: cannot open %ls (%lu)", file_path, GetLastError());
            return 0;
        }
        // A new file is empty: grow it to the struct, zero filled.
        if (size.QuadPart < (LONGLONG)sizeof(FovShm)) {
            size.QuadPart = sizeof(FovShm);
            SetFilePointerEx(file, size, nullptr, FILE_BEGIN);
            SetEndOfFile(file);
        }
    }
    HANDLE h = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, sizeof(FovShm),
                                  file == INVALID_HANDLE_VALUE ? FOV_SHM_NAME : nullptr);
    if (!h) {
        fov_log("shm: CreateFileMapping failed (%lu)", GetLastError());
        return 0;
    }
    // The mapping lives as long as any process holds it; this handle is kept for the life of the process.
    g_shm = static_cast<FovShm*>(MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FovShm)));
    if (!g_shm) {
        fov_log("shm: MapViewOfFile failed (%lu)", GetLastError());
        return 0;
    }
    // Whoever maps it first among the core and the app writes the header and default settings; the harness only
    // writes gaze. Two games starting at once may both do it, with the same values.
    if (g_shm->magic != FOV_SHM_MAGIC) {
        FovSettings s;
        fov_settings_load(&s);
        write_block(g_shm->settings_seq, g_shm->settings, s);
        g_shm->version = FOV_SHM_VERSION;
        g_shm->size = sizeof(FovShm);
        std::atomic_ref<uint32_t>(g_shm->magic).store(FOV_SHM_MAGIC, std::memory_order_release);
    } else if (g_shm->version != FOV_SHM_VERSION) {
        fov_log("shm: version %u, expected %u; ignoring it", g_shm->version, FOV_SHM_VERSION);
    }
    return 1;
}

extern "C" int fov_shm_read_settings(FovSettings* out) {
    return usable() && read_block(g_shm->settings_seq, g_shm->settings, out);
}

extern "C" int fov_shm_read_gaze(FovGaze* out) { return usable() && read_block(g_shm->gaze_seq, g_shm->gaze, out); }

extern "C" int fov_shm_write_gaze(const FovGaze* in) {
    if (!usable()) return 0;
    write_block(g_shm->gaze_seq, g_shm->gaze, *in);
    return 1;
}

extern "C" int fov_shm_claim_slot(void) {
    if (!usable()) return -1;
    uint32_t me = GetCurrentProcessId();
    for (int i = 0; i < FOV_SHM_SLOTS; ++i) {
        std::atomic_ref<uint32_t> pid(g_shm->slots[i].pid);
        uint32_t cur = pid.load();
        if (cur && cur != me) {
            // Taken unless its process is gone (a crashed game never frees its slot).
            // A process we may not open (it runs elevated) is there all the same.
            HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, cur);
            bool alive = h ? WaitForSingleObject(h, 0) == WAIT_TIMEOUT : GetLastError() == ERROR_ACCESS_DENIED;
            if (h) CloseHandle(h);
            if (alive) continue;
        }
        if (pid.compare_exchange_strong(cur, me)) return i;
    }
    return -1;
}

extern "C" void fov_shm_write_stats(int slot, const FovStats* in) {
    if (usable() && slot >= 0 && slot < FOV_SHM_SLOTS)
        write_block(g_shm->slots[slot].stats_seq, g_shm->slots[slot].stats, *in);
}

extern "C" void fov_settings_default(FovSettings* out, uint32_t preset) {
    *out = {};
    out->enabled = 1;
    out->mode = FOV_MODE_FIXED;
    out->preset = preset;
    out->saccade_expand = 1;
    // SPEC 3 starting values; gate P tuning may change them.
    switch (preset) {
    case FOV_PRESET_LOW:
        out->inner_deg = 30, out->mid_deg = 45;
        out->rate[0] = FOV_RATE_1X1, out->rate[1] = FOV_RATE_1X1, out->rate[2] = FOV_RATE_2X2;
        break;
    case FOV_PRESET_HIGH:
        out->inner_deg = 15, out->mid_deg = 22;
        out->rate[0] = FOV_RATE_1X1, out->rate[1] = FOV_RATE_2X2, out->rate[2] = FOV_RATE_4X4;
        break;
    default:
        out->preset = FOV_PRESET_MEDIUM;
        out->inner_deg = 22, out->mid_deg = 35;
        out->rate[0] = FOV_RATE_1X1, out->rate[1] = FOV_RATE_2X2, out->rate[2] = FOV_RATE_4X4;
    }
    out->smooth_min_cutoff = 1.0f, out->smooth_beta = 0.05f, out->smooth_d_cutoff = 1.0f;
    out->saccade_deg_s = 180, out->saccade_factor = 1.5f, out->saccade_hold_ms = 50;
}

extern "C" int64_t fov_qpc_now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

extern "C" double fov_qpc_ms(int64_t from, int64_t to) {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return (double)f.QuadPart;
    }();
    return (to - from) * 1000.0 / freq;
}
