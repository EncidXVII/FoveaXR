//! Installed Steam games that run in VR: found by the VR runtime DLLs they ship (openvr_api.dll, openxr_loader.dll).

use std::path::{Path, PathBuf};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Block {
    AntiCheat,
    NativeFoveation,
}

#[derive(Clone, Debug)]
pub struct Game {
    pub app_id: u32,
    pub name: String,
    /// 64-bit openvr_api.dll files (the proxy is 64-bit only).
    pub openvr: Vec<PathBuf>,
    pub openxr: bool,
    /// Lowercase exe names in the game's top two folder levels.
    pub exes: Vec<String>,
    pub block: Option<Block>,
}

/// Exe names and app ids of a profile list (`exe = "..."`, `steam_app_id = N`).
#[derive(Default)]
pub struct List {
    exes: Vec<String>,
    ids: Vec<u32>,
}

impl List {
    pub fn parse(text: &str) -> Self {
        let mut l = Self::default();
        for line in text.lines() {
            let Some((key, value)) = line.split_once('=') else { continue };
            let value = value.trim().trim_matches('"');
            match key.trim() {
                "exe" => l.exes.push(value.to_lowercase()),
                "steam_app_id" => l.ids.extend(value.parse::<u32>().ok()),
                _ => {}
            }
        }
        l
    }

    fn has(&self, game: &Game) -> bool {
        self.ids.contains(&game.app_id) || game.exes.iter().any(|e| self.exes.contains(e))
    }
}

pub struct Profiles {
    pub anticheat: List,
    pub native: List,
}

impl Profiles {
    /// profiles/ beside the app (installed), or in the repository (development builds in target/).
    pub fn load(app_dir: &Path) -> Self {
        let read = |name: &str| {
            [app_dir.join("profiles"), app_dir.join("../../profiles")]
                .iter()
                .find_map(|d| std::fs::read_to_string(d.join(name)).ok())
                .map(|t| List::parse(&t))
                .unwrap_or_default()
        };
        Self { anticheat: read("blocklist_anticheat.toml"), native: read("native_foveation.toml") }
    }
}

/// Quoted value of `"key"` on a line of Valve's KeyValues text, with \\ unescaped.
fn kv(line: &str, key: &str) -> Option<String> {
    let rest = line.trim().strip_prefix(&format!("\"{key}\""))?;
    let v = rest.trim().strip_prefix('"')?.strip_suffix('"')?;
    Some(v.replace("\\\\", "\\"))
}

fn steam_root() -> Option<PathBuf> {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let out = std::process::Command::new("reg")
        .args(["query", r"HKCU\Software\Valve\Steam", "/v", "SteamPath"])
        .creation_flags(CREATE_NO_WINDOW)
        .output()
        .ok()?;
    let text = String::from_utf8_lossy(&out.stdout);
    let line = text.lines().find(|l| l.contains("SteamPath"))?;
    Some(PathBuf::from(line.split("REG_SZ").nth(1)?.trim()))
}

fn libraries(root: &Path) -> Vec<PathBuf> {
    let text = std::fs::read_to_string(root.join("steamapps").join("libraryfolders.vdf")).unwrap_or_default();
    let mut libs: Vec<PathBuf> = text.lines().filter_map(|l| kv(l, "path")).map(PathBuf::from).collect();
    if libs.is_empty() {
        libs.push(root.to_path_buf());
    }
    libs
}

fn is_x64(dll: &Path) -> bool {
    let Ok(b) = std::fs::read(dll) else { return false };
    let Some(pe) = b.get(0x3c..0x40).map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]]) as usize) else {
        return false;
    };
    b.get(pe + 4..pe + 6) == Some(&[0x64, 0x86])
}

