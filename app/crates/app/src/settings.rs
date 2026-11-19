//! %APPDATA%\FoveatedVR\settings.toml: what the user chose, saved on every change. The in-game core reads the same
//! file when it starts before the app (and for the per-game off list); live changes reach games through shared memory.

use std::path::PathBuf;

#[derive(Clone, Debug, PartialEq)]
pub struct AppSettings {
    pub enabled: bool,
    pub eye_tracked: bool,
    pub preset: u32,
    pub debug_tint: bool,
    /// Lowercase exe names of OpenXR games turned off.
    pub off_games: Vec<String>,
    /// Accent theme (index into design-tokens.json themes); the app's own, games ignore it.
    pub theme: u32,
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            enabled: true,
            eye_tracked: false,
            preset: fov_shm::preset::MEDIUM,
            debug_tint: false,
            off_games: vec![],
            theme: 0,
        }
    }
}

pub fn dir() -> Option<PathBuf> {
    std::env::var_os("APPDATA").map(|d| PathBuf::from(d).join("FoveatedVR"))
}

fn path() -> Option<PathBuf> {
    dir().map(|d| d.join("settings.toml"))
}

fn unquote(v: &str) -> &str {
    v.trim().trim_matches('"')
}

impl AppSettings {
    pub fn parse(text: &str) -> Self {
        let mut s = Self::default();
        for line in text.lines() {
            let Some((key, value)) = line.split_once('=') else { continue };
            match key.trim() {
                "enabled" => s.enabled = value.trim() == "true",
                "mode" => {
                    let mode = unquote(value);
                    s.enabled &= mode != "off";
                    s.eye_tracked = mode == "eye";
                }
                "preset" => {
                    s.preset = match unquote(value) {
                        "low" => fov_shm::preset::LOW,
                        "high" => fov_shm::preset::HIGH,
                        _ => fov_shm::preset::MEDIUM,
                    }
                }
                "debug_tint" => s.debug_tint = value.trim() == "true",
                "theme" => s.theme = value.trim().parse().unwrap_or(0),
                "off_games" => {
                    s.off_games =
                        value.split('"').skip(1).step_by(2).map(str::to_lowercase).filter(|g| !g.is_empty()).collect();
                }
                _ => {}
            }
        }
        s
    }

    pub fn render(&self) -> String {
        let preset = match self.preset {
            fov_shm::preset::LOW => "low",
            fov_shm::preset::HIGH => "high",
            _ => "medium",
        };
        let games: Vec<String> = self.off_games.iter().map(|g| format!("\"{g}\"")).collect();
        format!(
            "# FoveatedVR settings, written by the app.\nenabled = {}\nmode = \"{}\"\npreset = \"{preset}\"\ndebug_tint = {}\noff_games = [{}]\ntheme = {}\n",
            self.enabled,
            if self.eye_tracked { "eye" } else { "fixed" },
            self.debug_tint,
            games.join(", "),
            self.theme
        )
    }

    pub fn load() -> Self {
        path().and_then(|p| std::fs::read_to_string(p).ok()).map(|t| Self::parse(&t)).unwrap_or_default()
    }

    pub fn save(&self) -> Result<(), String> {
        let p = path().ok_or("no APPDATA folder")?;
        if let Some(d) = p.parent() {
            std::fs::create_dir_all(d).map_err(|e| e.to_string())?;
        }
        std::fs::write(&p, self.render()).map_err(|e| e.to_string())
    }

    /// What every game reads: the preset resolved into radii and rates.
    pub fn to_shm(&self) -> fov_shm::Settings {
        let mut s = fov_shm::Settings::preset(self.preset);
        s.enabled = u32::from(self.enabled);
        s.mode = if self.eye_tracked { fov_shm::mode::EYE } else { fov_shm::mode::FIXED };
        s.debug_tint = u32::from(self.debug_tint);
        s
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let s = AppSettings {
            enabled: false,
            eye_tracked: true,
            preset: fov_shm::preset::HIGH,
            debug_tint: true,
            off_games: vec!["game.exe".into(), "other.exe".into()],
            theme: 3,
        };
        assert_eq!(AppSettings::parse(&s.render()), s);
        assert_eq!(AppSettings::parse(""), AppSettings::default());
    }

    #[test]
    fn shm_settings_follow_choices() {
        let s = AppSettings { eye_tracked: true, preset: fov_shm::preset::HIGH, ..Default::default() };
        let shm = s.to_shm();
        assert_eq!((shm.enabled, shm.mode, shm.preset, shm.inner_deg), (1, fov_shm::mode::EYE, 2, 15.0));
    }
}
