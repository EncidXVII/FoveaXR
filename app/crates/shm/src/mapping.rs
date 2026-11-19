//! The mapping itself. Unsafe is confined here: raw pointers into memory other processes write.
//!
//! Windows: the named section the core maps too. Linux: the file /dev/shm/FoveatedVR.v1, which the core inside
//! Wine/Proton maps as Z:\dev\shm\FoveatedVR.v1 (a named Wine section is invisible to Linux processes). The file
//! FOV_SHM_FILE names replaces either, for tests.
#![allow(unsafe_code)]

use std::ptr::{addr_of_mut, read_volatile, write_volatile};
use std::sync::atomic::{AtomicU32, Ordering, fence};

use crate::{MAGIC, Settings, Shm, Stats, VERSION, preset};

const RETRIES: usize = 64;

/// The file a Linux app maps, unless FOV_SHM_FILE names another (Windows: only when it is set).
pub fn file_path() -> Option<std::path::PathBuf> {
    match std::env::var_os("FOV_SHM_FILE") {
        Some(p) => Some(p.into()),
        None if cfg!(unix) => Some("/dev/shm/FoveatedVR.v1".into()),
        None => None,
    }
}

#[cfg(windows)]
mod os {
    use windows::Win32::Foundation::{CloseHandle, GENERIC_READ, GENERIC_WRITE, HANDLE, INVALID_HANDLE_VALUE};
    use windows::Win32::Storage::FileSystem::{
        CreateFileW, FILE_ATTRIBUTE_NORMAL, FILE_BEGIN, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_ALWAYS, SetEndOfFile,
        SetFilePointerEx,
    };
    use windows::Win32::System::Memory::{
        CreateFileMappingW, FILE_MAP_ALL_ACCESS, MEMORY_MAPPED_VIEW_ADDRESS, MapViewOfFile, PAGE_READWRITE,
        UnmapViewOfFile,
    };
    use windows::core::{HSTRING, PCWSTR};

    use crate::{NAME, Shm};

    pub struct View {
        handles: [HANDLE; 2],
        view: MEMORY_MAPPED_VIEW_ADDRESS,
    }

    pub fn map() -> Result<(View, *mut Shm), String> {
        let size = u32::try_from(size_of::<Shm>()).map_err(|e| e.to_string())?;
        // SAFETY: plain Win32 calls; every handle is checked and closed in Drop, the view checked for null.
        unsafe {
            let mut file = INVALID_HANDLE_VALUE;
            if let Some(path) = super::file_path() {
                let p = HSTRING::from(path.as_os_str());
                file = CreateFileW(
                    PCWSTR(p.as_ptr()),
                    (GENERIC_READ | GENERIC_WRITE).0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    None,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    None,
                )
                .map_err(|e| format!("{}: {e}", path.display()))?;
                if std::fs::metadata(&path).map(|m| m.len()).unwrap_or(0) < u64::from(size) {
                    let _ = SetFilePointerEx(file, i64::from(size), None, FILE_BEGIN);
                    let _ = SetEndOfFile(file);
                }
            }
            let name = HSTRING::from(NAME);
            let section = if file == INVALID_HANDLE_VALUE { PCWSTR(name.as_ptr()) } else { PCWSTR::null() };
            let handle = CreateFileMappingW(file, None, PAGE_READWRITE, 0, size, section)
                .map_err(|e| format!("CreateFileMapping: {e}"))?;
            let view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size_of::<Shm>());
            if view.Value.is_null() {
                let _ = CloseHandle(handle);
                return Err("MapViewOfFile failed".into());
            }
            Ok((View { handles: [handle, file], view }, view.Value.cast()))
        }
    }

    impl Drop for View {
        fn drop(&mut self) {
            // SAFETY: the view and handles came from map() and are released once.
            unsafe {
                let _ = UnmapViewOfFile(self.view);
                for h in self.handles {
                    if h != INVALID_HANDLE_VALUE {
                        let _ = CloseHandle(h);
                    }
                }
            }
        }
    }
}

#[cfg(unix)]
mod os {
    use std::os::fd::AsRawFd;

    use crate::Shm;

    pub struct View {
        addr: *mut libc::c_void,
    }

    pub fn map() -> Result<(View, *mut Shm), String> {
        let path = super::file_path().ok_or("no shared memory path")?;
        let size = size_of::<Shm>();
        let file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(&path)
            .map_err(|e| format!("{}: {e}", path.display()))?;
        // A new file is empty: grow it to the struct, zero filled. Never shrink one a newer core made bigger.
        let len = file.metadata().map_err(|e| e.to_string())?.len();
        if len < size as u64 {
            file.set_len(size as u64).map_err(|e| e.to_string())?;
        }
        // SAFETY: a shared mapping of an open file at least `size` bytes long; checked for failure.
        let addr = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                file.as_raw_fd(),
                0,
            )
        };
        if addr == libc::MAP_FAILED {
            return Err(format!("mmap {}: {}", path.display(), std::io::Error::last_os_error()));
        }
        Ok((View { addr }, addr.cast()))
    }

    impl Drop for View {
        fn drop(&mut self) {
            // SAFETY: the mapping came from map() and is unmapped once.
            unsafe {
                libc::munmap(self.addr, size_of::<Shm>());
            }
        }
    }
}

