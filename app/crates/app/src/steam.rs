// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

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
    /// Unix time Steam last started it, 0 if never.
    pub last_played: u64,
    /// 64-bit openvr_api.dll files (the proxy is 64-bit only).
    pub openvr: Vec<PathBuf>,
    pub openxr: bool,
    /// Its only OpenXR loader came with a VR mod (a BepInEx plugin, PortXR): a flat game played in VR through the mod.
    /// The layer runs there as in any OpenXR game, so it is listed, and can be turned off, like one.
    pub vr_mod: bool,
    /// Lowercase exe names in the game's top two folder levels.
    pub exes: Vec<String>,
    pub block: Option<Block>,
    /// The Proton prefix (steamapps/compatdata/<appid>/pfx); Linux only, it may not exist yet.
    pub prefix: PathBuf,
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

/// The runtime DLLs and exes of one game folder. Folder by folder, the nearest first: the DLLs sit near the top, so
/// the bound on how much is read (big games have many files) cuts deep data and never them. Deepest first, a game
/// with 130,000 files ran out before its `bin\win64` was read.
fn inspect(dir: &Path, game: &mut Game) {
    let mut queue = std::collections::VecDeque::from([(dir.to_path_buf(), 0)]);
    let mut visited = 0;
    let (mut own, mut modded) = (false, false);
    'walk: while let Some((d, depth)) = queue.pop_front() {
        let Ok(entries) = std::fs::read_dir(&d) else { continue };
        let entries: Vec<_> = entries.flatten().collect();
        // A loader under BepInEx, or in or beside a PortXR folder, came with a mod that plays a flat game in VR.
        let folder = d.strip_prefix(dir).unwrap_or(&d).to_string_lossy().to_lowercase();
        let mod_folder = folder.contains("bepinex")
            || folder.contains("portxr")
            || entries.iter().any(|e| e.file_name().to_string_lossy().eq_ignore_ascii_case("portxr"));
        for e in entries {
            visited += 1;
            if visited > 50_000 {
                break 'walk;
            }
            let p = e.path();
            let name = e.file_name().to_string_lossy().to_lowercase();
            if p.is_dir() {
                if depth < 7 {
                    queue.push_back((p, depth + 1));
                }
            } else if name == "openvr_api.dll" && is_x64(&p) {
                game.openvr.push(p);
            } else if name == "openxr_loader.dll" {
                if mod_folder {
                    modded = true;
                } else {
                    own = true;
                }
            } else if depth <= 1 && name.ends_with(".exe") && !name.contains("crash") && !name.starts_with("unins") {
                game.exes.push(name);
            }
        }
    }
    game.openxr = own || modded;
    game.vr_mod = modded && !own;
}

/// App ids Steam itself lists as VR overlays (`steam.overlay.<id>` in its config\steamapps.vrmanifest): tools drawn
/// over games (a desktop view, body tracking), with no eye passes to foveate.
fn overlay_ids(manifest: &str) -> Vec<u32> {
    manifest.split("\"steam.overlay.").skip(1).filter_map(|rest| rest.split('"').next()?.parse().ok()).collect()
}

pub enum Scan {
    NoSteam,
    Games(Vec<Game>),
}

/// Installed games with a VR runtime DLL, sorted by name. Slow (walks game folders): call off the UI thread.
pub fn scan(profiles: &Profiles) -> Scan {
    // Not games: SteamVR, Steamworks redistributables, and overlay tools that ship openvr_api.dll.
    const SKIP: [u32; 4] = [250_820, 228_980, 1_009_850, 1_173_510];
    let roots = crate::platform::steam_roots();
    if roots.is_empty() {
        return Scan::NoSteam;
    }
    let overlays: Vec<u32> = roots
        .iter()
        .flat_map(|r| {
            overlay_ids(&std::fs::read_to_string(r.join("config").join("steamapps.vrmanifest")).unwrap_or_default())
        })
        .collect();
    let mut libs: Vec<PathBuf> = Vec::new();
    for lib in roots.iter().flat_map(|r| libraries(r)) {
        let lib = lib.canonicalize().unwrap_or(lib);
        if !libs.contains(&lib) {
            libs.push(lib);
        }
    }
    let mut games = Vec::new();
    for lib in libs {
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
            if SKIP.contains(&app_id) || overlays.contains(&app_id) {
                continue;
            }
            let dir = apps.join("common").join(install);
            let last_played = field("LastPlayed").and_then(|v| v.parse().ok()).unwrap_or(0);
            let prefix = apps.join("compatdata").join(id.as_str()).join("pfx");
            let mut game = Game {
                app_id,
                name,
                last_played,
                openvr: vec![],
                openxr: false,
                vr_mod: false,
                exes: vec![],
                block: None,
                prefix,
            };
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
    Scan::Games(games)
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
            last_played: 0,
            openvr: vec![],
            openxr: true,
            vr_mod: false,
            exes: vec![exe.into()],
            block: None,
            prefix: PathBuf::new(),
        };
        assert!(l.has(&game(438_100, "x.exe")));
        assert!(l.has(&game(1, "vrchat.exe")));
        assert!(!l.has(&game(1, "other.exe")));
    }

    fn inspected(files: &[&str]) -> Game {
        let dir = std::env::temp_dir().join(format!("fov-inspect-{}-{}", std::process::id(), files.len()));
        let _ = std::fs::remove_dir_all(&dir);
        for f in files {
            let p = dir.join(f);
            let _ = std::fs::create_dir_all(p.parent().unwrap_or(&dir));
            let _ = std::fs::write(p, b"");
        }
        let mut game = Game {
            app_id: 1,
            name: String::new(),
            last_played: 0,
            openvr: vec![],
            openxr: false,
            vr_mod: false,
            exes: vec![],
            block: None,
            prefix: PathBuf::new(),
        };
        inspect(&dir, &mut game);
        let _ = std::fs::remove_dir_all(&dir);
        game
    }

    #[test]
    fn a_vr_mod_makes_a_flat_game_an_openxr_game() {
        // Unity through BepInEx, Unreal with the loader beside a PortXR folder, Godot inside one.
        let unity = inspected(&["Game.exe", "BepInEx/plugins/PortXR/openxr_loader.dll"]);
        assert!(unity.openxr && unity.vr_mod && unity.exes == ["game.exe"]);
        let unreal =
            inspected(&["Game/Binaries/Win64/openxr_loader.dll", "Game/Binaries/Win64/PortXR/portxr.log", "a"]);
        assert!(unreal.openxr && unreal.vr_mod);
        let godot = inspected(&["PortXR/openxr_loader.dll", "a", "b", "c"]);
        assert!(godot.openxr && godot.vr_mod);
        // A loader of the game's own makes it a VR game, with or without a mod beside it.
        let own = inspected(&[
            "G_Data/Plugins/x86_64/openxr_loader.dll",
            "BepInEx/plugins/PortXR/openxr_loader.dll",
            "a",
            "b",
        ]);
        assert!(own.openxr && !own.vr_mod);
        let flat = inspected(&["Game.exe", "a", "b", "c", "d", "e"]);
        assert!(!flat.openxr && !flat.vr_mod);
    }

    #[test]
    fn steam_names_the_overlays() {
        let manifest = r#"{"applications":[{"app_key":"steam.app.438100"},{"app_key":"steam.overlay.1173510"},
            {"app_key": "steam.overlay.2370570", "launch_type": "url"}]}"#;
        assert_eq!(overlay_ids(manifest), [1_173_510, 2_370_570]);
        assert!(overlay_ids("").is_empty());
    }
}
