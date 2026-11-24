// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! Per-game OpenXR on Linux: a Proton game reads the registry of its own Wine prefix, so our implicit layer is
//! registered there (HKCU\Software\Khronos\OpenXR\1\ApiLayers\Implicit in steamapps/compatdata/<appid>/pfx/user.reg),
//! pointing at the manifest through Wine's Z: drive.
//!
//! Undoing it: while Wine has not rewritten user.reg since, the saved original goes back byte for byte; after the
//! game ran (Wine rewrites the whole file on exit), only our value is taken out, and the key too if we added it.
//! Wine keeps the registry in memory while the prefix runs and writes it back on exit, so a running prefix is never
//! edited.
//!
//! Flatpak and Snap Steam can't see FoveaXR's install folder: their games get a copy of the layer and the profiles in
//! the sandbox's own folder, made on enable, updated with the app, and removed once no game there uses it.

use std::path::{Path, PathBuf};

use crate::openvr::{State, hash};
use crate::platform::Sandbox;

const KEY: &str = r"Software\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
const BACKUP: &str = "user.reg.fov-backup";

#[derive(Clone, Debug, PartialEq)]
pub struct Install {
    pub reg: PathBuf,
    /// The value we added: the manifest's path as the game sees it (empty in records from before it was kept).
    pub name: String,
    /// Hash of user.reg as we left it: unchanged since means the backup can go back as it is.
    pub written: u64,
    /// We added the key (it was not there before), so it goes when our value goes.
    pub created_key: bool,
}

/// A value name in Wine's .reg text: backslashes and quotes escaped.
fn quoted(name: &str) -> String {
    format!("\"{}\"", name.replace('\\', "\\\\").replace('"', "\\\""))
}

/// Windows FILETIME (100 ns since 1601) for `#time=` lines.
fn filetime(unix: u64) -> u64 {
    (unix + 11_644_473_600) * 10_000_000
}

/// user.reg with our value added (and the key if missing). Returns the text and whether the key was created.
pub fn add_value(text: &str, name: &str, now: u64) -> (String, bool) {
    let line = format!("{}=dword:00000000", quoted(name));
    let header = format!("[{KEY}]");
    let lines: Vec<&str> = text.split('\n').collect();
    if let Some(at) = lines.iter().position(|l| l.starts_with(&header)) {
        if lines.iter().any(|l| l.trim_end() == line) {
            return (text.to_owned(), false);
        }
        // After the key line and its #time line.
        let mut insert = at + 1;
        if lines.get(insert).is_some_and(|l| l.starts_with("#time=")) {
            insert += 1;
        }
        let mut out: Vec<String> = lines.iter().map(|l| (*l).to_owned()).collect();
        out.insert(insert, line);
        return (out.join("\n"), false);
    }
    let mut out = text.trim_end_matches('\n').to_owned();
    out.push_str(&format!("\n\n{header} {now}\n#time={:x}\n{line}\n", filetime(now)));
    (out, true)
}

/// user.reg with our value taken out (and the key, if we created it and nothing else is in it).
pub fn remove_value(text: &str, name: &str, created_key: bool) -> String {
    let line = format!("{}=dword:00000000", quoted(name));
    let header = format!("[{KEY}]");
    let mut out: Vec<&str> = text.split('\n').filter(|l| l.trim_end() != line).collect();
    if created_key && let Some(at) = out.iter().position(|l| l.starts_with(&header)) {
        let end = out[at + 1..].iter().position(|l| l.starts_with('[')).map_or(out.len(), |e| at + 1 + e);
        let body_empty = out[at + 1..end].iter().all(|l| l.trim().is_empty() || l.starts_with('#'));
        if body_empty {
            // The section and the blank line before it, as add_value wrote them.
            let start = if at > 0 && out[at - 1].trim().is_empty() { at - 1 } else { at };
            let keep_blank = end < out.len();
            out.drain(start..if keep_blank { end - 1 } else { end });
        }
    }
    let mut s = out.join("\n");
    if created_key && !s.ends_with('\n') {
        s.push('\n');
    }
    s
}

fn records_path() -> Option<PathBuf> {
    crate::platform::config_dir().map(|d| d.join("proton_layers.toml"))
}

pub fn parse(text: &str) -> Vec<Install> {
    let mut out = Vec::new();
    let mut cur: Option<Install> = None;
    for line in text.lines() {
        if line.trim() == "[[layer]]" {
            out.extend(cur.take());
            cur = Some(Install { reg: PathBuf::new(), name: String::new(), written: 0, created_key: false });
            continue;
        }
        let (Some(c), Some((k, v))) = (cur.as_mut(), line.split_once('=')) else { continue };
        let v = v.trim().trim_matches('"');
        match k.trim() {
            "reg" => c.reg = PathBuf::from(v.replace("\\\\", "\\")),
            "name" => c.name = v.replace("\\\\", "\\"),
            "written" => c.written = u64::from_str_radix(v, 16).unwrap_or(0),
            "created_key" => c.created_key = v == "true",
            _ => {}
        }
    }
    out.extend(cur);
    out
}

