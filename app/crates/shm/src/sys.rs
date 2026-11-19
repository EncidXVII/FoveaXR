//! The few Windows calls the app needs beyond the mapping; kept here, the one crate that may use unsafe.
#![allow(unsafe_code)]

use windows::Win32::System::Registry::{HKEY_CURRENT_USER, RRF_RT_REG_DWORD, RRF_RT_REG_SZ, RegGetValueW};
use windows::Win32::UI::WindowsAndMessaging::{
    GetSystemMetrics, SM_CXSMICON, SPI_GETCLIENTAREAANIMATION, SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS,
    SystemParametersInfoW,
};
use windows::core::{HSTRING, PCWSTR, w};

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
