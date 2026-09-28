// Copyright 2026 Owear Contributors — SPDX-License-Identifier: Apache-2.0
// benchmarks/tauri/src-tauri/src/main.rs — comandos de benchmark (IPC + eventos).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde_json::Value;
use tauri::Emitter;

const OUT: &str = "/tmp/opencode/bench/out/tauri.json";

#[tauri::command]
fn echo(payload: Value) -> Value {
    payload
}

#[tauri::command]
fn burst(window: tauri::Window, n: usize) -> Result<(), String> {
    for i in 0..n {
        window.emit("bench-tick", i).map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[tauri::command]
fn ready() {
    println!("BENCH_READY");
    use std::io::Write;
    let _ = std::io::stdout().flush();
}

#[tauri::command]
fn report(json: String) -> Result<(), String> {
    std::fs::write(OUT, json).map_err(|e| e.to_string())
}

#[tauri::command]
fn readfile() -> tauri::ipc::Response {
    let data = std::fs::read("/tmp/opencode/bench/blob.bin").unwrap_or_default();
    tauri::ipc::Response::new(data)
}

#[tauri::command]
fn big(n: usize) -> String {
    "y".repeat(n)
}

#[tauri::command]
fn done(app: tauri::AppHandle) {
    app.exit(0);
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![echo, burst, ready, report, readfile, big, done])
        .run(tauri::generate_context!())
        .expect("error running tauri app");
}
