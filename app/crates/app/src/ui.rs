//! The window: settings auto-saved and pushed to every game live, the games list with per-game enable, the running
//! game's GPU saving, Export logs. Slow work (scanning, file moves, the logs zip) runs on worker threads and reports
//! back through a channel the UI timer drains.

use std::cell::RefCell;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::mpsc;
use std::time::Duration;

use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

use crate::games::{self, Entry, Filter};
use crate::openvr::{self, State};
use crate::settings::AppSettings;
use crate::steam::{self, Scan};
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

/// Logs, settings and installs as one zip on the desktop (Windows' own tar writes zips), shown in Explorer.
fn export_logs() -> Result<PathBuf, String> {
    use std::os::windows::process::CommandExt;
    const NO_WINDOW: u32 = 0x0800_0000;
    let local = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).ok_or("no LOCALAPPDATA")?.join("FoveatedVR");
    let profile = std::env::var_os("USERPROFILE").map(PathBuf::from).ok_or("no USERPROFILE")?;
    // A desktop moved by OneDrive or policy may not be under the profile: then the profile folder itself.
    let desktop = Some(profile.join("Desktop")).filter(|d| d.is_dir()).unwrap_or(profile);
    let out = desktop.join("FoveatedVR-logs.zip");
    let settings = crate::settings::dir().filter(|d| d.exists());
    if !local.join("logs").exists() && settings.is_none() {
        return Err("no logs yet: start a VR game first".into());
    }
    let mut cmd = std::process::Command::new("tar");
    cmd.arg("-a").arg("-c").arg("-f").arg(&out).creation_flags(NO_WINDOW);
    if local.join("logs").exists() {
        cmd.arg("-C").arg(&local).arg("logs");
    }
    if let Some(dir) = settings {
        cmd.arg("-C").arg(dir).arg(".");
    }
    if !cmd.status().map_err(|e| e.to_string())?.success() {
        return Err("the zip could not be written".into());
    }
    let _ = std::process::Command::new("explorer").arg(format!("/select,{}", out.display())).spawn();
    Ok(out)
}

/// What the GPU-saved card shows for the running game.
pub struct Readout {
    pub text: String,
    pub ms: f32,
    pub pct: f32,
    pub valid: bool,
}

/// From the stats games publish: (process alive, stats) pairs.
pub fn readout(slots: &[(bool, fov_shm::Stats)]) -> Readout {
    let none = |text: String| Readout { text, ms: 0.0, pct: 0.0, valid: false };
    let Some(s) = slots.iter().filter(|(alive, _)| *alive).map(|(_, s)| s).max_by_key(|s| s.frames) else {
        return none("No game running. Start a VR game: the saving shows here once you compare with Ctrl+F9.".into());
    };
    let exe = s.exe();
    if let Some(why) = s.inactive_reason() {
        return none(format!("{exe}: not foveated, {why}."));
    }
    if s.eye_ms_on > 0.0 && s.eye_ms_off > 0.0 {
        let ms = s.eye_ms_off - s.eye_ms_on;
        return Readout { text: exe, ms, pct: 100.0 * ms / s.eye_ms_off, valid: true };
    }
    none(format!("{exe}: foveated. Press Ctrl+F9 twice in VR to measure off against on."))
}

enum Done {
    Scan(Scan),
    Action { id: usize, result: Result<String, String> },
    Export(Result<PathBuf, String>),
}

struct App {
    shm: fov_shm::Mapping,
    settings: AppSettings,
    entries: Vec<Entry>,
    /// 0 looking, 1 no Steam, 2 no VR games, 3 listed.
    state: i32,
    search: String,
    filter: Filter,
    by_played: bool,
    toasts: i32,
}

fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

fn show_rows(ui: &fov_ui::MainWindow, app: &App, rows: &VecModel<fov_ui::GameRow>) {
    let list = games::rows(&app.entries, &app.settings, &app.search, app.filter, app.by_played, now());
    rows.set_vec(
        list.into_iter()
            .map(|r| fov_ui::GameRow {
                id: r.id,
                header: r.header,
                name: SharedString::from(r.name),
                api: SharedString::from(r.api),
                badge: SharedString::from(r.badge),
                badge_kind: r.badge_kind,
                detail: SharedString::from(r.detail),
                action: SharedString::from(r.action),
                primary: r.primary,
                can_act: r.can_act,
                busy: r.busy,
            })
            .collect::<Vec<_>>(),
    );
    ui.set_games_state(app.state);
    ui.set_games_note(games::note(&app.entries, &app.settings).into());
}

