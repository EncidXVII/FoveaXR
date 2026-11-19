//! The few Windows calls the app needs beyond the mapping; kept here, the one crate that may use unsafe.
#![allow(unsafe_code)]

use windows::Win32::Foundation::{CloseHandle, ERROR_ALREADY_EXISTS, GetLastError, HANDLE, WAIT_TIMEOUT};
use windows::Win32::System::Registry::{HKEY_CURRENT_USER, RRF_RT_REG_DWORD, RRF_RT_REG_SZ, RegGetValueW};
use windows::Win32::System::Threading::{CreateMutexW, OpenProcess, PROCESS_SYNCHRONIZE, WaitForSingleObject};
use windows::Win32::UI::WindowsAndMessaging::{
    GetSystemMetrics, SM_CXSMICON, SPI_GETCLIENTAREAANIMATION, SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS,
    SystemParametersInfoW,
};
use windows::core::{HSTRING, PCWSTR, w};

/// Whether a process is still running (a game's slot outlives it until another game reuses it).
pub fn alive(pid: u32) -> bool {
    // SAFETY: plain Win32 calls on a handle closed before returning.
    unsafe {
        let Ok(h) = OpenProcess(PROCESS_SYNCHRONIZE, false, pid) else { return false };
        let running = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
        let _ = CloseHandle(h);
        running
    }
}

/// Held for the app's lifetime; a second copy would fight the first over settings and the tray.
pub struct Instance(HANDLE);

impl Drop for Instance {
    fn drop(&mut self) {
        // SAFETY: the handle came from CreateMutexW and is closed once.
        unsafe {
            let _ = CloseHandle(self.0);
        }
    }
}

/// None when another copy of the app already runs in this session.
pub fn single_instance() -> Option<Instance> {
    // SAFETY: plain Win32 call; the error code is read right after it.
    unsafe {
        let h = CreateMutexW(None, false, w!("Local\\FoveatedVR.app")).ok()?;
        if GetLastError() == ERROR_ALREADY_EXISTS {
            let _ = CloseHandle(h);
            return None;
        }
        Some(Instance(h))
    }
}

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