pub fn render(records: &[Install]) -> String {
    records
        .iter()
        .map(|r| {
            format!(
                "[[layer]]\nreg = \"{}\"\nname = \"{}\"\nwritten = \"{:016x}\"\ncreated_key = {}\n",
                r.reg.display().to_string().replace('\\', "\\\\"),
                r.name.replace('\\', "\\\\"),
                r.written,
                r.created_key
            )
        })
        .collect::<Vec<_>>()
        .join("\n")
}

pub fn load() -> Vec<Install> {
    records_path().and_then(|p| std::fs::read_to_string(p).ok()).map(|t| parse(&t)).unwrap_or_default()
}

fn save(records: &[Install]) -> Result<(), String> {
    let p = records_path().ok_or("no config folder")?;
    if records.is_empty() {
        return match std::fs::remove_file(&p) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(e.to_string()),
            _ => Ok(()),
        };
    }
    if let Some(d) = p.parent() {
        std::fs::create_dir_all(d).map_err(|e| e.to_string())?;
    }
    std::fs::write(p, render(records)).map_err(|e| e.to_string())
}

/// The WINEPREFIX values a process of this prefix may have: its path, or the path its sandbox sees (`inside`), with
/// or without a final slash.
fn prefix_vars(pfx: &Path, inside: Option<PathBuf>) -> Vec<String> {
    std::iter::once(pfx.to_path_buf())
        .chain(inside)
        .flat_map(|p| [format!("WINEPREFIX={}", p.display()), format!("WINEPREFIX={}/", p.display())])
        .collect()
}

/// Whether a Wine process of this prefix runs (it would overwrite our edit when it exits). A game of Flatpak Steam
/// names its prefix as the sandbox sees it.
fn running(pfx: &Path) -> bool {
    if cfg!(not(unix)) {
        return false;
    }
    let want = prefix_vars(pfx, Sandbox::ALL.into_iter().find_map(|s| s.seen_inside(pfx)));
    let Ok(procs) = std::fs::read_dir("/proc") else { return false };
    procs.flatten().any(|p| {
        std::fs::read(p.path().join("environ"))
            .is_ok_and(|env| env.split(|b| *b == 0).any(|v| want.iter().any(|w| v == w.as_bytes())))
    })
}

pub fn state(pfx: &Path, manifest: &str, records: &[Install]) -> State {
    let reg = pfx.join("user.reg");
    let Ok(text) = std::fs::read_to_string(&reg) else { return State::Unavailable };
    let line = format!("{}=dword:00000000", quoted(manifest));
    let present = text.lines().any(|l| l.trim_end() == line);
    match (present, records.iter().any(|r| r.reg == reg)) {
        (true, _) => State::On,
        // Recorded but gone: the prefix was reset or recreated.
        (false, true) => State::NeedsReenable,
        (false, false) => State::Off,
    }
}

pub fn enable(pfx: &Path, manifest: &str, now: u64) -> Result<(), String> {
    let reg = pfx.join("user.reg");
    let Ok(original) = std::fs::read(&reg) else {
        return Err("start the game once with Proton first, so it has a Wine prefix".into());
    };
    if running(pfx) {
        return Err("close the game first".into());
    }
    let mut records = load();
    records.retain(|r| r.reg != reg);
    let (text, created_key) = add_value(&String::from_utf8_lossy(&original), manifest, now);
    std::fs::write(pfx.join(BACKUP), &original).map_err(|e| e.to_string())?;
    std::fs::write(&reg, text.as_bytes()).map_err(|e| e.to_string())?;
    records.push(Install { reg, name: manifest.to_owned(), written: hash(text.as_bytes()), created_key });
    save(&records)
}

pub fn disable(pfx: &Path, manifest: &str) -> Result<(), String> {
    let reg = pfx.join("user.reg");
    let mut records = load();
    let Some(r) = records.iter().find(|r| r.reg == reg).cloned() else { return Ok(()) };
    if running(pfx) {
        return Err("close the game first".into());
    }
    restore(&r, manifest)?;
    records.retain(|x| x.reg != reg);
    save(&records)
}

