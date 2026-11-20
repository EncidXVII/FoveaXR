// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! Tray app views: Slint, built from design-tokens.json (shared with the sibling apps) by build.rs.

// Slint's generated code, not ours: its unwraps and todo!s sit on paths its compiler proved unreachable.
#[allow(clippy::unwrap_used, clippy::todo)]
mod generated {
    slint::include_modules!();
}
pub use generated::*;

pub mod offscreen;
