//! The window: settings auto-saved and pushed to every game live, the games list with per-game enable, the running
//! game's GPU saving, Export logs.

use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::mpsc;
use std::time::Duration;

use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

use crate::openvr::{self, State};
use crate::settings::AppSettings;
use crate::steam::{self, Block, Game};
use crate::tray;

fn app_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default()
}

/// The proxy beside the app (installed), or in the native build (development).
fn proxy_dll() -> PathBuf {
    let dir = app_dir();
    let installed = dir.join("openvr_proxy").join("openvr_api.dll");
    if installed.exists() { installed } else { dir.join("../../build/Release/openvr_proxy/openvr_api.dll") }
}

fn row(game: &Game, settings: &AppSettings, records: &[openvr::Install]) -> fov_ui::GameRow {
    let (detail, action, can_act, on): (&str, &str, bool, bool) = match game.block {
        Some(Block::AntiCheat) => ("Blocked: anti-cheat", "Blocked", false, false),
        Some(Block::NativeFoveation) => ("Has its own foveated rendering", "Not needed", false, false),
        None if !game.openvr.is_empty() => match openvr::state(&game.openvr, records) {
            State::On => ("OpenVR · on", "Disable", true, true),
            State::Off => ("OpenVR · enable to foveate", "Enable", true, false),
            State::NeedsReenable => ("OpenVR · game updated, enable again", "Re-enable", true, false),
        },
        None if game.exes.iter().any(|e| settings.off_games.contains(e)) => {
            ("OpenXR · off for this game", "Turn on", true, false)
        }
        None => ("OpenXR · on automatically", "Turn off", true, true),
    };
    fov_ui::GameRow {
        name: SharedString::from(game.name.as_str()),
        detail: SharedString::from(detail),
        action: SharedString::from(action),
        can_act,
        on,
    }
}

/// Logs, settings and installs as one zip on the desktop (Windows' own tar writes zips).
fn export_logs() -> Result<PathBuf, String> {
    use std::os::windows::process::CommandExt;
    let local = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).ok_or("no LOCALAPPDATA")?.join("FoveatedVR");
    let profile = std::env::var_os("USERPROFILE").map(PathBuf::from).ok_or("no USERPROFILE")?;
    let out = profile.join("Desktop").join("FoveatedVR-logs.zip");
    let mut cmd = std::process::Command::new("tar");
    cmd.arg("-a").arg("-c").arg("-f").arg(&out).creation_flags(0x0800_0000);
    if local.join("logs").exists() {
        cmd.arg("-C").arg(&local).arg("logs");
    }
    if let Some(dir) = crate::settings::dir().filter(|d| d.exists()) {
        cmd.arg("-C").arg(dir).arg(".");
    }
    let status = cmd.status().map_err(|e| e.to_string())?;
    if status.success() { Ok(out) } else { Err("no logs yet".into()) }
}

pub fn run() -> Result<(), String> {
    let shm = Rc::new(fov_shm::Mapping::open()?);
    let settings = Rc::new(std::cell::RefCell::new(AppSettings::load()));
    shm.write_settings(&settings.borrow().to_shm());
    let ui = fov_ui::MainWindow::new().map_err(|e| e.to_string())?;
    {
        let s = settings.borrow();
        ui.set_enabled(s.enabled);
        ui.set_mode(i32::from(s.eye_tracked));
        ui.set_preset(i32::try_from(s.preset).unwrap_or(1));
        ui.set_tint(s.debug_tint);
    }
    ui.on_changed({
        let (ui, shm, settings) = (ui.as_weak(), Rc::clone(&shm), Rc::clone(&settings));
        move || {
            let Some(ui) = ui.upgrade() else { return };
            let mut s = settings.borrow_mut();
            s.enabled = ui.get_enabled();
            s.eye_tracked = ui.get_mode() == 1;
            s.preset = u32::try_from(ui.get_preset()).unwrap_or(fov_shm::preset::MEDIUM);
            s.debug_tint = ui.get_tint();
            shm.write_settings(&s.to_shm());
            if let Err(e) = s.save() {
                ui.set_note(format!("Not saved: {e}").into());
            }
        }
    });
    ui.on_export_logs({
        let ui = ui.as_weak();
        move || {
            let note = match export_logs() {
                Ok(p) => format!("Saved {}", p.display()),
                Err(e) => format!("Export failed: {e}"),
            };
            if let Some(ui) = ui.upgrade() {
                ui.set_note(note.into());
            }
        }
    });

    // Games: scanned once in the background, then kept in the list.
    let games: Rc<std::cell::RefCell<Vec<Game>>> = Rc::default();
    let rows = Rc::new(VecModel::<fov_ui::GameRow>::default());
    ui.set_games(ModelRc::from(Rc::clone(&rows)));
    let refresh = {
        let (games, rows, settings) = (Rc::clone(&games), Rc::clone(&rows), Rc::clone(&settings));
        move || {
            let records = openvr::load();
            let s = settings.borrow();
            rows.set_vec(games.borrow().iter().map(|g| row(g, &s, &records)).collect::<Vec<_>>());
        }
    };
    let refresh = Rc::new(refresh);
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let profiles = steam::Profiles::load(&app_dir());
        let _ = tx.send(steam::scan(&profiles));
    });
    ui.on_game_action({
        let (ui, games, settings, refresh) =
            (ui.as_weak(), Rc::clone(&games), Rc::clone(&settings), Rc::clone(&refresh));
        move |i| {
            let Some(game) = usize::try_from(i).ok().and_then(|i| games.borrow().get(i).cloned()) else { return };
            let result = if game.openvr.is_empty() {
                let mut s = settings.borrow_mut();
                if game.exes.iter().any(|e| s.off_games.contains(e)) {
                    s.off_games.retain(|e| !game.exes.contains(e));
                } else {
                    s.off_games.extend(game.exes.iter().cloned());
                }
                s.save()
            } else if openvr::state(&game.openvr, &openvr::load()) == State::On {
                openvr::disable(&game.openvr)
            } else {
                openvr::enable(&game.openvr, &proxy_dll())
            };
            if let (Err(e), Some(ui)) = (result, ui.upgrade()) {
                ui.set_games_status(format!("{}: {e}", game.name).into());
            }
            refresh();
        }
    });

    let tray = tray::Tray::new()?;
    let timer = slint::Timer::default();
    timer.start(slint::TimerMode::Repeated, Duration::from_millis(250), {
        let (ui, shm, games, refresh) = (ui.as_weak(), Rc::clone(&shm), Rc::clone(&games), Rc::clone(&refresh));
        move || {
            let Some(ui) = ui.upgrade() else { return };
            match tray.poll() {
                Some(tray::Command::Open) => {
                    let _ = ui.show();
                }
                Some(tray::Command::Quit) => {
                    let _ = slint::quit_event_loop();
                }
                None => {}
            }
            if let Ok(found) = rx.try_recv() {
                ui.set_games_status(if found.is_empty() {
                    "No installed Steam VR games found.".into()
                } else {
                    "OpenXR games are on unless turned off. OpenVR games need Enable (restart the game).".into()
                });
                *games.borrow_mut() = found;
                refresh();
            }
            let slots: Vec<_> = shm.stats().into_iter().map(|(pid, s)| (fov_shm::alive(pid), s)).collect();
            ui.set_status(crate::status(&slots).into());
        }
    });
    ui.show().map_err(|e| e.to_string())?;
    // Closing the window hides it; the app lives in the tray until Quit.
    slint::run_event_loop_until_quit().map_err(|e| e.to_string())
}
