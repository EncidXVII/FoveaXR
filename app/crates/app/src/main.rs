// No console window when started from the Start menu or autostart.
#![cfg_attr(all(windows, not(test)), windows_subsystem = "windows")]

#[cfg(windows)]
mod tray;

use std::process::ExitCode;

fn main() -> ExitCode {
    #[cfg(windows)]
    let result = run();
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

/// Maps the UI's choices onto the settings every game reads; presets resolve into radii and rates here.
fn settings(enabled: bool, eye_tracked: bool, preset: i32) -> fov_shm::Settings {
    let mut s = fov_shm::Settings::preset(u32::try_from(preset).unwrap_or(fov_shm::preset::MEDIUM));
    s.enabled = u32::from(enabled);
    s.mode = if eye_tracked { fov_shm::mode::EYE } else { fov_shm::mode::FIXED };
    s
}

#[cfg(windows)]
fn run() -> Result<(), String> {
    use slint::ComponentHandle;
    use std::rc::Rc;

    let shm = Rc::new(fov_shm::Mapping::open()?);
    let ui = fov_ui::MainWindow::new().map_err(|e| e.to_string())?;
    let current = shm.settings().unwrap_or_else(|| fov_shm::Settings::preset(fov_shm::preset::MEDIUM));
    ui.set_enabled(current.enabled != 0);
    ui.set_mode(i32::from(current.mode == fov_shm::mode::EYE));
    ui.set_preset(i32::try_from(current.preset.min(fov_shm::preset::HIGH)).unwrap_or(1));
    ui.on_changed({
        let ui = ui.as_weak();
        let shm = Rc::clone(&shm);
        move || {
            if let Some(ui) = ui.upgrade() {
                shm.write_settings(&settings(ui.get_enabled(), ui.get_mode() == 1, ui.get_preset()));
            }
        }
    });

    let tray = tray::Tray::new()?;
    let timer = slint::Timer::default();
    timer.start(slint::TimerMode::Repeated, std::time::Duration::from_millis(100), {
        let ui = ui.as_weak();
        move || match tray.poll() {
            Some(tray::Command::Open) => {
                if let Some(ui) = ui.upgrade() {
                    let _ = ui.show();
                }
            }
            Some(tray::Command::Quit) => {
                let _ = slint::quit_event_loop();
            }
            None => {}
        }
    });
    ui.show().map_err(|e| e.to_string())?;
    // Closing the window hides it; the app lives in the tray until Quit.
    slint::run_event_loop_until_quit().map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ui_choices_map_to_settings() {
        let s = settings(true, true, 2);
        assert_eq!((s.enabled, s.mode, s.preset, s.inner_deg), (1, fov_shm::mode::EYE, fov_shm::preset::HIGH, 15.0));
        let s = settings(false, false, -1);
        assert_eq!((s.enabled, s.mode, s.preset), (0, fov_shm::mode::FIXED, fov_shm::preset::MEDIUM));
    }
}