/// `manifest`: the value to take out when the record does not name it.
fn restore(r: &Install, manifest: &str) -> Result<(), String> {
    let manifest = if r.name.is_empty() { manifest } else { &r.name };
    let backup = r.reg.with_file_name(BACKUP);
    match std::fs::read(&r.reg) {
        // Untouched since: the original, byte for byte.
        Ok(now) if hash(&now) == r.written && backup.exists() => {
            std::fs::rename(&backup, &r.reg).map_err(|e| e.to_string())?;
        }
        Ok(now) => {
            let text = remove_value(&String::from_utf8_lossy(&now), manifest, r.created_key);
            std::fs::write(&r.reg, text).map_err(|e| e.to_string())?;
            let _ = std::fs::remove_file(&backup);
        }
        // The prefix is gone: nothing left to undo.
        Err(_) => {
            let _ = std::fs::remove_file(&backup);
        }
    }
    Ok(())
}

/// Every prefix registration undone (the uninstaller runs this through `--uninstall`). A prefix that cannot be
/// undone now (its game runs) does not stop the others; the first such error is returned.
pub fn disable_all(manifest: &str) -> Result<(), String> {
    let mut result = Ok(());
    for r in load() {
        if let Some(pfx) = r.reg.parent() {
            result = result.and(disable(pfx, manifest));
        }
    }
    result
}

/// Copies FoveaXR's layer files, (source, path in the copy), into a sandbox's folder. Each is written beside its old
/// copy and renamed over it, so a game running the old one keeps it; a file already the same is left alone.
pub fn copy_into(dir: &Path, files: &[(PathBuf, PathBuf)]) -> Result<(), String> {
    for (src, rel) in files {
        let bytes = std::fs::read(src).map_err(|e| format!("{}: {e}", src.display()))?;
        let dest = dir.join(rel);
        if std::fs::read(&dest).is_ok_and(|b| b == bytes) {
            continue;
        }
        let mut tmp = dest.clone().into_os_string();
        tmp.push(".fov-new");
        std::fs::create_dir_all(dest.parent().unwrap_or(dir))
            .and_then(|()| std::fs::write(&tmp, &bytes))
            .and_then(|()| std::fs::rename(&tmp, &dest))
            .map_err(|e| format!("{}: {e}", dest.display()))?;
    }
    Ok(())
}

/// After the app was updated: each sandbox's copy gets the new files.
pub fn refresh_copies(files: &[(PathBuf, PathBuf)]) {
    for dir in Sandbox::ALL.into_iter().filter_map(Sandbox::data_dir).filter(|d| d.is_dir()) {
        let _ = copy_into(&dir, files);
    }
}

