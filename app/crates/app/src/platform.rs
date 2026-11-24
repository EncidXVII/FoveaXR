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

/// Steam's install folders, each with the sandbox its Steam runs in: the registry on Windows; on Linux the native and
/// Debian/Ubuntu locations, then Flatpak's and Snap's (a folder seen twice through symlinks is listed once).
pub fn steam_roots() -> Vec<(PathBuf, Option<Sandbox>)> {
    #[cfg(windows)]
    return fov_shm::sys::steam_path().map(|p| (PathBuf::from(p), None)).into_iter().collect();
    #[cfg(unix)]
    {
        let Some(home) = env_path("HOME") else { return vec![] };
        let sandboxed =
            Sandbox::ALL.iter().flat_map(|s| s.steam_rels().iter().map(move |r| format!("{}/{r}", s.home_rel())));
        let mut roots: Vec<(PathBuf, Option<Sandbox>)> = Vec::new();
        for p in [".steam/steam".to_owned(), ".local/share/Steam".to_owned()].into_iter().chain(sandboxed) {
            if let Ok(real) = home.join(&p).canonicalize()
                && real.join("steamapps").is_dir()
                && !roots.iter().any(|(r, _)| *r == real)
            {
                let sandbox = Sandbox::ALL.into_iter().find(|s| s.home().is_some_and(|h| real.starts_with(h)));
                roots.push((real, sandbox));
            }
        }
        roots
    }
}

/// Steam from Flatpak or Snap (Linux). Its sandbox sees a home folder of its own and a /dev/shm of its own, so
/// FoveaXR's layer, logs and shared memory file for its games live in that home, which the host sees under the real
/// one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Sandbox {
    Flatpak,
    Snap,
}

impl Sandbox {
    pub const ALL: [Self; 2] = [Self::Flatpak, Self::Snap];

    /// Its home folder, under the real one. Flatpak Steam runs with `--persist=.`, so inside its sandbox this folder
    /// is the home folder; Snap Steam sets $HOME to it.
    const fn home_rel(self) -> &'static str {
        match self {
            Self::Flatpak => ".var/app/com.valvesoftware.Steam",
            Self::Snap => "snap/steam/common",
        }
    }

    /// Where its Steam keeps its files, under its home (older Flatpak builds used data/Steam).
    const fn steam_rels(self) -> &'static [&'static str] {
        match self {
            Self::Flatpak => &[".local/share/Steam", "data/Steam"],
            Self::Snap => &[".local/share/Steam"],
        }
    }

    /// Its home folder as the host sees it. Linux only.
    pub fn home(self) -> Option<PathBuf> {
        let home = env_path("HOME").filter(|_| cfg!(unix))?.join(self.home_rel());
        Some(home.canonicalize().unwrap_or(home))
    }

    /// Its Steam is installed.
    pub fn installed(self) -> bool {
        self.home().is_some_and(|h| self.steam_rels().iter().any(|r| h.join(r).join("steamapps").is_dir()))
    }

    /// FoveaXR's install folder in it, where the core inside looks (the XDG fallback under its home): the app copies
    /// the OpenXR layer and the profiles there.
    pub fn data_dir(self) -> Option<PathBuf> {
        self.home().map(|h| h.join(".local/share/foveaxr"))
    }

    /// Where the core inside writes its logs.
    pub fn state_dir(self) -> Option<PathBuf> {
        self.home().map(|h| h.join(".local/state/foveaxr"))
    }

    /// The shared memory file the core inside maps (its /dev/shm is not the host's).
    pub fn shm_file(self) -> Option<PathBuf> {
        self.state_dir().map(|d| d.join("FoveaXR.v1"))
    }

    /// A path its Steam wrote down (a library), as the host sees it.
    pub fn host_path(self, written: &Path) -> PathBuf {
        match (self, env_path("HOME"), self.home()) {
            (Self::Flatpak, Some(home), Some(sandbox)) => from_flatpak(written, &home, &sandbox, Path::exists),
            _ => written.to_path_buf(),
        }
    }

    /// A host path as its games see it, when they see it elsewhere (Flatpak: in its folder, under the home folder).
    pub fn seen_inside(self, host: &Path) -> Option<PathBuf> {
        match (self, env_path("HOME"), self.home()) {
            (Self::Flatpak, Some(home), Some(sandbox)) => to_flatpak(host, &home, &sandbox),
            _ => None,
        }
    }
}

/// A path Flatpak Steam wrote down, as the host sees it. It writes paths as its sandbox sees them, where its folder
/// (`sandbox`) is the home folder: a path in the home folder lies in that folder, unless it is under ~/.var itself or
/// not there (a folder the user shared with Flatpak Steam is the real one).
fn from_flatpak(p: &Path, home: &Path, sandbox: &Path, exists: impl Fn(&Path) -> bool) -> PathBuf {
    match p.strip_prefix(home) {
        Ok(rel) if !rel.starts_with(".var") && exists(&sandbox.join(rel)) => sandbox.join(rel),
        _ => p.to_path_buf(),
    }
}

/// The other way: a path in Flatpak Steam's folder as its sandbox sees it.
fn to_flatpak(p: &Path, home: &Path, sandbox: &Path) -> Option<PathBuf> {
    p.strip_prefix(sandbox).ok().map(|rel| home.join(rel))
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
    fn flatpak_paths() {
        let (home, sandbox) = (Path::new("/home/u"), Path::new("/home/u/.var/app/com.valvesoftware.Steam"));
        let there = |p: &Path| p.starts_with(sandbox.join(".local"));
        // Its own library, as it wrote it down.
        assert_eq!(
            from_flatpak(Path::new("/home/u/.local/share/Steam"), home, sandbox, there),
            sandbox.join(".local/share/Steam")
        );
        // Outside the home folder, or a home folder the user shared with it (nothing in its folder): as written.
        assert_eq!(from_flatpak(Path::new("/mnt/games"), home, sandbox, there), Path::new("/mnt/games"));
        assert_eq!(from_flatpak(Path::new("/home/u/Games"), home, sandbox, there), Path::new("/home/u/Games"));
        let pfx = sandbox.join(".local/share/Steam/steamapps/compatdata/620/pfx");
        assert_eq!(from_flatpak(&pfx, home, sandbox, |_| true), pfx);
        // A prefix in its folder, as its games see it (WINEPREFIX); one outside is the same inside.
        assert_eq!(
            to_flatpak(&pfx, home, sandbox).as_deref(),
            Some(Path::new("/home/u/.local/share/Steam/steamapps/compatdata/620/pfx"))
        );
        assert_eq!(to_flatpak(Path::new("/mnt/games/steamapps"), home, sandbox), None);
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
