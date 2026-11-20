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
    /// The game was updated and its new openvr_api.dll replaced ours (Proton: the prefix lost our layer).
    NeedsReenable,
    /// Cannot be enabled yet: a Proton game without a Wine prefix (never started).
    Unavailable,
}

/// FNV-1a: tells our proxy and the game's original apart and notices a replaced file; not a security measure.
pub fn hash(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325, |h, b| (h ^ u64::from(*b)).wrapping_mul(0x0100_0000_01b3))
}

fn hash_file(p: &Path) -> Option<u64> {
    std::fs::read(p).ok().map(|b| hash(&b))
}

/// Whether a DLL is a proxy of ours, of any version: each carries the name of the original it forwards to (UTF-16,
/// as the Windows loader takes it).
fn is_proxy(bytes: &[u8]) -> bool {
    let marker: Vec<u8> = ORIGINAL.encode_utf16().flat_map(u16::to_le_bytes).collect();
    bytes.windows(marker.len()).any(|w| w == marker)
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
            let current = std::fs::read(dll).map_err(|e| e.to_string())?;
            // Already ours (its record lost): the original beside it stays the original. Moving ours there would
            // lose the game's own DLL and leave the proxy forwarding to itself.
            if hash(&current) == proxy_hash || is_proxy(&current) {
                let original = hash_file(&orig)
                    .ok_or("the game's own openvr_api.dll is missing; verify the game's files in Steam")?;
                if hash(&current) != proxy_hash {
                    std::fs::write(dll, &proxy_bytes).map_err(|e| e.to_string())?;
                }
                return Ok(Install { dll: dll.clone(), original, proxy: proxy_hash });
            }
            let original = hash(&current);
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
                    let _ = restore(r);
                }
                return Err(format!("{}: {e}", dll.display()));
            }
        }
    }
    records.extend(done);
    save(&records)
}

/// Puts the original back. If the game replaced our DLL with a new original, that one stays and only our renamed
/// old copy goes. An error (a running game holds its DLLs) leaves both files as they were.
fn restore(r: &Install) -> std::io::Result<()> {
    use std::io::ErrorKind::NotFound;
    let orig = r.dll.with_file_name(ORIGINAL);
    let drop_orig = || match std::fs::remove_file(&orig) {
        Err(e) if e.kind() != NotFound => Err(e),
        _ => Ok(()),
    };
    match std::fs::read(&r.dll) {
        // One step: the original takes the proxy's place, or nothing changed.
        Ok(b) if (hash(&b) == r.proxy || is_proxy(&b)) && orig.exists() => std::fs::rename(&orig, &r.dll),
        Ok(b) if hash(&b) == r.proxy || is_proxy(&b) => std::fs::remove_file(&r.dll),
        Ok(_) => drop_orig(),
        // The game is gone, or it removed the DLL.
        Err(e) if e.kind() == NotFound => drop_orig(),
        Err(e) => Err(e),
    }
}

/// A game whose files could not be put back keeps its record, so it stays listed as on and a later try (or the
/// uninstaller) finds it again.
pub fn disable(dlls: &[PathBuf]) -> Result<(), String> {
    let mut records = load();
    let mut failed = None;
    records.retain(|r| {
        if !dlls.contains(&r.dll) {
            return true;
        }
        match restore(r) {
            Ok(()) => false,
            Err(e) => {
                failed.get_or_insert(format!("{}: {e}", r.dll.display()));
                true
            }
        }
    });
    save(&records)?;
    failed.map_or(Ok(()), Err)
}

/// Every install undone (the uninstaller runs this through `--uninstall`).
pub fn disable_all() -> Result<(), String> {
    let all: Vec<PathBuf> = load().into_iter().map(|r| r.dll).collect();
    disable(&all)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests that install and remove share the one records file: one at a time.
    static RECORDS: std::sync::Mutex<()> = std::sync::Mutex::new(());

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
        let _records = RECORDS.lock().map_err(|e| e.to_string())?;
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

    #[test]
    fn a_proxy_without_its_record_never_becomes_the_original() -> Result<(), String> {
        let _records = RECORDS.lock().map_err(|e| e.to_string())?;
        let dir = std::env::temp_dir().join(format!("fov-openvr-lost-{}", std::process::id()));
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let (dll, proxy) = (dir.join("openvr_api.dll"), dir.join("proxy.dll"));
        let marker: Vec<u8> = ORIGINAL.encode_utf16().flat_map(u16::to_le_bytes).collect();
        let (old, new) = ([b"old ".as_slice(), &marker].concat(), [b"new ".as_slice(), &marker].concat());
        assert!(is_proxy(&old) && !is_proxy(b"the game's original"));
        std::fs::write(&dll, &old).map_err(|e| e.to_string())?;
        std::fs::write(&proxy, &new).map_err(|e| e.to_string())?;
        let before = load();
        let had_dir = crate::settings::dir().is_some_and(|d| d.exists());
        // No original beside it: nothing to forward to, nothing is moved.
        assert!(enable(std::slice::from_ref(&dll), &proxy).is_err());
        assert_eq!(std::fs::read(&dll).ok(), Some(old));
        std::fs::write(dir.join(ORIGINAL), b"the game's original").map_err(|e| e.to_string())?;
        enable(std::slice::from_ref(&dll), &proxy)?;
        assert_eq!(std::fs::read(&dll).ok(), Some(new));
        assert_eq!(std::fs::read(dir.join(ORIGINAL)).ok().as_deref(), Some(&b"the game's original"[..]));
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
