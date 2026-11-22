// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! Input on the real component, offscreen. Scrolling is measured, not guessed: each check reads where the scrollbar
//! thumb is in the rendered picture, so a hover highlight never passes for a scroll. The wheel arrives as it does
//! from a real mouse on Windows (an event without a gesture phase, which Slint animates). One test: the platform is
//! set once per process.

use std::rc::Rc;
use std::time::{Duration, Instant};

use fov_ui::offscreen::{self, Pointer, Surface};
use fov_ui::{GameRow, MainWindow};
use i_slint_core::input::{BackendMouseEvent, TouchPhase};
use slint::platform::WindowEvent;
use slint::{ComponentHandle, Model, ModelRc, VecModel};

fn rows(n: i32) -> Vec<GameRow> {
    (0..n)
        .map(|i| GameRow {
            id: i,
            // Two groups, as the app lists them.
            header: i == 0 || i == 12,
            name: format!("Game {i:02}").into(),
            api: "OpenXR".into(),
            badge: "On".into(),
            badge_kind: 0,
            detail: "Why this game is in the state it is, in a sentence long enough to wrap onto a second line.".into(),
            action: "Turn off".into(),
            primary: false,
            can_act: true,
            busy: false,
        })
        .collect()
}

/// Renders for `ms`, so animations (the wheel's, a row opening) run to their end.
fn settle(s: &mut Surface<MainWindow>, ms: u64) {
    let end = Instant::now() + Duration::from_millis(ms);
    loop {
        s.render();
        if Instant::now() >= end {
            break;
        }
        std::thread::sleep(Duration::from_millis(8));
    }
}

/// One wheel notch (60 px) towards the end of the content, or back with `up`, then time to finish.
fn notch(s: &mut Surface<MainWindow>, x: f32, y: f32, up: bool) {
    s.pointer(x, y, Pointer::Move);
    s.ui.window().dispatch_event(WindowEvent::internal(BackendMouseEvent::Wheel {
        position: i_slint_core::lengths::LogicalPoint::new(x, y),
        delta_x: 0.0,
        delta_y: if up { 60.0 } else { -60.0 },
        phase: TouchPhase::Moved,
    }));
    settle(s, 450);
}

/// Top and bottom of the scrollbar thumb in pixel column `x` (it is drawn in one of three greys).
fn thumb(s: &mut Surface<MainWindow>, x: u32) -> (i32, i32) {
    let (w, h, x) = (s.width as usize, s.height as usize, x as usize);
    let f = s.frame();
    let is = |y: usize| {
        let p = &f[(y * w + x) * 4..(y * w + x) * 4 + 3];
        p == [0x64, 0x64, 0x6B] || p == [0x88, 0x88, 0x8F] || p == [0xA8, 0xA8, 0xAF]
    };
    let top = (0..h).find(|&y| is(y)).unwrap_or_else(|| panic!("no scrollbar in column {x}"));
    let len = (top..h).take_while(|&y| is(y)).count();
    (top as i32, (top + len) as i32)
}

fn window(w: u32, h: u32, tab: i32) -> (Surface<MainWindow>, Rc<VecModel<GameRow>>) {
    let mut s = Surface::new(MainWindow::new, w, h).unwrap_or_else(|e| panic!("window: {e}"));
    s.ui.set_tab(tab);
    s.ui.set_games_state(3);
    let model = Rc::new(VecModel::from(rows(30)));
    s.ui.set_games(ModelRc::from(Rc::clone(&model)));
    settle(&mut s, 50);
    (s, model)
}

/// A notch at (x, y) moves the thumb in column `bar` down by a notch's worth.
fn scrolls(s: &mut Surface<MainWindow>, bar: u32, x: f32, y: f32, what: &str) {
    let before = thumb(s, bar).0;
    notch(s, x, y, false);
    let after = thumb(s, bar).0;
    assert!(after >= before + 8, "{what}: the wheel at ({x}, {y}) moved the thumb from {before} to {after}");
}

