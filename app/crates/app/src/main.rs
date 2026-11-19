// No console window when started from the Start menu or autostart.
#![cfg_attr(all(windows, not(test)), windows_subsystem = "windows")]

mod games;
mod openvr;
mod platform;
mod proton;
mod settings;
mod steam;
mod tray;
mod ui;

use std::process::ExitCode;

fn main() -> ExitCode {
    // The uninstaller: every game's openvr_api.dll back as it was, and on Linux every Proton prefix's registry.
    if std::env::args().any(|a| a == "--uninstall") {
        let proton = || if cfg!(unix) { proton::disable_all(&ui::layer_manifest()) } else { Ok(()) };
        return match openvr::disable_all().and_then(|()| proton()) {
            Ok(()) => ExitCode::SUCCESS,
            Err(e) => {
                eprintln!("{e}");
                ExitCode::FAILURE
            }
        };
    }
    // A second copy would fight the first over settings and the tray; the first one stays.
    let Some(_instance) = platform::single_instance() else { return ExitCode::SUCCESS };
    match ui::run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
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
    fn readout_for_the_running_game() {
        use crate::ui::readout;
        assert!(!readout(&[]).valid);
        assert!(readout(&[(false, stats("old.exe", 9, 0, 1.0, 2.0))]).text.starts_with("No game running"));
        let r = readout(&[(true, stats("game.exe", 5, 0, 1.0, 2.0)), (true, stats("idle.exe", 1, 0, 0.0, 0.0))]);
        assert!(r.valid && (r.ms - 1.0).abs() < 1e-6 && (r.pct - 50.0).abs() < 1e-4 && r.text == "game.exe");
        assert!(readout(&[(true, stats("game.exe", 5, 0, 0.0, 0.0))]).text.contains("Ctrl+F9"));
        assert_eq!(
            readout(&[(true, stats("ac.exe", 5, 4, 0.0, 0.0))]).text,
            "ac.exe: not foveated, blocked: anti-cheat."
        );
        let gpu = readout(&[(true, stats("old.exe", 5, 7, 0.0, 0.0))]);
        assert!(!gpu.valid && gpu.text.contains("no variable rate shading"));
    }
}
