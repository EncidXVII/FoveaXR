// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! The few Windows calls the app needs beyond the mapping; kept here, the one crate that may use unsafe.
#![allow(unsafe_code)]

use windows::Win32::Foundation::{ERROR_MORE_DATA, ERROR_NO_MORE_ITEMS};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, KEY_READ, RRF_RT_REG_DWORD, RRF_RT_REG_SZ, RegCloseKey, RegEnumValueW,
    RegGetValueW, RegOpenKeyExW,
};
use windows::Win32::UI::WindowsAndMessaging::{
    GetSystemMetrics, SM_CXSMICON, SPI_GETCLIENTAREAANIMATION, SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS,
    SystemParametersInfoW,
};
use windows::core::{HSTRING, PCWSTR, PWSTR, w};

/// Windows' "Animation effects" (Settings > Accessibility > Visual effects). Off keeps every state change and
/// drops the movement. On when the query fails: the setting is unknown, not off.
pub fn animations_enabled() -> bool {
    let mut on = windows::core::BOOL(1);
    // SAFETY: plain query into a BOOL we own.
    let ok = unsafe {
        SystemParametersInfoW(
            SPI_GETCLIENTAREAANIMATION,
            0,
            Some((&raw mut on).cast()),
            SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0),
        )
    };
    ok.is_err() || on.as_bool()
}

fn reg_dword(key: PCWSTR, value: PCWSTR) -> Option<u32> {
    let mut v = 0u32;
    let mut len = 4u32;
    // SAFETY: a DWORD written into `v`, at most `len` bytes.
    let r = unsafe {
        RegGetValueW(
            HKEY_CURRENT_USER,
            key,
            value,
            RRF_RT_REG_DWORD,
            None,
            Some((&raw mut v).cast()),
            Some(&raw mut len),
        )
    };
    r.is_ok().then_some(v)
}

/// Whether Windows draws the taskbar light (then the tray icon is the dark-ink one).
pub fn light_taskbar() -> bool {
    reg_dword(w!(r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"), w!("SystemUsesLightTheme"))
        == Some(1)
}

/// The size Windows draws small icons at (the tray), in pixels at the current display scale.
pub fn small_icon_px() -> u32 {
    // SAFETY: plain read of a system value.
    u32::try_from(unsafe { GetSystemMetrics(SM_CXSMICON) }).unwrap_or(16).max(16)
}

/// Steam's install folder (HKCU\Software\Valve\Steam\SteamPath), None without Steam.
pub fn steam_path() -> Option<String> {
    let key = HSTRING::from(r"Software\Valve\Steam");
    let value = HSTRING::from("SteamPath");
    let mut buf = [0u16; 1024];
    let mut len = u32::try_from(std::mem::size_of_val(&buf)).ok()?;
    // SAFETY: a UTF-16 string written into `buf`, at most `len` bytes.
    let r = unsafe {
        RegGetValueW(
            HKEY_CURRENT_USER,
            PCWSTR(key.as_ptr()),
            PCWSTR(value.as_ptr()),
            RRF_RT_REG_SZ,
            None,
            Some(buf.as_mut_ptr().cast()),
            Some(&raw mut len),
        )
    };
    if r.is_err() {
        return None;
    }
    let n = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    Some(String::from_utf16_lossy(&buf[..n]))
}

/// The implicit OpenXR API layers one registry root lists: (manifest path, disabled). The loader's convention is a
/// value named by the manifest's path, with 0 for an enabled layer.
fn implicit_layers(root: HKEY) -> Vec<(String, bool)> {
    let mut out = Vec::new();
    let mut key = HKEY::default();
    // SAFETY: the key is opened read-only and closed below; names and data go into buffers we own, with their sizes.
    unsafe {
        if RegOpenKeyExW(root, w!(r"Software\Khronos\OpenXR\1\ApiLayers\Implicit"), None, KEY_READ, &raw mut key)
            .is_err()
        {
            return out;
        }
        for index in 0.. {
            let mut name = [0u16; 1024];
            let mut name_len = 1024u32;
            let mut data = 0u32;
            let mut data_len = 4u32;
            let r = RegEnumValueW(
                key,
                index,
                Some(PWSTR(name.as_mut_ptr())),
                &raw mut name_len,
                None,
                None,
                Some((&raw mut data).cast()),
                Some(&raw mut data_len),
            );
            if r == ERROR_NO_MORE_ITEMS {
                break;
            }
            // A value that is no DWORD (too long for `data`) is not a layer entry: skipped, the rest still counts.
            if r.is_ok() {
                out.push((String::from_utf16_lossy(&name[..name_len as usize]), data != 0));
            } else if r != ERROR_MORE_DATA {
                break;
            }
        }
        let _ = RegCloseKey(key);
    }
    out
}

/// Whether the OpenXR loader will load our layer into this user's games: a manifest named `file` is registered
/// (for the user or the machine), enabled, and still on disk.
pub fn openxr_layer_registered(file: &str) -> bool {
    [HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE].into_iter().flat_map(implicit_layers).any(|(path, disabled)| {
        let p = std::path::Path::new(&path);
        !disabled && p.file_name().is_some_and(|n| n.eq_ignore_ascii_case(file)) && p.exists()
    })
}