/// Each sandbox's copy goes once no game of its Steam has FoveaXR enabled (a prefix's layer, an OpenVR proxy).
pub fn prune_copies() -> Result<(), String> {
    let copies: Vec<(Sandbox, PathBuf)> =
        Sandbox::ALL.into_iter().filter_map(|s| Some((s, s.data_dir().filter(|d| d.exists())?))).collect();
    if copies.is_empty() {
        return Ok(());
    }
    let used: Vec<Sandbox> = load()
        .into_iter()
        .map(|r| r.reg)
        .chain(crate::openvr::load().into_iter().map(|r| r.dll))
        .filter_map(|p| crate::steam::sandbox_of(&p))
        .collect();
    let mut result = Ok(());
    for (_, dir) in copies.into_iter().filter(|(s, _)| !used.contains(s)) {
        result = result.and(std::fs::remove_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display())));
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    const NAME: &str = r"Z:\home\u\.local\share\foveaxr\fov_openxr_layer.json";
    const REG: &str = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n#arch=win64\n\n[Software\\\\Wine] 1700000000\n#time=1da0000000000000\n\"Version\"=\"win10\"\n";

    #[test]
    fn add_then_remove_is_the_original() {
        let (added, created) = add_value(REG, NAME, 1_732_000_000);
        assert!(created);
        assert!(added.contains("[Software\\\\Khronos\\\\OpenXR\\\\1\\\\ApiLayers\\\\Implicit] 1732000000"));
        assert!(
            added.contains("\"Z:\\\\home\\\\u\\\\.local\\\\share\\\\foveaxr\\\\fov_openxr_layer.json\"=dword:00000000")
        );
        assert_eq!(remove_value(&added, NAME, true), REG);
        // Adding twice changes nothing.
        assert_eq!(add_value(&added, NAME, 5).0, added);
    }

    #[test]
    fn existing_key_keeps_other_layers() {
        let with_key = format!("{REG}\n[{KEY}] 1700000001\n#time=1da\n\"Z:\\\\other.json\"=dword:00000000\n");
        let (added, created) = add_value(&with_key, NAME, 9);
        assert!(!created);
        let lines: Vec<&str> = added.lines().collect();
        let at = lines.iter().position(|l| l.starts_with(&format!("[{KEY}]"))).unwrap_or(0);
        assert!(lines[at + 2].starts_with("\"Z:\\\\home"));
        assert_eq!(remove_value(&added, NAME, false), with_key);
    }

    #[test]
    fn records_round_trip() {
        let r = vec![Install {
            reg: PathBuf::from("/home/u/steamapps/compatdata/1/pfx/user.reg"),
            name: NAME.into(),
            written: 7,
            created_key: true,
        }];
        assert_eq!(parse(&render(&r)), r);
        // A record from before the value was kept: the caller's manifest is taken out.
        let old = parse("[[layer]]\nreg = \"/p/user.reg\"\nwritten = \"07\"\ncreated_key = true\n");
        assert_eq!(old.first().map(|r| r.name.as_str()), Some(""));
    }

    #[test]
    fn a_flatpak_prefix_is_named_as_the_sandbox_sees_it() {
        let host = Path::new("/home/u/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/compatdata/1/pfx");
        let inside = PathBuf::from("/home/u/.local/share/Steam/steamapps/compatdata/1/pfx");
        let vars = prefix_vars(host, Some(inside));
        assert!(vars.contains(&format!("WINEPREFIX={}", host.display())));
        assert!(vars.contains(&"WINEPREFIX=/home/u/.local/share/Steam/steamapps/compatdata/1/pfx/".to_owned()));
        assert_eq!(prefix_vars(Path::new("/p"), None), ["WINEPREFIX=/p", "WINEPREFIX=/p/"]);
    }

    #[test]
    fn copies_replace_only_what_changed() -> Result<(), String> {
        let dir = std::env::temp_dir().join(format!("fov-proton-copy-{}", std::process::id()));
        let (src, copy) = (dir.join("src"), dir.join("copy"));
        std::fs::create_dir_all(src.join("profiles")).map_err(|e| e.to_string())?;
        std::fs::write(src.join("layer.dll"), b"1.0").map_err(|e| e.to_string())?;
        std::fs::write(src.join("profiles/a.toml"), b"a").map_err(|e| e.to_string())?;
        let files = [
            (src.join("layer.dll"), PathBuf::from("layer.dll")),
            (src.join("profiles/a.toml"), PathBuf::from("profiles/a.toml")),
        ];
        copy_into(&copy, &files)?;
        assert_eq!(std::fs::read(copy.join("profiles/a.toml")).ok().as_deref(), Some(&b"a"[..]));
        std::fs::write(src.join("layer.dll"), b"1.1").map_err(|e| e.to_string())?;
        copy_into(&copy, &files)?;
        assert_eq!(std::fs::read(copy.join("layer.dll")).ok().as_deref(), Some(&b"1.1"[..]));
        assert!(!copy.join("layer.dll.fov-new").exists());
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn enable_then_disable_restores_the_prefix() -> Result<(), String> {
        let _config = crate::platform::CONFIG_DIR_LOCK.lock().map_err(|e| e.to_string())?;
        let pfx = std::env::temp_dir().join(format!("fov-proton-pfx-{}", std::process::id()));
        std::fs::create_dir_all(&pfx).map_err(|e| e.to_string())?;
        std::fs::write(pfx.join("user.reg"), REG).map_err(|e| e.to_string())?;
        // Records go to the real config file: keep any the user has, and no folder left behind.
        let before = load();
        let had_dir = crate::platform::config_dir().is_some_and(|d| d.exists());
        enable(&pfx, NAME, 1_732_000_000)?;
        assert_eq!(state(&pfx, NAME, &load()), State::On);
        disable(&pfx, NAME)?;
        assert_eq!(std::fs::read_to_string(pfx.join("user.reg")).ok().as_deref(), Some(REG));
        // Wine rewrote user.reg since: the value the record names is taken out, whatever the caller passes now.
        enable(&pfx, NAME, 1_732_000_000)?;
        let ran = std::fs::read_to_string(pfx.join("user.reg")).map_err(|e| e.to_string())? + "\n[Game] 1\n";
        std::fs::write(pfx.join("user.reg"), ran).map_err(|e| e.to_string())?;
        disable(&pfx, r"Z:\elsewhere.json")?;
        let text = std::fs::read_to_string(pfx.join("user.reg")).map_err(|e| e.to_string())?;
        assert!(!text.contains("fov_openxr_layer") && text.contains("[Game] 1"));
        assert!(!pfx.join(BACKUP).exists());
        assert_eq!(state(&pfx, NAME, &load()), State::Off);
        assert_eq!(load(), before);
        if !had_dir && let Some(d) = crate::platform::config_dir() {
            let _ = std::fs::remove_dir(d);
        }
        let _ = std::fs::remove_dir_all(&pfx);
        Ok(())
    }

    #[test]
    fn no_prefix_yet() {
        let dir = std::env::temp_dir().join(format!("fov-proton-none-{}", std::process::id()));
        assert_eq!(state(&dir, NAME, &[]), State::Unavailable);
        assert!(enable(&dir, NAME, 0).is_err_and(|e| e.contains("start the game once")));
    }
}
