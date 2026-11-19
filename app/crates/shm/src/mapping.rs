//! The named mapping itself. Unsafe is confined here: raw pointers into memory other processes write.
#![allow(unsafe_code)]

use std::ptr::{addr_of_mut, read_volatile, write_volatile};
use std::sync::atomic::{AtomicU32, Ordering, fence};

use windows::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
use windows::Win32::System::Memory::{
    CreateFileMappingW, FILE_MAP_ALL_ACCESS, MEMORY_MAPPED_VIEW_ADDRESS, MapViewOfFile, PAGE_READWRITE, UnmapViewOfFile,
};
use windows::core::{HSTRING, PCWSTR};

use crate::{MAGIC, NAME, Settings, Shm, VERSION, preset};

const RETRIES: usize = 64;

pub struct Mapping {
    handle: HANDLE,
    view: MEMORY_MAPPED_VIEW_ADDRESS,
    shm: *mut Shm,
}

// The view is process-wide memory; every access goes through the seqlock protocol.
unsafe impl Send for Mapping {}

impl Mapping {
    /// Opens or creates the mapping; fills the header and default settings if no one has yet.
    pub fn open() -> Result<Self, String> {
        let name = HSTRING::from(NAME);
        let size = u32::try_from(size_of::<Shm>()).map_err(|e| e.to_string())?;
        // SAFETY: plain Win32 calls; the view is checked for null before use and unmapped in Drop.
        let (handle, view) = unsafe {
            let handle = CreateFileMappingW(INVALID_HANDLE_VALUE, None, PAGE_READWRITE, 0, size, PCWSTR(name.as_ptr()))
                .map_err(|e| format!("CreateFileMapping: {e}"))?;
            let view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size_of::<Shm>());
            if view.Value.is_null() {
                let _ = CloseHandle(handle);
                return Err("MapViewOfFile failed".into());
            }
            (handle, view)
        };
        let m = Self { handle, view, shm: view.Value.cast() };
        // SAFETY: shm points at a live view of size_of::<Shm>() bytes.
        unsafe {
            if (*m.shm).magic != MAGIC {
                m.write_settings(&Settings::preset(preset::MEDIUM));
                (*m.shm).version = VERSION;
                (*m.shm).size = size;
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
        // SAFETY: fields of a live view; a torn copy is detected by the counter and discarded.
        unsafe {
            let seq = AtomicU32::from_ptr(addr_of_mut!((*self.shm).settings_seq));
            for _ in 0..RETRIES {
                let before = seq.load(Ordering::Acquire);
                if before & 1 == 1 {
                    std::hint::spin_loop();
                    continue;
                }
                let s = read_volatile(addr_of_mut!((*self.shm).settings));
                fence(Ordering::Acquire);
                if seq.load(Ordering::Relaxed) == before {
                    return Some(s);
                }
            }
        }
        None
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        // SAFETY: the view and handle came from open() and are released once.
        unsafe {
            let _ = UnmapViewOfFile(self.view);
            let _ = CloseHandle(self.handle);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn settings_round_trip() -> Result<(), String> {
        let m = Mapping::open()?;
        let mut s = Settings::preset(preset::HIGH);
        s.debug_tint = 1;
        m.write_settings(&s);
        assert_eq!(m.settings(), Some(s));
        // A second mapping of the same name sees the same memory.
        assert_eq!(Mapping::open()?.settings(), Some(s));
        Ok(())
    }
}
