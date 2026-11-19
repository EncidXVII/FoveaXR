//! Per-game OpenXR on Linux: a Proton game reads the registry of its own Wine prefix, so our implicit layer is
//! registered there (HKCU\Software\Khronos\OpenXR\1\ApiLayers\Implicit in steamapps/compatdata/<appid>/pfx/user.reg),
//! pointing at the manifest through Wine's Z: drive.
//!
//! Undoing it: while Wine has not rewritten user.reg since, the saved original goes back byte for byte; after the
//! game ran (Wine rewrites the whole file on exit), only our value is taken out, and the key too if we added it.
//! Wine keeps the registry in memory while the prefix runs and writes it back on exit, so a running prefix is never
//! edited.

use std::path::{Path, PathBuf};

use crate::openvr::{State, hash};

const KEY: &str = r"Software\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
const BACKUP: &str = "user.reg.fov-backup";

#[derive(Clone, Debug, PartialEq)]
pub struct Install {
    pub reg: PathBuf,
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
            cur = Some(Install { reg: PathBuf::new(), written: 0, created_key: false });
            continue;
        }
        let (Some(c), Some((k, v))) = (cur.as_mut(), line.split_once('=')) else { continue };
        let v = v.trim().trim_matches('"');
        match k.trim() {
            "reg" => c.reg = PathBuf::from(v.replace("\\\\", "\\")),
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
                "[[layer]]\nreg = \"{}\"\nwritten = \"{:016x}\"\ncreated_key = {}\n",
                r.reg.display().to_string().replace('\\', "\\\\"),
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

/// Whether a Wine process of this prefix runs (it would overwrite our edit when it exits).
fn running(pfx: &Path) -> bool {
    #[cfg(unix)]
    {
        let want = format!("WINEPREFIX={}", pfx.display());
        let Ok(procs) = std::fs::read_dir("/proc") else { return false };
        procs.flatten().any(|p| {
            std::fs::read(p.path().join("environ")).is_ok_and(|env| {
                env.split(|b| *b == 0).any(|v| v == want.as_bytes() || v == format!("{want}/").as_bytes())
            })
        })
    }
    #[cfg(not(unix))]
    {
        let _ = pfx;
        false
    }
}

pub fn state(pfx: &Path, manifest: &str, records: &[Install]) -> State {
    let reg = pfx.join("user.reg");
    let Ok(text) = std::fs::read_to_string(&reg) else { return State::Unavailable };
    let present = text.lines().any(|l| l.trim_end() == format!("{}=dword:00000000", quoted(manifest)));
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
    records.push(Install { reg, written: hash(text.as_bytes()), created_key });
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

fn restore(r: &Install, manifest: &str) -> Result<(), String> {
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

/// Every prefix registration undone (the uninstaller runs this through `--uninstall`).
pub fn disable_all(manifest: &str) -> Result<(), String> {
    for r in load() {
        if let Some(pfx) = r.reg.parent() {
            disable(pfx, manifest)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const NAME: &str = r"Z:\home\u\.local\share\foveatedvr\fov_openxr_layer.json";
    const REG: &str = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n#arch=win64\n\n[Software\\\\Wine] 1700000000\n#time=1da0000000000000\n\"Version\"=\"win10\"\n";

    #[test]
    fn add_then_remove_is_the_original() {
        let (added, created) = add_value(REG, NAME, 1_732_000_000);
        assert!(created);
        assert!(added.contains("[Software\\\\Khronos\\\\OpenXR\\\\1\\\\ApiLayers\\\\Implicit] 1732000000"));
        assert!(
            added.contains(
                "\"Z:\\\\home\\\\u\\\\.local\\\\share\\\\foveatedvr\\\\fov_openxr_layer.json\"=dword:00000000"
            )
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
            written: 7,
            created_key: true,
        }];
        assert_eq!(parse(&render(&r)), r);
    }

    #[test]
    fn enable_then_disable_restores_the_prefix() -> Result<(), String> {
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
