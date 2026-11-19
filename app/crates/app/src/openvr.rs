//! Per-game OpenVR enable (SPEC 12): the game's openvr_api.dll is renamed to openvr_api.fov-orig.dll and our proxy
//! takes its place. Each install is recorded with both files' hashes in %APPDATA%\FoveatedVR\openvr_installs.toml,
//! so disabling (and uninstalling) puts the original back exactly, and a game update that replaced the DLL is noticed.

use std::path::{Path, PathBuf};

pub const ORIGINAL: &str = "openvr_api.fov-orig.dll";

#[derive(Clone, Debug, PartialEq)]
pub struct Install {
    pub dll: PathBuf,
    pub original: u64,
    pub proxy: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Off,
    On,
    /// The game was updated and its new openvr_api.dll replaced ours.
    NeedsReenable,
}

/// FNV-1a: tells our proxy and the game's original apart and notices a replaced file; not a security measure.
pub fn hash(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325, |h, b| (h ^ u64::from(*b)).wrapping_mul(0x0100_0000_01b3))
}

fn hash_file(p: &Path) -> Option<u64> {
    std::fs::read(p).ok().map(|b| hash(&b))
}

fn records_path() -> Option<PathBuf> {
    crate::settings::dir().map(|d| d.join("openvr_installs.toml"))
}

pub fn parse(text: &str) -> Vec<Install> {
    let mut out = Vec::new();
    let mut cur: Option<Install> = None;
    for line in text.lines() {
        if line.trim() == "[[install]]" {
            out.extend(cur.take());
            cur = Some(Install { dll: PathBuf::new(), original: 0, proxy: 0 });
            continue;
        }
        let (Some(c), Some((k, v))) = (cur.as_mut(), line.split_once('=')) else { continue };
        let v = v.trim().trim_matches('"');
        match k.trim() {
            "dll" => c.dll = PathBuf::from(v.replace("\\\\", "\\")),
            "original" => c.original = u64::from_str_radix(v, 16).unwrap_or(0),
            "proxy" => c.proxy = u64::from_str_radix(v, 16).unwrap_or(0),
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
                "[[install]]\ndll = \"{}\"\noriginal = \"{:016x}\"\nproxy = \"{:016x}\"\n",
                r.dll.display().to_string().replace('\\', "\\\\"),
                r.original,
                r.proxy
            )
        })
        .collect::<Vec<_>>()
        .join("\n")
}

pub fn load() -> Vec<Install> {
    records_path().and_then(|p| std::fs::read_to_string(p).ok()).map(|t| parse(&t)).unwrap_or_default()
}

fn save(records: &[Install]) -> Result<(), String> {
    let p = records_path().ok_or("no APPDATA folder")?;
    if !records.is_empty()
        && let Some(d) = p.parent()
    {
        std::fs::create_dir_all(d).map_err(|e| e.to_string())?;
    }
    if records.is_empty() {
        // Nothing installed: no file, so uninstalling leaves no trace.
        return match std::fs::remove_file(&p) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(e.to_string()),
            _ => Ok(()),
        };
    }
    std::fs::write(p, render(records)).map_err(|e| e.to_string())
}

pub fn state(dlls: &[PathBuf], records: &[Install]) -> State {
    let mine: Vec<&Install> = records.iter().filter(|r| dlls.contains(&r.dll)).collect();
    if mine.is_empty() {
        State::Off
    } else if mine.iter().all(|r| hash_file(&r.dll) == Some(r.proxy)) {
        State::On
    } else {
        State::NeedsReenable
    }
}

