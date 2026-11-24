// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

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
    // The uninstaller: every game's openvr_api.dll back as it was, and on Linux every Proton prefix's registry, then
    // the copies of the layer in Flatpak and Snap Steam's folders (kept for a game that could not be undone).
    if std::env::args().any(|a| a == "--uninstall") {
        // Both run, whatever the first one says.
        let openvr = openvr::disable_all();
        let proton = if cfg!(unix) { proton::disable_all(&ui::layer_manifest(None)) } else { Ok(()) };
        let copies = proton::prune_copies();
        return match openvr.and(proton).and(copies) {
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
            eye_passes: 0,
            last_error: [0; 96],
        };
        s.exe[..exe.len()].copy_from_slice(exe.as_bytes());
        s
    }

    #[test]
    fn readout_for_the_running_game() {
        use crate::settings::AppSettings;
        let on = AppSettings::default();
        let readout = |slots: &[(bool, fov_shm::Stats)]| crate::ui::readout(slots, &on, false);
        assert!(!readout(&[]).valid);
        assert!(readout(&[(false, stats("old.exe", 9, 0, 1.0, 2.0))]).text.starts_with("No game running"));
        let r = readout(&[(true, stats("game.exe", 5, 0, 1.0, 2.0)), (true, stats("idle.exe", 1, 0, 0.0, 0.0))]);
        assert!(r.valid && (r.ms - 1.0).abs() < 1e-6 && (r.pct - 50.0).abs() < 1e-4 && r.text == "game.exe, Fixed");
        assert!(readout(&[(true, stats("game.exe", 5, 0, 0.0, 0.0))]).text.contains("Ctrl+F9"));
        assert_eq!(
            readout(&[(true, stats("ac.exe", 5, 4, 0.0, 0.0))]).text,
            "ac.exe: not foveated, blocked: anti-cheat."
        );
        let gpu = readout(&[(true, stats("old.exe", 5, 7, 0.0, 0.0))]);
        assert!(!gpu.valid && gpu.text.contains("no variable rate shading"));
    }

    #[test]
    fn readout_says_what_is_true_of_the_game() {
        use crate::settings::AppSettings;
        use crate::ui::readout;
        let fixed = AppSettings::default();
        let eye = AppSettings { eye_tracked: true, ..AppSettings::default() };
        let off = AppSettings { enabled: false, ..AppSettings::default() };
        // The layer is not registered: said while nothing runs, where the user wonders why nothing shows up.
        assert!(readout(&[], &fixed, true).text.starts_with("Not installed"));
        // The switch is off: the game is not called foveated.
        let game = stats("game.exe", 5000, 0, 0.0, 0.0);
        assert_eq!(readout(&[(true, game)], &off, false).text, "game.exe: FoveaXR is in this game, switched off.");
        // In the game, but none of its passes was recognised: not called foveated either. A game still loading
        // (few frames) is given time.
        assert!(readout(&[(true, game)], &fixed, false).text.contains("found no eye pass"));
        assert!(
            readout(&[(true, stats("game.exe", 100, 0, 0.0, 0.0))], &fixed, false).text.contains("foveated, Fixed")
        );
        // Eye-tracked is said only when eye data arrives in the game.
        let mut seen = game;
        seen.eye_passes = 40_000;
        seen.rings_bound = 40_000;
        assert!(readout(&[(true, seen)], &eye, false).text.contains("no eye tracking reaches this game"));
        seen.gaze_source = 1;
        seen.gaze_valid_pct = 97.0;
        assert!(readout(&[(true, seen)], &eye, false).text.contains("foveated, eye-tracked."));
        assert!(readout(&[(true, seen)], &fixed, false).text.contains("eye tracking is available"));
        seen.eye_ms_on = 1.0;
        seen.eye_ms_off = 2.0;
        assert_eq!(readout(&[(true, seen)], &eye, false).text, "game.exe, eye-tracked");
    }
}
