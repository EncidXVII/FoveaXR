//! Tray icon: open the window, quit. Windows: the notification area (tray-icon). Linux: a StatusNotifierItem
//! (ksni) that KDE shows natively and GNOME with the AppIndicator extension. Without one the window stays the app.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    Open,
    Quit,
}

#[cfg(windows)]
mod imp {
    use super::Command;
    use tray_icon::menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem};
    use tray_icon::{Icon, TrayIcon, TrayIconBuilder, TrayIconEvent};

    pub struct Tray {
        _icon: TrayIcon,
        open: MenuItem,
        quit: MenuItem,
    }

    /// The FoveaXR mark from the exe's icons (see build.rs) at the tray's size: lime on a dark taskbar, dark ink on
    /// a light one, where lime would be too faint.
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
}

#[cfg(unix)]
mod imp {
    use super::Command;
    use ksni::blocking::TrayMethods;
    use std::sync::mpsc::{Receiver, Sender, channel};

    /// The tray mark, 64 px, drawn by packaging/brand/marks.py.
    const PNG: &[u8] = include_bytes!("../../../../packaging/brand/foveaxr-64.png");

    struct Item {
        tx: Sender<Command>,
        icon: ksni::Icon,
    }

    impl ksni::Tray for Item {
        fn id(&self) -> String {
            "foveatedvr".into()
        }
        fn title(&self) -> String {
            "FoveatedVR".into()
        }
        fn icon_pixmap(&self) -> Vec<ksni::Icon> {
            vec![self.icon.clone()]
        }
        fn activate(&mut self, _x: i32, _y: i32) {
            let _ = self.tx.send(Command::Open);
        }
        fn menu(&self) -> Vec<ksni::MenuItem<Self>> {
            use ksni::menu::StandardItem;
            vec![
                StandardItem {
                    label: "Open FoveatedVR".into(),
                    activate: Box::new(|t: &mut Self| {
                        let _ = t.tx.send(Command::Open);
                    }),
                    ..Default::default()
                }
                .into(),
                ksni::MenuItem::Separator,
                StandardItem {
                    label: "Quit".into(),
                    activate: Box::new(|t: &mut Self| {
                        let _ = t.tx.send(Command::Quit);
                    }),
                    ..Default::default()
                }
                .into(),
            ]
        }
    }

    /// RGBA PNG to the ARGB32 (network order) a StatusNotifierItem wants.
    fn argb(png_bytes: &[u8]) -> Result<ksni::Icon, String> {
        let mut decoder = png::Decoder::new(std::io::Cursor::new(png_bytes));
        decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::ALPHA);
        let mut reader = decoder.read_info().map_err(|e| e.to_string())?;
        let mut buf = vec![0; reader.output_buffer_size().ok_or("png too large")?];
        let info = reader.next_frame(&mut buf).map_err(|e| e.to_string())?;
        let data = buf[..info.buffer_size()].as_chunks::<4>().0.iter().flat_map(|&[r, g, b, a]| [a, r, g, b]).collect();
        let size = |v: u32| i32::try_from(v).map_err(|e| e.to_string());
        Ok(ksni::Icon { width: size(info.width)?, height: size(info.height)?, data })
    }

    pub struct Tray {
        _handle: ksni::blocking::Handle<Item>,
        rx: Receiver<Command>,
    }

    impl Tray {
        /// Fails without a StatusNotifier host (e.g. GNOME without the AppIndicator extension).
        pub fn new() -> Result<Self, String> {
            let (tx, rx) = channel();
            let handle = Item { tx, icon: argb(PNG)? }.spawn().map_err(|e| e.to_string())?;
            Ok(Self { _handle: handle, rx })
        }

        pub fn poll(&self) -> Option<Command> {
            self.rx.try_recv().ok()
        }
    }
}

pub use imp::Tray;
