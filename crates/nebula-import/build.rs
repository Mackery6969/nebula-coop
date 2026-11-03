//! Generate build fingerprint from git metadata at compile time.

use std::env;
use std::fs;
use std::path::Path;

fn main() {
    let out_dir = env::var("OUT_DIR").unwrap();
    let dest_path = Path::new(&out_dir).join("version.rs");

    let commit_hash = get_commit_hash();
    let commit_date = get_commit_date();

    fs::write(
        &dest_path,
        format!(
            r#"/// Semantic version (from Cargo.toml workspace).
pub const SEMVER: &str = "{semver}";

/// Short git commit hash (7 chars).
pub const COMMIT: &str = "{commit}";

/// Commit date (YYYY-MM-DD).
pub const COMMIT_DATE: &str = "{date}";

/// Full fingerprint: `semver+commit` (e.g. `0.1.0+a1b2c3d`).
pub const FINGERPRINT: &str = "{semver}+{commit}";
"#,
            semver = env::var("CARGO_PKG_VERSION").unwrap_or_default(),
            commit = commit_hash,
            date = commit_date,
        ),
    )
    .unwrap();

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=.git");
}

fn get_commit_hash() -> String {
    run_cmd("git", &["rev-parse", "--short=7", "HEAD"])
        .unwrap_or("unknown".to_string())
}

fn get_commit_date() -> String {
    run_cmd("git", &["log", "-1", "--format=%Y-%m-%d"])
        .unwrap_or("unknown".to_string())
}

fn run_cmd(cmd: &str, args: &[&str]) -> Option<String> {
    let output = std::process::Command::new(cmd)
        .args(args)
        .output()
        .ok()?;
    if output.status.success() {
        Some(String::from_utf8_lossy(&output.stdout).trim().to_string())
    } else {
        None
    }
}
