//! Shared memory between the tray app and every game process: a field-for-field mirror of
//! `native/include/fov_shm.h` (the C header is the source of truth; the offset test below checks the same numbers
//! the C++ and Python mirrors check).
//!
//! The app writes the global settings (and forwarded OSC gaze); games write their slot's stats. Each block has a
//! seqlock counter: odd while being written.

pub const NAME: &str = "Local\\FoveatedVR.v1";
pub const MAGIC: u32 = 0x3156_4F46;
pub const VERSION: u32 = 1;
pub const SLOTS: usize = 8;

pub mod mode {
    pub const OFF: u32 = 0;
    pub const FIXED: u32 = 1;
    pub const EYE: u32 = 2;
}

pub mod preset {
    pub const LOW: u32 = 0;
    pub const MEDIUM: u32 = 1;
    pub const HIGH: u32 = 2;
    pub const CUSTOM: u32 = 3;
}

pub mod rate {
    pub const R1X1: u32 = 0;
    pub const R2X1: u32 = 1;
    pub const R1X2: u32 = 2;
    pub const R2X2: u32 = 3;
    pub const R4X2: u32 = 4;
    pub const R2X4: u32 = 5;
    pub const R4X4: u32 = 6;
}

/// Effective settings, presets already resolved into radii and rates.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Settings {
    pub enabled: u32,
    pub mode: u32,
    pub preset: u32,
    pub debug_tint: u32,
    pub ab_off: u32,
    pub saccade_expand: u32,
    pub inner_deg: f32,
    pub mid_deg: f32,
    pub rate: [u32; 3],
    pub smooth_min_cutoff: f32,
    pub smooth_beta: f32,
    pub smooth_d_cutoff: f32,
    pub saccade_deg_s: f32,
    pub saccade_factor: f32,
    pub saccade_hold_ms: f32,
    pub reserved: [u32; 7],
}

impl Settings {
    /// SPEC 3 starting values, the same as `fov_settings_default` in the core.
    pub fn preset(p: u32) -> Self {
        let (p, inner_deg, mid_deg, rate) = match p {
            preset::LOW => (p, 30.0, 45.0, [rate::R1X1, rate::R1X1, rate::R2X2]),
            preset::HIGH => (p, 15.0, 22.0, [rate::R1X1, rate::R2X2, rate::R4X4]),
            _ => (preset::MEDIUM, 22.0, 35.0, [rate::R1X1, rate::R2X2, rate::R4X4]),
        };
        Self {
            enabled: 1,
            mode: mode::FIXED,
            preset: p,
            saccade_expand: 1,
            inner_deg,
            mid_deg,
            rate,
            smooth_min_cutoff: 1.0,
            smooth_beta: 0.05,
            smooth_d_cutoff: 1.0,
            saccade_deg_s: 180.0,
            saccade_factor: 1.5,
            saccade_hold_ms: 50.0,
            ..Self::default()
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Gaze {
    pub source: u32,
    pub kind: u32,
    pub flags: u32,
    pub pad: u32,
    pub qpc: i64,
    pub eye: [[f32; 3]; 2],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Stats {
    pub exe: [u8; 64],
    pub app_id: u32,
    pub api: u32,
    pub gfx: u32,
    pub layout: u32,
    pub active: u32,
    pub reason: u32,
    pub gaze_source: u32,
    pub gaze_valid_pct: f32,
    pub eye_ms_on: f32,
    pub eye_ms_off: f32,
    pub frames: u64,
    pub rings_bound: u64,
    pub uncertain_eye: u64,
    pub focus: [[f32; 2]; 2],
    pub ui_skipped: u64,
    pub pad2: u64,
    pub last_error: [u8; 96],
}

impl Stats {
    /// The game's exe name.
    pub fn exe(&self) -> String {
        let end = self.exe.iter().position(|&b| b == 0).unwrap_or(self.exe.len());
        String::from_utf8_lossy(&self.exe[..end]).into_owned()
    }

    /// Why foveation is not running in this game, in the words the app shows; None when it runs.
    pub fn inactive_reason(&self) -> Option<&'static str> {
        match self.reason {
            0 => None,
            1 => Some("turned off"),
            2 => Some("off for this game"),
            3 | 4 => Some("blocked: anti-cheat"),
            5 => Some("game has its own foveated rendering"),
            6 => Some("not a game"),
            7 => Some("this GPU or graphics API has no variable rate shading"),
            _ => Some("error, see logs"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Slot {
    pub pid: u32,
    pub settings_seq: u32,
    pub settings: Settings,
    pub stats_seq: u32,
    pub pad: u32,
    pub stats: Stats,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Shm {
    pub magic: u32,
    pub version: u32,
    pub size: u32,
    pub pad: u32,
    pub settings_seq: u32,
    pub gaze_seq: u32,
    pub settings: Settings,
    pub gaze: Gaze,
    pub slots: [Slot; SLOTS],
}

mod mapping;
#[cfg(windows)]
pub mod sys;
pub use mapping::{Mapping, file_path};

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{offset_of, size_of};

    /// The same numbers native/include/fov_shm.h and harness/tests/test_shm.py assert.
    #[test]
    fn layout_matches_c_header() {
        assert_eq!(size_of::<Settings>(), 96);
        assert_eq!(offset_of!(Settings, inner_deg), 24);
        assert_eq!(offset_of!(Settings, rate), 32);
        assert_eq!(offset_of!(Settings, saccade_hold_ms), 64);
        assert_eq!(size_of::<Gaze>(), 48);
        assert_eq!(offset_of!(Gaze, qpc), 16);
        assert_eq!(offset_of!(Gaze, eye), 24);
        assert_eq!(size_of::<Stats>(), 256);
        assert_eq!(offset_of!(Stats, gaze_valid_pct), 92);
        assert_eq!(offset_of!(Stats, frames), 104);
        assert_eq!(offset_of!(Stats, focus), 128);
        assert_eq!(offset_of!(Stats, last_error), 160);
        assert_eq!(size_of::<Slot>(), 368);
        assert_eq!(offset_of!(Slot, settings), 8);
        assert_eq!(offset_of!(Slot, stats), 112);
        assert_eq!(size_of::<Shm>(), 3112);
        assert_eq!(offset_of!(Shm, settings_seq), 16);
        assert_eq!(offset_of!(Shm, settings), 24);
        assert_eq!(offset_of!(Shm, gaze), 120);
        assert_eq!(offset_of!(Shm, slots), 168);
    }

    #[test]
    fn presets_match_core_defaults() {
        let low = Settings::preset(preset::LOW);
        assert_eq!((low.inner_deg, low.mid_deg, low.rate[2]), (30.0, 45.0, rate::R2X2));
        let high = Settings::preset(preset::HIGH);
        assert_eq!((high.inner_deg, high.rate[2]), (15.0, rate::R4X4));
        let other = Settings::preset(99);
        assert_eq!((other.preset, other.inner_deg, other.mid_deg), (preset::MEDIUM, 22.0, 35.0));
        assert_eq!((other.enabled, other.mode, other.saccade_deg_s), (1, mode::FIXED, 180.0));
    }
}
