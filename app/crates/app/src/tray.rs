//! Tray icon: open the window, quit. Events arrive through the main thread's message pump (Slint's event loop).
use tray_icon::menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem};
use tray_icon::{Icon, TrayIcon, TrayIconBuilder, TrayIconEvent};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    Open,
    Quit,
}

pub struct Tray {
    _icon: TrayIcon,
    open: MenuItem,
    quit: MenuItem,
}

/// The accent ring on transparent: the app has no drawn mark yet (comes with the installer's icons).
fn icon() -> Result<Icon, String> {
    const N: u32 = 32;
    let mut rgba = Vec::with_capacity((N * N * 4) as usize);
    for y in 0..N {
        for x in 0..N {
            let (dx, dy) = (x as f32 - 15.5, y as f32 - 15.5);
            let r = (dx * dx + dy * dy).sqrt();
            let on = (9.0..15.0).contains(&r) || r < 4.0;
            rgba.extend_from_slice(if on { &[0xC6, 0xFF, 0x1A, 0xFF] } else { &[0, 0, 0, 0] });
        }
    }
    Icon::from_rgba(rgba, N, N).map_err(|e| e.to_string())
}

impl Tray {
    pub fn new() -> Result<Self, String> {
        let open = MenuItem::new("Open FoveatedVR", true, None);
        let quit = MenuItem::new("Quit", true, None);
        let menu = Menu::new();
        menu.append_items(&[&open, &PredefinedMenuItem::separator(), &quit]).map_err(|e| e.to_string())?;
        let icon = TrayIconBuilder::new()
            .with_menu(Box::new(menu))
            .with_tooltip("FoveatedVR")
            .with_icon(icon()?)
            .build()
            .map_err(|e| e.to_string())?;
        Ok(Self { _icon: icon, open, quit })
    }

    /// Next command, if any (non-blocking). A left click on the icon opens the window.
    pub fn poll(&self) -> Option<Command> {
        while let Ok(event) = TrayIconEvent::receiver().try_recv() {
            if let TrayIconEvent::Click { button: tray_icon::MouseButton::Left, .. } = event {
                return Some(Command::Open);
            }
        }
        let event = MenuEvent::receiver().try_recv().ok()?;
        if event.id == *self.open.id() {
            Some(Command::Open)
        } else if event.id == *self.quit.id() {
            Some(Command::Quit)
        } else {
            None
        }
    }
}
