// No console window when started from the Start menu or autostart.
#![cfg_attr(all(windows, not(test)), windows_subsystem = "windows")]

mod openvr;
mod settings;
#[cfg(windows)]
mod steam;
#[cfg(windows)]
mod tray;
#[cfg(windows)]
mod ui;

use std::process::ExitCode;

fn main() -> ExitCode {
    // The uninstaller: every game's openvr_api.dll back as it was.
    if std::env::args().any(|a| a == "--uninstall") {
        return match openvr::disable_all() {
            Ok(()) => ExitCode::SUCCESS,
            Err(e) => {
                eprintln!("{e}");
                ExitCode::FAILURE
            }
        };
    }
    #[cfg(windows)]
    let result = ui::run();
    #[cfg(not(windows))]
    let result: Result<(), String> = Err("FoveatedVR runs on Windows only.".into());
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}

/// The running game's line on the main view, from the stats games publish: (process alive, stats) pairs.
fn status(slots: &[(bool, fov_shm::Stats)]) -> String {
    let Some(s) = slots.iter().filter(|(alive, _)| *alive).map(|(_, s)| s).max_by_key(|s| s.frames) else {
        return "No game running".into();
    };
    let exe = s.exe();
    if let Some(why) = s.inactive_reason() {
        return format!("{exe}: not foveated, {why}");
    }
    if s.eye_ms_on > 0.0 && s.eye_ms_off > 0.0 {
        let saved = s.eye_ms_off - s.eye_ms_on;
        return format!(
            "{exe}: GPU saved {saved:.2} ms per frame ({:.0}% of eye rendering)",
            100.0 * saved / s.eye_ms_off
        );
    }
    format!("{exe}: foveated. Press Ctrl+F9 in VR to measure the saving")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stats(exe: &str, frames: u64, reason: u32, on: f32, off: f32) -> fov_shm::Stats {
        let mut s = fov_shm::Stats {
            exe: [0; 64],
            app_id: 0,
            api: 1,
            gfx: 1,
            layout: 1,
            active: u32::from(reason == 0),
            reason,
            gaze_source: 0,
            gaze_valid_pct: 0.0,
            eye_ms_on: on,
            eye_ms_off: off,
            frames,
            rings_bound: 0,
            uncertain_eye: 0,
            focus: [[0.5; 2]; 2],
            ui_skipped: 0,
            pad2: 0,
            last_error: [0; 96],
        };
        s.exe[..exe.len()].copy_from_slice(exe.as_bytes());
        s
    }

    #[test]
    fn status_line() {
        assert_eq!(status(&[]), "No game running");
        assert_eq!(status(&[(false, stats("old.exe", 9, 0, 1.0, 2.0))]), "No game running");
        assert_eq!(
            status(&[(true, stats("game.exe", 5, 0, 1.0, 2.0))]),
            "game.exe: GPU saved 1.00 ms per frame (50% of eye rendering)"
        );
        assert!(status(&[(true, stats("game.exe", 5, 0, 0.0, 0.0))]).contains("Ctrl+F9"));
        assert_eq!(status(&[(true, stats("ac.exe", 5, 4, 0.0, 0.0))]), "ac.exe: not foveated, blocked: anti-cheat");
    }
}