fn toast(ui: &fov_ui::MainWindow, app: &mut App, text: String) {
    app.toasts += 1;
    ui.set_toast(text.into());
    ui.set_toast_count(app.toasts);
}

pub fn run() -> Result<(), String> {
    let settings = AppSettings::load();
    let shm = fov_shm::Mapping::open()?;
    shm.write_settings(&settings.to_shm());
    let ui = fov_ui::MainWindow::new().map_err(|e| e.to_string())?;
    ui.global::<fov_ui::Mode>().set_reduce_motion(!fov_shm::sys::animations_enabled());
    ui.global::<fov_ui::Mode>().set_theme(i32::try_from(settings.theme).unwrap_or(0));
    ui.set_enabled(settings.enabled);
    ui.set_mode(i32::from(settings.eye_tracked));
    ui.set_preset(i32::try_from(settings.preset).unwrap_or(1));
    ui.set_tint(settings.debug_tint);
    ui.set_version(env!("CARGO_PKG_VERSION").into());
    let app = Rc::new(RefCell::new(App {
        shm,
        settings,
        entries: vec![],
        state: 0,
        search: String::new(),
        filter: Filter::All,
        by_played: false,
        toasts: 0,
    }));
    let rows = Rc::new(VecModel::<fov_ui::GameRow>::default());
    ui.set_games(ModelRc::from(Rc::clone(&rows)));
    let (tx, rx) = mpsc::channel::<Done>();

    // Settings: saved on every change and live in every running game at once.
    let save = {
        let (ui, app, rows) = (ui.as_weak(), Rc::clone(&app), Rc::clone(&rows));
        move || {
            let Some(ui) = ui.upgrade() else { return };
            let mut a = app.borrow_mut();
            a.settings.enabled = ui.get_enabled();
            a.settings.eye_tracked = ui.get_mode() == 1;
            a.settings.preset = u32::try_from(ui.get_preset()).unwrap_or(fov_shm::preset::MEDIUM);
            a.settings.debug_tint = ui.get_tint();
            a.settings.theme = u32::try_from(ui.global::<fov_ui::Mode>().get_theme()).unwrap_or(0);
            a.shm.write_settings(&a.settings.to_shm());
            if let Err(e) = a.settings.save() {
                toast(&ui, &mut a, format!("Settings not saved: {e}"));
            }
            show_rows(&ui, &a, &rows);
        }
    };
    let save = Rc::new(save);
    ui.on_changed({
        let save = Rc::clone(&save);
        move || save()
    });
    ui.on_theme_picked({
        let (ui, save) = (ui.as_weak(), Rc::clone(&save));
        move |i| {
            if let Some(ui) = ui.upgrade() {
                ui.global::<fov_ui::Mode>().set_theme(i);
            }
            save();
        }
    });
    ui.on_export_logs({
        let (ui, tx) = (ui.as_weak(), tx.clone());
        move || {
            if let Some(ui) = ui.upgrade() {
                ui.set_exporting(true);
            }
            let tx = tx.clone();
            std::thread::spawn(move || {
                let _ = tx.send(Done::Export(export_logs()));
            });
        }
    });

    // Games list: search, filter and sort are re-applied on each change; the list itself is scanned once.
    let refilter = {
        let (ui, app, rows) = (ui.as_weak(), Rc::clone(&app), Rc::clone(&rows));
        move || {
            if let Some(ui) = ui.upgrade() {
                let mut a = app.borrow_mut();
                a.search = ui.get_search().to_string();
                a.filter = Filter::from_index(ui.get_filter());
                a.by_played = ui.get_sort() == 1;
                show_rows(&ui, &a, &rows);
            }
        }
    };
    let refilter = Rc::new(refilter);
    ui.on_search_edited({
        let f = Rc::clone(&refilter);
        move |_| f()
    });
    ui.on_filter_picked({
        let (ui, f) = (ui.as_weak(), Rc::clone(&refilter));
        move |i| {
            if let Some(ui) = ui.upgrade() {
                ui.set_filter(i);
            }
            f();
        }
    });
    ui.on_sort_picked({
        let (ui, f) = (ui.as_weak(), Rc::clone(&refilter));
        move |i| {
            if let Some(ui) = ui.upgrade() {
                ui.set_sort(i);
            }
            f();
        }
    });
    ui.on_game_action({
        let (ui, app, rows, tx) = (ui.as_weak(), Rc::clone(&app), Rc::clone(&rows), tx.clone());
        move |id| {
            let Some(ui) = ui.upgrade() else { return };
            let Ok(id) = usize::try_from(id) else { return };
            let mut a = app.borrow_mut();
            let settings = a.settings.clone();
            let Some(e) = a.entries.get_mut(id).filter(|e| !e.busy && e.game.block.is_none()) else { return };
            let name = e.game.name.clone();
            if e.game.openvr.is_empty() {
                // OpenXR: a per-game switch in the settings, no files touched.
                let exes = e.game.exes.clone();
                let off = exes.iter().any(|x| settings.off_games.contains(x));
                if off {
                    a.settings.off_games.retain(|x| !exes.contains(x));
                } else {
                    a.settings.off_games.extend(exes);
                }
                let msg = if off {
                    format!("{name}: on from its next start")
                } else {
                    format!("{name}: off from its next start")
                };
                let msg = match a.settings.save() {
                    Ok(()) => msg,
                    Err(e) => format!("Not saved: {e}"),
                };
                toast(&ui, &mut a, msg);
            } else {
                // OpenVR: files move in the game folder, off the UI thread.
                e.busy = true;
                let (dlls, on) = (e.game.openvr.clone(), e.vr == State::On);
                let tx = tx.clone();
                std::thread::spawn(move || {
                    let result = if on {
                        openvr::disable(&dlls).map(|()| format!("{name}: disabled, its own openvr_api.dll is back"))
                    } else {
                        openvr::enable(&dlls, &proxy_dll())
                            .map(|()| format!("{name}: enabled. Start the game to use it"))
                    };
                    let _ = tx.send(Done::Action { id, result: result.map_err(|e| format!("{name}: {e}")) });
                });
            }
            show_rows(&ui, &a, &rows);
        }
    });
    std::thread::spawn({
        let tx = tx.clone();
        move || {
            let profiles = steam::Profiles::load(&app_dir());
            let _ = tx.send(Done::Scan(steam::scan(&profiles)));
        }
    });

    let tray = tray::Tray::new()?;
    let timer = slint::Timer::default();
    timer.start(slint::TimerMode::Repeated, Duration::from_millis(250), {
        let (ui, app, rows) = (ui.as_weak(), Rc::clone(&app), Rc::clone(&rows));
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
            let mut a = app.borrow_mut();
            while let Ok(done) = rx.try_recv() {
                match done {
                    Done::Scan(Scan::NoSteam) => a.state = 1,
                    Done::Scan(Scan::Games(found)) => {
                        let records = openvr::load();
                        a.state = if found.is_empty() { 2 } else { 3 };
                        a.entries = found
                            .into_iter()
                            .map(|game| {
                                let vr = openvr::state(&game.openvr, &records);
                                Entry { game, vr, busy: false }
                            })
                            .collect();
                    }
                    Done::Action { id, result } => {
                        let records = openvr::load();
                        if let Some(e) = a.entries.get_mut(id) {
                            e.busy = false;
                            e.vr = openvr::state(&e.game.openvr, &records);
                        }
                        let msg = result.unwrap_or_else(|e| {
                            // A running game holds its DLL open.
                            if e.contains("os error 32") { format!("{e}. Close the game and try again") } else { e }
                        });
                        toast(&ui, &mut a, msg);
                    }
                    Done::Export(result) => {
                        ui.set_exporting(false);
                        let msg = match result {
                            Ok(p) => {
                                format!("Saved {}", p.file_name().map(|f| f.to_string_lossy()).unwrap_or_default())
                            }
                            Err(e) => format!("Export failed: {e}"),
                        };
                        toast(&ui, &mut a, msg);
                    }
                }
                show_rows(&ui, &a, &rows);
            }
            let slots: Vec<_> = a.shm.stats().into_iter().map(|(pid, s)| (fov_shm::sys::alive(pid), s)).collect();
            let r = readout(&slots);
            ui.set_status(r.text.into());
            ui.set_saved_ms(r.ms);
            ui.set_saved_pct(r.pct);
            ui.set_saved_valid(r.valid);
        }
    });
    show_rows(&ui, &app.borrow(), &rows);
    ui.show().map_err(|e| e.to_string())?;
    // Closing the window hides it; the app lives in the tray until Quit.
    slint::run_event_loop_until_quit().map_err(|e| e.to_string())
}