/// Puts the proxy in front of each of the game's DLLs. On a failure the files done so far are put back.
pub fn enable(dlls: &[PathBuf], proxy: &Path) -> Result<(), String> {
    let proxy_bytes = std::fs::read(proxy).map_err(|e| format!("{}: {e}", proxy.display()))?;
    let proxy_hash = hash(&proxy_bytes);
    let mut records = load();
    disable(dlls)?; // a re-enable after a game update starts from the game's current files
    records.retain(|r| !dlls.contains(&r.dll));
    let mut done = Vec::new();
    for dll in dlls {
        let orig = dll.with_file_name(ORIGINAL);
        let result = (|| {
            // Already ours with the original beside it (its record lost): record it again, move nothing.
            if hash_file(dll) == Some(proxy_hash)
                && let Some(original) = hash_file(&orig)
            {
                return Ok(Install { dll: dll.clone(), original, proxy: proxy_hash });
            }
            let original = hash_file(dll).ok_or("cannot read openvr_api.dll")?;
            std::fs::rename(dll, &orig).map_err(|e| e.to_string())?;
            if let Err(e) = std::fs::write(dll, &proxy_bytes) {
                let _ = std::fs::rename(&orig, dll);
                return Err(e.to_string());
            }
            Ok::<_, String>(Install { dll: dll.clone(), original, proxy: proxy_hash })
        })();
        match result {
            Ok(r) => done.push(r),
            Err(e) => {
                for r in &done {
                    restore(r);
                }
                return Err(format!("{}: {e}", dll.display()));
            }
        }
    }
    records.extend(done);
    save(&records)
}

/// Puts the original back. If the game replaced our DLL with a new original, that one stays and only our renamed
/// old copy goes.
fn restore(r: &Install) {
    let orig = r.dll.with_file_name(ORIGINAL);
    if hash_file(&r.dll) == Some(r.proxy) {
        if std::fs::remove_file(&r.dll).is_ok() {
            let _ = std::fs::rename(&orig, &r.dll);
        }
    } else {
        let _ = std::fs::remove_file(&orig);
    }
}

pub fn disable(dlls: &[PathBuf]) -> Result<(), String> {
    let mut records = load();
    for r in records.iter().filter(|r| dlls.contains(&r.dll)) {
        restore(r);
    }
    records.retain(|r| !dlls.contains(&r.dll));
    save(&records)
}

/// Every install undone (the uninstaller runs this through `--uninstall`).
pub fn disable_all() -> Result<(), String> {
    let all: Vec<PathBuf> = load().into_iter().map(|r| r.dll).collect();
    disable(&all)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn records_round_trip() {
        let r = vec![
            Install { dll: PathBuf::from(r"D:\Steam\common\Game\openvr_api.dll"), original: 0x1234, proxy: u64::MAX },
            Install { dll: PathBuf::from(r"C:\x\openvr_api.dll"), original: 1, proxy: 2 },
        ];
        assert_eq!(parse(&render(&r)), r);
        assert!(parse("").is_empty());
    }

    #[test]
    fn hash_tells_files_apart() {
        assert_ne!(hash(b"proxy"), hash(b"original"));
        assert_eq!(hash(b""), 0xcbf2_9ce4_8422_2325);
    }

    #[test]
    fn enable_then_disable_restores_the_folder() -> Result<(), String> {
        let dir = std::env::temp_dir().join(format!("fov-openvr-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let dll = dir.join("openvr_api.dll");
        let proxy = dir.join("proxy.dll");
        std::fs::write(&dll, b"the game's original").map_err(|e| e.to_string())?;
        std::fs::write(&proxy, b"our proxy").map_err(|e| e.to_string())?;
        // Records go to the real APPDATA file: keep any the user has, and no folder left behind.
        let before = load();
        let had_dir = crate::settings::dir().is_some_and(|d| d.exists());
        enable(std::slice::from_ref(&dll), &proxy)?;
        assert_eq!(std::fs::read(&dll).ok().as_deref(), Some(&b"our proxy"[..]));
        assert_eq!(state(std::slice::from_ref(&dll), &load()), State::On);
        disable(std::slice::from_ref(&dll))?;
        assert_eq!(std::fs::read(&dll).ok().as_deref(), Some(&b"the game's original"[..]));
        assert!(!dir.join(ORIGINAL).exists());
        assert_eq!(load(), before);
        if !had_dir && let Some(d) = crate::settings::dir() {
            let _ = std::fs::remove_dir(d);
        }
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }
}
