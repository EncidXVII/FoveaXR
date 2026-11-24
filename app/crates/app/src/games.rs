// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! The games list as the window shows it: each game's state, then search, filter, sort and grouping (games that
//! will be foveated first). Pure, so the list rules are tested without a window.

use crate::openvr::State;
use crate::settings::AppSettings;
use crate::steam::{Block, Game};

pub struct Entry {
    pub game: Game,
    /// The OpenVR install state (Off for OpenXR-only games), read when the list loads and after each action.
    pub vr: State,
    /// Linux: the OpenXR layer in the game's Proton prefix. None on Windows, where one layer covers every game.
    pub xr: Option<State>,
    /// Windows: the OpenXR layer is not registered for this user (the app was started without installing it), so
    /// OpenXR games start without it.
    pub no_layer: bool,
    pub busy: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Filter {
    All,
    Enabled,
    OpenVr,
    OpenXr,
    Blocked,
}

impl Filter {
    pub fn from_index(i: i32) -> Self {
        match i {
            1 => Self::Enabled,
            2 => Self::OpenVr,
            3 => Self::OpenXr,
            4 => Self::Blocked,
            _ => Self::All,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Row {
    /// Index into the entries; -1 for a group header.
    pub id: i32,
    pub header: bool,
    pub name: String,
    pub api: String,
    pub badge: String,
    pub badge_kind: i32,
    pub detail: String,
    pub action: String,
    pub primary: bool,
    pub can_act: bool,
    pub busy: bool,
    /// Added by hand and FoveaXR not in it: Remove from list is offered.
    pub removable: bool,
}

/// Whether foveation will run in this game when it starts.
pub fn foveated(e: &Entry, s: &AppSettings) -> bool {
    e.game.block.is_none()
        && match (e.game.openvr.is_empty(), e.xr) {
            (false, _) => e.vr == State::On,
            (true, Some(xr)) => xr == State::On,
            (true, None) => !e.no_layer && !e.game.exes.iter().any(|x| s.off_games.contains(x)),
        }
}

/// What a game added by hand says first: FoveaXR knows nothing about it beyond its folder.
const ADDED: &str = "Added by hand. Use FoveaXR only in single-player games: an online game can ban players for anything that changes it.";

/// What every Linux row adds: vkd3d-proton turns D3D12 VRS into Vulkan's; dxvk-nvapi has no D3D11 VRS.
const LINUX_D3D11: &str =
    "On Linux, D3D12 games are foveated; D3D11 games are not available (Proton has no D3D11 variable rate shading).";

fn played(last: u64, now: u64) -> String {
    if last == 0 {
        return String::new();
    }
    let days = now.saturating_sub(last) / 86_400;
    match days {
        0 => " · played today".into(),
        1 => " · played yesterday".into(),
        2..=59 => format!(" · played {days} days ago"),
        _ => format!(" · played {} months ago", days / 30),
    }
}

fn row(id: usize, e: &Entry, s: &AppSettings, now: u64) -> Row {
    let g = &e.game;
    let api = match (g.openvr.is_empty(), g.openxr) {
        (false, true) => "OpenVR + OpenXR",
        (false, false) => "OpenVR",
        _ if g.vr_mod => "OpenXR (VR mod)",
        _ => "OpenXR",
    };
    // (badge, kind, detail, action, primary, can act)
    let (badge, kind, detail, action, primary, can_act) = match g.block {
        Some(Block::AntiCheat) => (
            "Blocked",
            2,
            "Anti-cheat found. FoveaXR never runs here, so nothing it adds can get your account flagged.",
            "Why?",
            false,
            false,
        ),
        Some(Block::NativeFoveation) => (
            "Built in",
            1,
            "This game has its own foveated rendering: use the game's setting, ours stays out of the way.",
            "Why?",
            false,
            false,
        ),
        None if !g.openvr.is_empty() => match e.vr {
            State::On => (
                "On",
                0,
                "Our openvr_api.dll sits in front of the game's own. Disable or uninstall puts the original back. \
                 Changes apply from the game's next start.",
                "Disable",
                false,
                true,
            ),
            // An OpenVR game folder is always there: never unavailable.
            State::Off | State::Unavailable => (
                "Off",
                1,
                "OpenVR games need FoveaXR placed in their folder once. Close the game, then Enable.",
                "Enable",
                true,
                true,
            ),
            State::NeedsReenable => (
                "Updated",
                3,
                "A game update replaced openvr_api.dll, so FoveaXR is out. Enable again to put it back.",
                "Re-enable",
                true,
                true,
            ),
        },
        // Linux: OpenXR is per game, in the game's Proton prefix.
        None if e.xr.is_some() => match e.xr.unwrap_or(State::Off) {
            State::On => (
                "On",
                0,
                "Our OpenXR layer is registered in this game's Proton prefix. Disable or uninstall takes it out \
                 again. Changes apply from the game's next start.",
                "Disable",
                false,
                true,
            ),
            State::Off => (
                "Off",
                1,
                "Proton games read their own Windows registry: Enable registers our OpenXR layer in this game's \
                 prefix. Close the game first.",
                "Enable",
                true,
                true,
            ),
            State::NeedsReenable => (
                "Reset",
                3,
                "The game's Proton prefix no longer has our layer (it was reset or recreated). Enable again.",
                "Re-enable",
                true,
                true,
            ),
            State::Unavailable => (
                "Not started",
                1,
                "Start the game once with Proton, so it has a Wine prefix, then enable it here.",
                "Why?",
                false,
                false,
            ),
        },
        None if e.no_layer => (
            "Not installed",
            3,
            "FoveaXR's OpenXR layer is not registered for this Windows user, so OpenXR games start without it. \
             Run Install.cmd from the folder you unzipped: it registers the layer, without admin rights.",
            "Why?",
            false,
            false,
        ),
        None if !foveated(e, s) => (
            "Off",
            1,
            "Turned off for this game. Turning it on applies from the game's next start.",
            "Turn on",
            true,
            true,
        ),
        None => (
            "On",
            0,
            "Foveated through the OpenXR layer every time it starts; nothing to install. Turning it off applies \
             from the game's next start.",
            "Turn off",
            false,
            true,
        ),
    };
    Row {
        id: i32::try_from(id).unwrap_or(-1),
        header: false,
        name: g.name.clone(),
        api: format!("{api}{}", played(g.last_played, now)),
        badge: badge.into(),
        badge_kind: kind,
        detail: [g.added.then_some(ADDED), Some(detail), (cfg!(unix) && g.block.is_none()).then_some(LINUX_D3D11)]
            .into_iter()
            .flatten()
            .collect::<Vec<_>>()
            .join(" "),
        action: action.into(),
        primary,
        can_act,
        busy: e.busy,
        removable: g.added && e.vr != State::On,
    }
}

fn header(name: &str) -> Row {
    Row {
        id: -1,
        header: true,
        name: name.into(),
        api: String::new(),
        badge: String::new(),
        badge_kind: 1,
        detail: String::new(),
        action: String::new(),
        primary: false,
        can_act: false,
        busy: false,
        removable: false,
    }
}

/// The visible list: matching games, foveated ones first (each group under a header when both are shown),
/// by name or by last played.
pub fn rows(entries: &[Entry], s: &AppSettings, search: &str, filter: Filter, by_played: bool, now: u64) -> Vec<Row> {
    let needle = search.trim().to_lowercase();
    let mut picked: Vec<(usize, &Entry)> = entries
        .iter()
        .enumerate()
        .filter(|(_, e)| needle.is_empty() || e.game.name.to_lowercase().contains(&needle))
        .filter(|(_, e)| match filter {
            Filter::All => true,
            Filter::Enabled => foveated(e, s),
            Filter::OpenVr => !e.game.openvr.is_empty(),
            Filter::OpenXr => e.game.openvr.is_empty(),
            Filter::Blocked => e.game.block.is_some(),
        })
        .collect();
    if by_played {
        picked.sort_by_key(|(_, e)| std::cmp::Reverse(e.game.last_played));
    } else {
        picked.sort_by_key(|(_, e)| e.game.name.to_lowercase());
    }
    let (on, off): (Vec<_>, Vec<_>) = picked.into_iter().partition(|(_, e)| foveated(e, s));
    let mut out = Vec::new();
    let both = !on.is_empty() && !off.is_empty();
    if both {
        out.push(header("Foveated when they start"));
    }
    out.extend(on.iter().map(|(i, e)| row(*i, e, s, now)));
    if both {
        out.push(header("Other games"));
    }
    out.extend(off.iter().map(|(i, e)| row(*i, e, s, now)));
    out
}

/// "12 games · 3 foveated"
pub fn note(entries: &[Entry], s: &AppSettings) -> String {
    let on = entries.iter().filter(|e| foveated(e, s)).count();
    match entries.len() {
        0 => String::new(),
        1 => format!("1 game · {on} foveated"),
        n => format!("{n} games · {on} foveated"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn entry(name: &str, openvr: bool, vr: State, block: Option<Block>, last: u64) -> Entry {
        Entry {
            game: Game {
                app_id: 1,
                name: name.into(),
                last_played: last,
                openvr: if openvr { vec![PathBuf::from("x/openvr_api.dll")] } else { vec![] },
                openxr: !openvr,
                vr_mod: false,
                exes: vec![format!("{}.exe", name.to_lowercase())],
                block,
                prefix: PathBuf::new(),
                dir: PathBuf::new(),
                added: false,
            },
            vr,
            xr: None,
            no_layer: false,
            busy: false,
        }
    }

    fn list() -> Vec<Entry> {
        vec![
            entry("Zeta", true, State::On, None, 10),
            entry("Alpha", true, State::Off, None, 30),
            entry("Mid", false, State::Off, None, 20),
            entry("Cheat", false, State::Off, Some(Block::AntiCheat), 0),
            entry("Upd", true, State::NeedsReenable, None, 0),
        ]
    }

    fn names(rows: &[Row]) -> Vec<&str> {
        rows.iter().map(|r| r.name.as_str()).collect()
    }

    fn games_only(rows: &[Row]) -> Vec<&str> {
        rows.iter().filter(|r| !r.header).map(|r| r.name.as_str()).collect()
    }

    #[test]
    fn foveated_first_then_by_name() {
        let r = rows(&list(), &AppSettings::default(), "", Filter::All, false, 0);
        assert_eq!(names(&r), ["Foveated when they start", "Mid", "Zeta", "Other games", "Alpha", "Cheat", "Upd"]);
        assert!(r[0].header && r[3].header);
    }

    #[test]
    fn search_filter_sort() {
        let s = AppSettings::default();
        assert_eq!(games_only(&rows(&list(), &s, "  a ", Filter::All, false, 0)), ["Zeta", "Alpha", "Cheat"]);
        assert!(rows(&list(), &s, "nothing", Filter::All, false, 0).is_empty());
        // One group only: no headers.
        assert_eq!(names(&rows(&list(), &s, "", Filter::Blocked, false, 0)), ["Cheat"]);
        assert_eq!(names(&rows(&list(), &s, "", Filter::Enabled, false, 0)), ["Mid", "Zeta"]);
        // Last played first, within each group.
        assert_eq!(games_only(&rows(&list(), &s, "", Filter::OpenVr, true, 0)), ["Zeta", "Alpha", "Upd"]);
    }

    #[test]
    fn row_states() {
        let mut s = AppSettings::default();
        let r = rows(&list(), &s, "", Filter::All, false, 86_400 * 3 + 20);
        let by = |n: &str| r.iter().find(|x| x.name == n).cloned();
        let cheat = by("Cheat").map(|x| (x.badge, x.can_act));
        assert_eq!(cheat, Some(("Blocked".into(), false)));
        assert_eq!(by("Upd").map(|x| (x.badge, x.action)), Some(("Updated".into(), "Re-enable".into())));
        assert_eq!(by("Mid").map(|x| x.api), Some("OpenXR · played 3 days ago".into()));
        s.off_games.push("mid.exe".into());
        let r = rows(&list(), &s, "", Filter::All, false, 0);
        assert_eq!(r.iter().find(|x| x.name == "Mid").map(|x| x.action.clone()), Some("Turn on".into()));
        assert_eq!(note(&list(), &s), "5 games · 1 foveated");
    }

    #[test]
    fn proton_openxr_is_per_game() {
        let s = AppSettings::default();
        let mut games = list();
        games[2].xr = Some(State::Off);
        let mid = |g: &[Entry]| rows(g, &s, "mid", Filter::All, false, 0).into_iter().next();
        assert_eq!(mid(&games).map(|r| (r.badge, r.action)), Some(("Off".into(), "Enable".into())));
        games[2].xr = Some(State::Unavailable);
        assert_eq!(mid(&games).map(|r| (r.badge, r.can_act)), Some(("Not started".into(), false)));
        games[2].xr = Some(State::On);
        assert!(foveated(&games[2], &s));
    }

    #[test]
    fn openxr_games_need_the_layer_registered() {
        let s = AppSettings::default();
        let mut games = list();
        for g in &mut games {
            g.no_layer = true;
        }
        let r = rows(&games, &s, "", Filter::All, false, 0);
        let by = |n: &str| r.iter().find(|x| x.name == n).cloned();
        assert_eq!(by("Mid").map(|x| (x.badge, x.can_act)), Some(("Not installed".into(), false)));
        // OpenVR games go through their own DLL: the layer is nothing to them.
        assert_eq!(by("Zeta").map(|x| x.badge), Some("On".into()));
        assert_eq!(note(&games, &s), "5 games · 1 foveated");
    }

    #[test]
    fn a_game_with_a_vr_mod_says_so() {
        let s = AppSettings::default();
        let mut games = list();
        games[2].game.vr_mod = true;
        let mid = rows(&games, &s, "mid", Filter::OpenXr, false, 0).into_iter().next();
        assert_eq!(mid.map(|r| (r.api, r.badge)), Some(("OpenXR (VR mod) · played today".into(), "On".into())));
    }
}