pub struct Mapping {
    _view: os::View,
    shm: *mut Shm,
}

// The view is process-wide memory; every access goes through the seqlock protocol.
unsafe impl Send for Mapping {}

impl Mapping {
    /// Opens or creates the mapping; fills the header and default settings if no one has yet.
    pub fn open() -> Result<Self, String> {
        let (view, shm) = os::map()?;
        let m = Self { _view: view, shm };
        // SAFETY: shm points at a live view of size_of::<Shm>() bytes.
        unsafe {
            if (*m.shm).magic != MAGIC {
                m.write_settings(&Settings::preset(preset::MEDIUM));
                (*m.shm).version = VERSION;
                (*m.shm).size = u32::try_from(size_of::<Shm>()).unwrap_or(u32::MAX);
                AtomicU32::from_ptr(addr_of_mut!((*m.shm).magic)).store(MAGIC, Ordering::Release);
            } else if (*m.shm).version != VERSION {
                return Err(format!("shared memory version {}, expected {VERSION}", (*m.shm).version));
            }
        }
        Ok(m)
    }

    pub fn write_settings(&self, s: &Settings) {
        // SAFETY: fields of a live view; the app is the only settings writer.
        unsafe {
            let seq = AtomicU32::from_ptr(addr_of_mut!((*self.shm).settings_seq));
            seq.fetch_add(1, Ordering::AcqRel);
            write_volatile(addr_of_mut!((*self.shm).settings), *s);
            seq.fetch_add(1, Ordering::Release);
        }
    }

    /// None when every retry saw a write in progress.
    pub fn settings(&self) -> Option<Settings> {
        // SAFETY: fields of a live view.
        unsafe { read(addr_of_mut!((*self.shm).settings_seq), addr_of_mut!((*self.shm).settings)) }
    }

    /// Stats of every claimed slot, with its process id (a Windows or Wine process id; the process may have ended).
    pub fn stats(&self) -> Vec<(u32, Stats)> {
        let mut out = Vec::new();
        for i in 0..crate::SLOTS {
            // SAFETY: fields of a live view; the pid is read atomically, the stats through their seqlock.
            unsafe {
                let slot = addr_of_mut!((*self.shm).slots[i]);
                let pid = AtomicU32::from_ptr(addr_of_mut!((*slot).pid)).load(Ordering::Acquire);
                if pid != 0
                    && let Some(st) = read(addr_of_mut!((*slot).stats_seq), addr_of_mut!((*slot).stats))
                {
                    out.push((pid, st));
                }
            }
        }
        out
    }
}

/// Seqlock read: copies the block and retries while a write was in progress or happened meanwhile.
///
/// # Safety
/// Both pointers must point into the live view.
unsafe fn read<T: Copy>(seq: *mut u32, block: *mut T) -> Option<T> {
    // SAFETY: the caller guarantees both pointers are valid; a torn copy is detected by the counter and discarded.
    unsafe {
        let seq = AtomicU32::from_ptr(seq);
        for _ in 0..RETRIES {
            let before = seq.load(Ordering::Acquire);
            if before & 1 == 1 {
                std::hint::spin_loop();
                continue;
            }
            let v = read_volatile(block);
            fence(Ordering::Acquire);
            if seq.load(Ordering::Relaxed) == before {
                return Some(v);
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests touching FOV_SHM_FILE run one at a time (the variable is process-wide).
    static ENV: std::sync::Mutex<()> = std::sync::Mutex::new(());

    #[test]
    fn settings_round_trip() -> Result<(), String> {
        let _env = ENV.lock().map_err(|e| e.to_string())?;
        let m = Mapping::open()?;
        let mut s = Settings::preset(preset::HIGH);
        s.debug_tint = 1;
        m.write_settings(&s);
        assert_eq!(m.settings(), Some(s));
        // A second mapping sees the same memory.
        assert_eq!(Mapping::open()?.settings(), Some(s));
        Ok(())
    }

    /// The Linux arrangement on any OS: two mappings of one file share the pages, header and seqlock included.
    #[test]
    fn file_backed_round_trip() -> Result<(), String> {
        let _env = ENV.lock().map_err(|e| e.to_string())?;
        let path = std::env::temp_dir().join(format!("fov-shm-test-{}", std::process::id()));
        // SAFETY (env): tests touching FOV_SHM_FILE hold ENV, so nothing reads the environment meanwhile.
        unsafe { std::env::set_var("FOV_SHM_FILE", &path) };
        let result = (|| {
            let a = Mapping::open()?;
            let s = Settings::preset(preset::LOW);
            a.write_settings(&s);
            let b = Mapping::open()?;
            assert_eq!(b.settings(), Some(s));
            assert_eq!(std::fs::metadata(&path).map(|m| m.len()).ok(), Some(size_of::<Shm>() as u64));
            Ok(())
        })();
        unsafe { std::env::remove_var("FOV_SHM_FILE") };
        let _ = std::fs::remove_file(&path);
        result
    }
}
