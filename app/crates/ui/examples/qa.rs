//! QA screenshots of every view, rendered offscreen with the software renderer:
//!     cargo run -p fov-ui --example qa -- <out dir>
//! Motion is off (reduced motion) so each picture shows the settled state.

use std::path::{Path, PathBuf};
use std::rc::Rc;

use fov_ui::offscreen::{self, Surface};
use fov_ui::{GameRow, MainWindow, Mode};
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

/// A sample row; the action decides the rest (primary for Enable, "Why?" for games we must not touch).
fn game(id: i32, name: &str, api: &str, badge: &str, kind: i32, action: &str) -> GameRow {
    let (primary, can_act) = (action.ends_with("nable") && action != "Disable", action != "Why?");
    GameRow {
        id,
        header: false,
        name: name.into(),
        api: api.into(),
        badge: badge.into(),
        badge_kind: kind,
        detail: "Foveated through the OpenXR layer every time it starts; nothing to install.".into(),
        action: action.into(),
        primary,
        can_act,
        busy: false,
    }
}

fn header(name: &str) -> GameRow {
    GameRow { id: -1, header: true, name: SharedString::from(name), ..game(-1, "", "", "", 1, "") }
}

fn sample() -> Vec<GameRow> {
    let mut busy = game(3, "Tower Unite", "OpenVR · played yesterday", "Off", 1, "Enable");
    busy.busy = true;
    vec![
        header("Foveated when they start"),
        game(0, "HUMANITY", "OpenXR · played 3 days ago", "On", 0, "Turn off"),
        game(1, "A Game With An Exceptionally Long Name: The Definitive Edition", "OpenVR", "On", 0, "Disable"),
        header("Other games"),
        game(2, "Resonite", "OpenVR · played 2 months ago", "Updated", 3, "Re-enable"),
        busy,
        GameRow {
            detail: "Anti-cheat found. FoveatedVR never runs here, so nothing it adds can get your account flagged."
                .into(),
            ..game(4, "VRChat", "OpenVR", "Blocked", 2, "Why?")
        },
        game(5, "Built-In Foveation Game", "OpenXR", "Built in", 1, "Why?"),
    ]
}

fn save(s: &mut Surface<MainWindow>, out: &Path, name: &str) -> Result<(), Box<dyn std::error::Error>> {
    // Settle layout and the first paint, then a second pass for anything that depended on it.
    s.render();
    s.render();
    let file = std::fs::File::create(out.join(format!("{name}.png")))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(file), s.width, s.height);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    enc.write_header()?.write_image_data(s.frame())?;
    println!("{}", out.join(format!("{name}.png")).display());
    Ok(())
}

fn window(w: u32, h: u32) -> Result<Surface<MainWindow>, Box<dyn std::error::Error>> {
    let s = Surface::new(MainWindow::new, w, h)?;
    s.ui.global::<Mode>().set_reduce_motion(true);
    s.ui.set_version("1.0.0".into());
    Ok(s)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let out = PathBuf::from(std::env::args().nth(1).unwrap_or_else(|| ".agent/qa".into()));
    std::fs::create_dir_all(&out)?;
    offscreen::install();

    let mut s = window(880, 640)?;
    s.ui.set_saved_valid(true);
    s.ui.set_saved_ms(1.24);
    s.ui.set_saved_pct(31.0);
    s.ui.set_status("tower-win64-shipping.exe".into());
    save(&mut s, &out, "main")?;
    s.ui.set_enabled(false);
    s.ui.set_mode(1);
    s.ui.set_saved_valid(false);
    s.ui.set_status("No game running. Start a VR game: the saving shows here once you compare with Ctrl+F9.".into());
    save(&mut s, &out, "main_off_eye")?;

    let mut s = window(880, 640)?;
    s.ui.set_tab(1);
    s.ui.set_games_state(3);
    s.ui.set_games_note("6 games · 2 foveated".into());
    s.ui.set_games(ModelRc::from(Rc::new(VecModel::from(sample()))));
    save(&mut s, &out, "games")?;
    // "Why?" on the blocked game opens its row.
    s.pointer(792.0, 542.0, offscreen::Pointer::Down);
    s.pointer(792.0, 542.0, offscreen::Pointer::Up);
    save(&mut s, &out, "games_row_open")?;
    s.ui.set_search("zzz".into());
    s.ui.set_games(ModelRc::from(Rc::new(VecModel::<GameRow>::default())));
    save(&mut s, &out, "games_no_results")?;

    let mut s = window(880, 640)?;
    s.ui.set_tab(1);
    s.ui.set_games_state(2);
    save(&mut s, &out, "games_empty")?;
    s.ui.set_games_state(1);
    save(&mut s, &out, "games_no_steam")?;

    let mut s = window(880, 640)?;
    s.ui.set_tab(2);
    s.ui.global::<Mode>().set_theme(3);
    save(&mut s, &out, "settings_sky")?;

    let mut s = window(560, 520)?;
    s.ui.set_tab(1);
    s.ui.set_games_state(3);
    s.ui.set_games(ModelRc::from(Rc::new(VecModel::from(sample()))));
    save(&mut s, &out, "games_narrow")?;
    Ok(())
}
