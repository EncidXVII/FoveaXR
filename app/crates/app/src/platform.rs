// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! What differs between Windows and Linux: folders, Steam's location, the desktop's motion setting, revealing a file.

use std::path::{Path, PathBuf};

fn env_path(name: &str) -> Option<PathBuf> {
    std::env::var_os(name).filter(|v| !v.is_empty()).map(PathBuf::from)
}

#[cfg(unix)]
fn xdg(var: &str, fallback: &str) -> Option<PathBuf> {
    env_path(var).or_else(|| env_path("HOME").map(|h| h.join(fallback)))
}

/// Tests that write the real config folder, and remove it when they made it, take this first: run side by side,
/// one could delete the folder while another is writing its file there.
#[cfg(test)]
pub static CONFIG_DIR_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Settings and install records: %APPDATA%\FoveaXR, or $XDG_CONFIG_HOME/foveaxr.
pub fn config_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    return env_path("APPDATA").map(|d| d.join("FoveaXR"));
    #[cfg(unix)]
    return xdg("XDG_CONFIG_HOME", ".config").map(|d| d.join("foveaxr"));
}

/// Logs (the in-game core writes them): %LOCALAPPDATA%\FoveaXR, or $XDG_STATE_HOME/foveaxr.
pub fn state_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    return env_path("LOCALAPPDATA").map(|d| d.join("FoveaXR"));
    #[cfg(unix)]
    return xdg("XDG_STATE_HOME", ".local/state").map(|d| d.join("foveaxr"));
}

/// Where Export logs puts the zip: the desktop, else the home folder.
pub fn desktop_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    let (home, desktop) = (env_path("USERPROFILE")?, None::<PathBuf>);
    #[cfg(unix)]
    let (home, desktop) = (
        env_path("HOME")?,
        std::process::Command::new("xdg-user-dir")
            .arg("DESKTOP")
            .output()
            .ok()
            .map(|o| PathBuf::from(String::from_utf8_lossy(&o.stdout).trim())),
    );
    // A desktop moved by OneDrive or policy may not be under the profile: then the home folder itself.
    Some(desktop.unwrap_or_else(|| home.join("Desktop"))).filter(|d| d.is_dir()).or(Some(home))
}

/// Shows a file in the file manager.
pub fn reveal(file: &Path) {
    #[cfg(windows)]
    let _ = std::process::Command::new("explorer").arg(format!("/select,{}", file.display())).spawn();
    #[cfg(unix)]
    if let Some(dir) = file.parent() {
        let _ = std::process::Command::new("xdg-open").arg(dir).spawn();
    }
}

/// Opens a web page in the default browser (Settings > About). Only https links, so nothing else can be started.
pub fn open_url(url: &str) {
    if url.starts_with("https://") {
        let _ = std::process::Command::new(if cfg!(windows) { "explorer" } else { "xdg-open" }).arg(url).spawn();
    }
}

/// Steam's install folders: the registry on Windows; on Linux the native, Debian/Ubuntu and Flatpak locations
/// (the same library seen twice through symlinks is listed once).
pub fn steam_roots() -> Vec<PathBuf> {
    #[cfg(windows)]
    return fov_shm::sys::steam_path().map(PathBuf::from).into_iter().collect();
    #[cfg(unix)]
    {
        let Some(home) = env_path("HOME") else { return vec![] };
        let mut roots: Vec<PathBuf> = Vec::new();
        for p in [
            ".steam/steam",
            ".local/share/Steam",
            ".var/app/com.valvesoftware.Steam/.local/share/Steam",
            ".var/app/com.valvesoftware.Steam/data/Steam",
        ] {
            if let Ok(real) = home.join(p).canonicalize()
                && real.join("steamapps").is_dir()
                && !roots.contains(&real)
            {
                roots.push(real);
            }
        }
        roots
    }
}

/// Whether OpenXR games get our layer without a step per game. Windows: the layer is registered for this user (the
/// installer does it; an app started straight from the unzipped folder has none). Linux registers it per game, in
/// each Proton prefix, and shows that per game.
pub fn openxr_layer_registered() -> bool {
    #[cfg(windows)]
    return fov_shm::sys::openxr_layer_registered("fov_openxr_layer.json");
    #[cfg(unix)]
    return true;
}

/// The desktop's "reduce animations" setting: Windows' Animation effects; GNOME's enable-animations; KDE's animation
/// speed set to instant. On when unknown.
pub fn animations_enabled() -> bool {
    #[cfg(windows)]
    return fov_shm::sys::animations_enabled();
    #[cfg(unix)]
    {
        let gnome = std::process::Command::new("gsettings")
            .args(["get", "org.gnome.desktop.interface", "enable-animations"])
            .output()
            .ok()
            .is_some_and(|o| String::from_utf8_lossy(&o.stdout).trim() == "false");
        let kde = xdg("XDG_CONFIG_HOME", ".config")
            .and_then(|d| std::fs::read_to_string(d.join("kdeglobals")).ok())
            .is_some_and(|t| t.lines().any(|l| l.replace(' ', "") == "AnimationDurationFactor=0"));
        !(gnome || kde)
    }
}

/// Held for the app's lifetime; a second copy would fight the first over settings and the tray. None when another
/// copy runs (or the lock cannot be taken).
pub fn single_instance() -> Option<std::fs::File> {
    let dir = state_dir()?;
    std::fs::create_dir_all(&dir).ok()?;
    let file = std::fs::OpenOptions::new().create(true).truncate(false).write(true).open(dir.join("app.lock")).ok()?;
    file.try_lock().ok()?;
    Some(file)
}

/// A path as a program inside Wine sees it: Z: is the Linux root.
pub fn wine_path(p: &Path) -> String {
    format!("Z:{}", p.display().to_string().replace('/', "\\"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wine_paths() {
        assert_eq!(
            wine_path(Path::new("/home/u/.local/share/foveaxr/fov_openxr_layer.json")),
            r"Z:\home\u\.local\share\foveaxr\fov_openxr_layer.json"
        );
    }

    #[test]
    fn one_instance() {
        let first = single_instance();
        assert!(first.is_some());
        assert!(single_instance().is_none());
        drop(first);
        assert!(single_instance().is_some());
    }
}
