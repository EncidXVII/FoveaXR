// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

/* fov_core: the foveation core as a static library with a small C API, so other injectors can link it. */
#ifndef FOV_CORE_H
#define FOV_CORE_H
#include <stdint.h>
#include "fov_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* fov_version(void);

/* 1 inside Wine/Proton (ntdll exports wine_get_version), where the app runs natively on Linux. */
int fov_is_wine(void);

/* One line per state change, never per frame. Goes to FOV_LOG_FILE if set (tests), else
 * %LOCALAPPDATA%\FoveaXR\logs\<exe>-<yyyymmdd>.log; a file past 1 MB is moved to .old. */
void fov_log(const char* fmt, ...);

/* Maps the shared memory, creating it if nobody has yet; writes the header and default settings if no one has.
 * Returns 1 when mapped. Cheap after the first call. */
int fov_shm_connect(void);

/* Seqlock copies. Return 0 when not mapped, not initialised, of another version, or torn on every retry. */
int fov_shm_read_settings(FovSettings* out);
int fov_shm_read_gaze(FovGaze* out);
int fov_shm_write_gaze(const FovGaze* in);

/* A stats slot for this process (claimed by pid, dead owners' slots reused), -1 if none; its stats writer. */
int fov_shm_claim_slot(void);
void fov_shm_write_stats(int slot, const FovStats* in);

/* Defaults for a preset (SPEC 3), and the app's %APPDATA%\FoveaXR\settings.toml applied over Medium. */
void fov_settings_default(FovSettings* out, uint32_t preset);
void fov_settings_load(FovSettings* out);

int64_t fov_qpc_now(void);
double fov_qpc_ms(int64_t from, int64_t to);

#ifdef __cplusplus
}
#endif
#endif