#[test]
fn scrolling_and_keys() {
    offscreen::install();
    // The pages' own scrollbar sits 8 px from the window's right edge; the games list's at the list's right edge.
    // Main page and settings in windows too short for them: the page scrolls.
    let (mut s, _) = window(880, 500, 0);
    scrolls(&mut s, 873, 500.0, 300.0, "main page, short window");
    let (mut s, _) = window(880, 300, 2);
    scrolls(&mut s, 873, 500.0, 200.0, "settings, short window");
    let (mut s, _) = window(560, 500, 0);
    scrolls(&mut s, 553, 300.0, 300.0, "main page, compact window");

    // Games list: with the pointer on a row, on a row's button, on the scrollbar, and anywhere else on the page.
    let (mut s, rows) = window(880, 640, 1);
    let bar = 843;
    let (top, bottom) = thumb(&mut s, bar);
    scrolls(&mut s, bar, 400.0, 400.0, "a row");
    scrolls(&mut s, bar, 790.0, 400.0, "a row's button");
    scrolls(&mut s, bar, 840.0, 400.0, "the scrollbar");
    scrolls(&mut s, bar, 400.0, 50.0, "the page title");
    scrolls(&mut s, bar, 400.0, 110.0, "the search field");
    scrolls(&mut s, bar, 400.0, 170.0, "the filter bar");
    scrolls(&mut s, bar, 862.0, 400.0, "the right margin");
    scrolls(&mut s, bar, 400.0, 632.0, "the bottom margin");
    // The sidebar is not the page.
    let before = thumb(&mut s, bar);
    notch(&mut s, 100.0, 400.0, false);
    assert_eq!(thumb(&mut s, bar), before, "the wheel over the sidebar leaves the list alone");
    // Back up, from inside and from outside the list.
    notch(&mut s, 400.0, 400.0, true);
    notch(&mut s, 400.0, 50.0, true);
    let up = thumb(&mut s, bar).0;
    assert!(up <= before.0 - 16, "two notches up moved the thumb from {} to {up}", before.0);

    // A row that changes (its button was clicked, its state came back) leaves the list where it is.
    let before = thumb(&mut s, bar).0;
    let mut row = rows.row_data(20).unwrap_or_default();
    row.busy = true;
    rows.set_row_data(20, row);
    rows.remove(29);
    settle(&mut s, 100);
    let after = thumb(&mut s, bar).0;
    assert!((after - before).abs() <= 3, "a changed row moved the list: thumb {before} to {after}");

    // The scrollbar drags: 100 px of pointer is 100 px of thumb. A press on the track brings the thumb there.
    let (mut s, _) = window(880, 640, 1);
    s.pointer(840.0, top as f32 + 30.0, Pointer::Move);
    s.pointer(840.0, top as f32 + 30.0, Pointer::Down);
    for i in 1..=10 {
        s.pointer(840.0, top as f32 + 30.0 + 10.0 * i as f32, Pointer::Move);
        settle(&mut s, 16);
    }
    s.pointer(840.0, top as f32 + 130.0, Pointer::Up);
    settle(&mut s, 200);
    let dragged = thumb(&mut s, bar).0;
    assert!((dragged - top - 100).abs() <= 3, "dragging the thumb 100 px moved it from {top} to {dragged}");
    s.pointer(840.0, 590.0, Pointer::Move);
    s.pointer(840.0, 590.0, Pointer::Down);
    s.pointer(840.0, 590.0, Pointer::Up);
    settle(&mut s, 200);
    let (jumped, end) = thumb(&mut s, bar);
    assert!(jumped < 590 && end > 590, "a press on the track at 590 put the thumb at {jumped}..{end}");

    // Wide window: the list keeps its measure on the left, and the wheel works over the empty side too.
    let (mut s, _) = window(1600, 640, 1);
    scrolls(&mut s, 200 + 36 + 880 - 1, 1400.0, 400.0, "the empty side of a wide window");

    // Arrow keys walk the whole list and it follows: at the last row the list is at its end. Space opens that row,
    // and all of it comes into view (the list is at its new, longer end).
    let (mut s, _) = window(880, 640, 1);
    for _ in 0..30 {
        s.keys("\u{F701}");
        settle(&mut s, 30);
    }
    settle(&mut s, 300);
    let list_end = bottom - top + thumb(&mut s, bar).0;
    assert!((thumb(&mut s, bar).1 - list_end).abs() <= 1, "arrow keys: the last row is selected and in view");
    assert!(thumb(&mut s, bar).0 > top + 250, "arrow keys: the list followed the selection to its end");
    s.keys(" ");
    settle(&mut s, 700);
    let (open_top, open_end) = thumb(&mut s, bar);
    assert!(open_end - open_top < bottom - top, "the opened row made the list longer");
    assert!((open_top - top) + (open_end - open_top) >= list_end - top - 1, "the opened row is in view to its end");

    // Keyboard on the games page: "/" focuses the search, typing fills it (and reaches Rust), Escape clears it.
    let (mut s, _) = window(880, 640, 1);
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