fn has_anticheat(dir: &Path) -> bool {
    let hit = |name: &str| {
        let n = name.to_lowercase();
        n.starts_with("easyanticheat")
            || n.starts_with("battleye")
            || n.starts_with("beservice")
            || n.starts_with("beclient")
            || n == "start_protected_game.exe"
    };
    let names = |d: &Path| -> Vec<(String, bool)> {
        std::fs::read_dir(d)
            .map(|r| r.flatten().map(|e| (e.file_name().to_string_lossy().into_owned(), e.path().is_dir())).collect())
            .unwrap_or_default()
    };
    names(dir).iter().any(|(n, is_dir)| hit(n) || (*is_dir && names(&dir.join(n)).iter().any(|(m, _)| hit(m))))
}

/// The runtime DLLs and exes of one game folder; a bounded walk, big games have many files.
fn inspect(dir: &Path, game: &mut Game) {
    let mut stack = vec![(dir.to_path_buf(), 0)];
    let mut visited = 0;
    while let Some((d, depth)) = stack.pop() {
        let Ok(entries) = std::fs::read_dir(&d) else { continue };
        for e in entries.flatten() {
            visited += 1;
            if visited > 50_000 {
                return;
            }
            let p = e.path();
            let name = e.file_name().to_string_lossy().to_lowercase();
            if p.is_dir() {
                if depth < 7 {
                    stack.push((p, depth + 1));
                }
            } else if name == "openvr_api.dll" && is_x64(&p) {
                game.openvr.push(p);
            } else if name == "openxr_loader.dll" {
                game.openxr = true;
            } else if depth <= 1 && name.ends_with(".exe") && !name.contains("crash") && !name.starts_with("unins") {
                game.exes.push(name);
            }
        }
    }
}

/// Installed games with a VR runtime DLL, sorted by name. Slow (walks game folders): call off the UI thread.
pub fn scan(profiles: &Profiles) -> Vec<Game> {
    const SKIP: [u32; 3] = [250_820, 228_980, 1_009_850]; // SteamVR, Steamworks redistributables, SteamVR beta tools
    let Some(root) = steam_root() else { return vec![] };
    let mut games = Vec::new();
    for lib in libraries(&root) {
        let apps = lib.join("steamapps");
        let Ok(entries) = std::fs::read_dir(&apps) else { continue };
        for e in entries.flatten() {
            let file = e.file_name().to_string_lossy().into_owned();
            if !file.starts_with("appmanifest_") || !file.ends_with(".acf") {
                continue;
            }
            let text = std::fs::read_to_string(e.path()).unwrap_or_default();
            let field = |k: &str| text.lines().find_map(|l| kv(l, k));
            let (Some(id), Some(name), Some(install)) = (field("appid"), field("name"), field("installdir")) else {
                continue;
            };
            let app_id = id.parse().unwrap_or(0);
            if SKIP.contains(&app_id) {
                continue;
            }
            let dir = apps.join("common").join(install);
            let mut game = Game { app_id, name, openvr: vec![], openxr: false, exes: vec![], block: None };
            inspect(&dir, &mut game);
            if game.openvr.is_empty() && !game.openxr {
                continue;
            }
            game.block = if profiles.anticheat.has(&game) || has_anticheat(&dir) {
                Some(Block::AntiCheat)
            } else if profiles.native.has(&game) {
                Some(Block::NativeFoveation)
            } else {
                None
            };
            games.push(game);
        }
    }
    games.sort_by_key(|g| g.name.to_lowercase());
    games
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keyvalues_lines() {
        assert_eq!(kv("\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"", "path").as_deref(), Some("D:\\SteamLibrary"));
        assert_eq!(kv("\t\"appid\"\t\t\"620980\"", "appid").as_deref(), Some("620980"));
        assert_eq!(kv("\t\"name\"\t\t\"x\"", "appid"), None);
    }

    #[test]
    fn profile_lists() {
        let l = List::parse("[[game]]\nname = \"VRChat\"\nexe = \"VRChat.exe\"\nsteam_app_id = 438100\n");
        let game = |id, exe: &str| Game {
            app_id: id,
            name: String::new(),
            openvr: vec![],
            openxr: true,
            exes: vec![exe.into()],
            block: None,
        };
        assert!(l.has(&game(438_100, "x.exe")));
        assert!(l.has(&game(1, "vrchat.exe")));
        assert!(!l.has(&game(1, "other.exe")));
    }
}
