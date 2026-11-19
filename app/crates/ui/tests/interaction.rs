//! Input on the real component, offscreen: the mouse wheel / touchpad scrolls every view taller than the window (the
//! picture must move), and the games list's keyboard search works. One test: the platform is set once per process.

use std::rc::Rc;

use fov_ui::offscreen::{self, Pointer, Surface};
use fov_ui::{GameRow, MainWindow, Mode};
use slint::{ComponentHandle, ModelRc, VecModel};

fn rows(n: i32) -> Vec<GameRow> {
    (0..n)
        .map(|i| GameRow {
            id: i,
            header: false,
            name: format!("Game {i:02}").into(),
            api: "OpenXR".into(),
            badge: "On".into(),
            badge_kind: 0,
            detail: "detail".into(),
            action: "Turn off".into(),
            primary: false,
            can_act: true,
            busy: false,
        })
        .collect()
}

/// Renders, scrolls `dy` at (x, y), renders again; true when the picture changed.
fn scrolls(s: &mut Surface<MainWindow>, x: f32, y: f32, dy: i32) -> bool {
    s.render();
    s.render();
    let before = s.frame().to_vec();
    // Over a row and over its button: neither may swallow the wheel.
    s.pointer(x, y, Pointer::Move);
    s.pointer(x, y, Pointer::Scroll(dy));
    s.render();
    s.render();
    before != s.frame()
}

fn window(w: u32, h: u32, tab: i32) -> Surface<MainWindow> {
    let s = Surface::new(MainWindow::new, w, h).unwrap_or_else(|e| panic!("window: {e}"));
    s.ui.global::<Mode>().set_reduce_motion(true);
    s.ui.set_tab(tab);
    s.ui.set_games_state(3);
    s.ui.set_games(ModelRc::from(Rc::new(VecModel::from(rows(30)))));
    s
}

#[test]
fn wheel_scrolls_every_view() {
    offscreen::install();
    // Main page in a short window: the page itself scrolls.
    let mut s = window(880, 500, 0);
    assert!(scrolls(&mut s, 500.0, 300.0, -120), "main page, short window");
    // Settings in a very short window.
    let mut s = window(880, 300, 2);
    assert!(scrolls(&mut s, 500.0, 200.0, -120), "settings, short window");
    // Games list: over a row's text, and over its button.
    let mut s = window(880, 640, 1);
    assert!(scrolls(&mut s, 400.0, 400.0, -120), "games list over a row");
    let mut s = window(880, 640, 1);
    assert!(scrolls(&mut s, 790.0, 400.0, -120), "games list over a button");
    // Compact layout at the minimum size.
    let mut s = window(560, 500, 1);
    assert!(scrolls(&mut s, 300.0, 400.0, -120), "games list, compact window");
    let mut s = window(560, 500, 0);
    assert!(scrolls(&mut s, 300.0, 300.0, -120), "main page, compact window");

    // Keyboard on the games page: "/" focuses the search, typing fills it (and reaches Rust), Escape clears it.
    let mut s = window(880, 640, 1);
    let typed = Rc::new(std::cell::RefCell::new(String::new()));
    s.ui.on_search_edited({
        let typed = Rc::clone(&typed);
        move |t| *typed.borrow_mut() = t.to_string()
    });
    s.render();
    s.keys("/tow");
    assert_eq!(s.ui.get_search(), "tow");
    assert_eq!(*typed.borrow(), "tow");
    s.keys("\u{1b}");
    s.render();
    assert_eq!(s.ui.get_search(), "", "Escape clears the search");
}
