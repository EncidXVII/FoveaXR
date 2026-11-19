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

/// The FoveaXR mark from the exe's icons (see build.rs) at the tray's size: lime on a dark taskbar, dark ink on a
/// light one, where lime would be too faint.
fn icon() -> Result<Icon, String> {
    let size = fov_shm::sys::small_icon_px();
    Icon::from_resource(if fov_shm::sys::light_taskbar() { 3 } else { 2 }, Some((size, size)))
        .map_err(|e| e.to_string())
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
