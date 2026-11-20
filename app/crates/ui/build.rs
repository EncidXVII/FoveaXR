// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

//! design-tokens.json (shared with the sibling apps) is the single source of colours, type, spacing and motion. It
//! becomes the `Tokens` Slint global here, desktop sizes applied, so a value can never drift from the file.

use std::error::Error;
use std::fmt::Write;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn Error>> {
    let dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR")?);
    let tokens_path = dir.join("../../../design-tokens.json");
    println!("cargo:rerun-if-changed={}", tokens_path.display());
    let json: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(&tokens_path)?)?;

    let mut s = String::from(
        "// Generated from design-tokens.json by build.rs.
export global Mode {
    in-out property <bool> reduce-motion;
    // Index into Tokens.themes: the accent follows the user's choice.
    in-out property <int> theme;
}
export global Tokens {
",
    );
    let group = |name: &str| json[name].as_object().ok_or(format!("tokens: missing {name}"));
    let mut line = |ty: &str, name: String, value: String| {
        writeln!(s, "    out property <{ty}> {}: {value};", name.replace('_', "-"))
    };
    let themes = json["themes"].as_array().ok_or("tokens: missing themes")?;
    let list = |key: &str| -> Result<String, String> {
        let items: Result<Vec<String>, String> = themes
            .iter()
            .map(|t| {
                let v = t[key].as_str().ok_or(format!("theme {key} must be a string"))?;
                Ok(if key == "name" { format!("{v:?}") } else { v.to_owned() })
            })
            .collect();
        Ok(format!("[{}]", items?.join(", ")))
    };
    line("[color]", "themes".into(), list("accent")?)?;
    line("[color]", "themes-pressed".into(), list("pressed")?)?;
    line("[string]", "theme-names".into(), list("name")?)?;
    for (k, v) in group("color")? {
        let value = match k.as_str() {
            "accent" => "themes[Mode.theme]".to_owned(),
            "accent_pressed" => "themes-pressed[Mode.theme]".to_owned(),
            _ => v.as_str().ok_or("color must be a string")?.to_owned(),
        };
        line("color", k.clone(), value)?;
    }
    for (k, v) in group("font")? {
        line("string", format!("font-{k}"), format!("{v}"))?;
    }
    // A desktop window: the `desktop` overrides win over the VR sizes the sibling apps also use.
    for (prefix, unit) in [("type", "px"), ("space", "px"), ("radius", "px"), ("size", "px")] {
        for (k, v) in group(prefix)? {
            let value = json["desktop"][prefix].get(k).unwrap_or(v);
            line("length", format!("{prefix}-{k}"), format!("{value}{unit}"))?;
        }
    }
    for (k, v) in group("motion")? {
        line("duration", format!("motion-{}", k.trim_end_matches("_ms")), format!("{v}ms"))?;
    }
    s.push_str("}\n");

    let out = PathBuf::from(std::env::var("OUT_DIR")?).join("tokens.slint");
    std::fs::write(&out, s)?;
    println!("cargo:rerun-if-changed={}", dir.join("ui").display());
    let config = slint_build::CompilerConfiguration::new()
        .with_library_paths(std::collections::HashMap::from([("tokens".to_owned(), out)]));
    slint_build::compile_with_config("ui/app.slint", config)?;
    Ok(())
}
