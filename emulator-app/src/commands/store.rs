// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 에뮬레이터의 스토어 앱 입출력 — 가상 기기의 /data/apps 대응
// 수행범위: OS 이미지의 스토어 설정(system/store.json) 제공, https 다운로드,
//           패키지 풀기(이름 검사: 절대 경로·..·링크 거부)·확정·버리기·삭제(앱
//           컨테이너까지), 설치된 스토어 앱 목록, 스토어 앱을 기기와 같은 출처
//           zyl-app://<id>/ 로 제공(/shared/·/keyboard/ 는 OS 코드). 봉인 대조용 자료
//           (파일마다 SHA-256, .zyl-seal/ 의 내용)와 스토어가 폐기한 인증서 목록 보관.
//           앱 파일에 붙는 CSP 는 OS(sandbox.js)가 정해 실행 전에 건넨 것을 그대로.
//           무엇을 받을지·서명·봉인·카탈로그 해석·app.json 확인은 OS
//           (apps/system/services/appstore.js, app-seal.js)의 몫
// 의존방향: state, appdata::container, config::bundled_app_dirs, curl, unzip/zipinfo, sha2
// SOLID: SRP — 스토어 앱 파일 입출력만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::commands::appdata::container;
use crate::commands::config::bundled_app_dirs;
use crate::state::AppState;
use base64::Engine;
use sha2::{Digest, Sha256};
use std::borrow::Cow;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;
use tauri::{Manager, State};

/// The OS image's store configuration, as the device has it in /etc/zyl-os
const STORE_CONFIG: &str = include_str!("../../../system/store.json");

/// Where store apps live on the virtual device (out of the file service's reach)
fn store_root(app_state: &AppState) -> PathBuf {
    let base = app_state.mount_point.clone().unwrap_or_else(|| app_state.data_dir.clone());
    base.join(".system").join("apps")
}

/// <root>/<id>, the id checked like a container's
fn app_dir(app_state: &AppState, app_id: &str) -> Result<PathBuf, String> {
    container(app_state, app_id)?;
    Ok(store_root(app_state).join(app_id))
}

fn staging(app_state: &AppState, app_id: &str) -> Result<PathBuf, String> {
    container(app_state, app_id)?;
    Ok(store_root(app_state).join(format!(".staging-{}", app_id)))
}

#[tauri::command]
pub fn store_config() -> String {
    STORE_CONFIG.to_string()
}

/// https only (redirects too); the bytes as base64 — what to trust is the OS's to check
#[tauri::command]
pub async fn store_download(url: String, max: u64) -> Result<String, String> {
    if !url.starts_with("https://") || url.len() > 2048 {
        return Err("https only".into());
    }
    let (tx, rx) = tokio::sync::oneshot::channel();
    std::thread::spawn(move || {
        let out = Command::new("curl")
            .args(["-s", "-f", "-L", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "3",
                   "--connect-timeout", "10", "-m", "300", "--max-filesize"])
            .arg(max.to_string())
            .arg("--")
            .arg(&url)
            .output();
        let r = match out {
            Ok(o) if o.status.success() => Ok(base64::engine::general_purpose::STANDARD.encode(o.stdout)),
            Ok(o) => Err(format!("download failed: {}", o.status)),
            Err(e) => Err(format!("curl: {}", e)),
        };
        let _ = tx.send(r);
    });
    rx.await.map_err(|_| "cancelled".to_string())?
}

/// A package entry name that cannot leave the directory it is unpacked into
fn safe_entry(name: &str) -> bool {
    !name.is_empty()
        && !name.starts_with('/')
        && !name.contains('\\')
        && name.split('/').enumerate().all(|(i, p)| {
            p != ".." && p != "." && (!p.is_empty() || i + 1 == name.split('/').count())
        })
}

/// What the OS checks a seal against: every file's SHA-256 by its relative path
/// (anything but a plain file as "", which no seal matches) and the seal's own
/// files (.zyl-seal/*) as text
fn inspect(dir: &Path) -> serde_json::Value {
    fn walk(root: &Path, dir: &Path, hashes: &mut serde_json::Map<String, serde_json::Value>) {
        for entry in fs::read_dir(dir).into_iter().flatten().flatten() {
            let path = entry.path();
            let rel = path.strip_prefix(root).unwrap_or(&path).to_string_lossy().replace('\\', "/");
            match fs::symlink_metadata(&path) {
                Ok(m) if m.is_dir() => walk(root, &path, hashes),
                Ok(m) if m.is_file() => {
                    let hex = fs::read(&path).map(|b| format!("{:x}", Sha256::digest(&b))).unwrap_or_default();
                    hashes.insert(rel, hex.into());
                }
                _ => { hashes.insert(rel, "".into()); }
            }
        }
    }
    let mut hashes = serde_json::Map::new();
    walk(dir, dir, &mut hashes);
    let mut seal = serde_json::Map::new();
    for entry in fs::read_dir(dir.join(".zyl-seal")).into_iter().flatten().flatten() {
        if let Ok(text) = fs::read_to_string(entry.path()) {
            seal.insert(entry.file_name().to_string_lossy().into_owned(), text.into());
        }
    }
    serde_json::json!({ "hashes": hashes, "seal": seal })
}

/// Unpack into staging (named by the app id, or a placeholder when the id is not
/// known yet: a package from the user's files); { manifest: app.json text,
/// hashes, seal } for the OS to check before it commits or discards
#[tauri::command]
pub fn store_unpack(app_id: String, package: String, state: State<'_, Mutex<AppState>>) -> Result<serde_json::Value, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let stage = staging(&app_state, &app_id)?;
    let _ = fs::remove_dir_all(&stage);
    fs::create_dir_all(&stage).map_err(|e| format!("Create error: {}", e))?;
    let zip = stage.join("package.zip");
    let bytes = base64::engine::general_purpose::STANDARD.decode(package).map_err(|_| "not base64")?;
    fs::write(&zip, bytes).map_err(|e| format!("Write error: {}", e))?;

    let names = Command::new("zipinfo").arg("-1").arg(&zip).output().map_err(|e| e.to_string())?;
    let long = Command::new("zipinfo").arg(&zip).output().map_err(|e| e.to_string())?;
    if !names.status.success() {
        return Err("not a package".into());
    }
    let listing = String::from_utf8_lossy(&names.stdout);
    let entries: Vec<&str> = listing.lines().collect();
    if entries.is_empty() || entries.len() > 4096 || !entries.iter().all(|n| safe_entry(n)) {
        return Err("unsafe path in package".into());
    }
    // Only plain files and folders: a link could point anywhere once unpacked
    if String::from_utf8_lossy(&long.stdout).lines().any(|l| l.starts_with('l')) {
        return Err("link in package".into());
    }
    let out = stage.join("app");
    let ok = Command::new("unzip").args(["-qq", "-o"]).arg(&zip).arg("-d").arg(&out)
        .status().map_err(|e| e.to_string())?.success();
    if !ok {
        return Err("cannot unpack".into());
    }
    let manifest = fs::read_to_string(out.join("app.json")).map_err(|_| "no app.json".to_string())?;
    let mut found = inspect(&out);
    found["manifest"] = manifest.into();
    Ok(found)
}

/// An installed store app's files, for the OS to check its seal before listing it
#[tauri::command]
pub fn store_inspect(app_id: String, state: State<'_, Mutex<AppState>>) -> Result<serde_json::Value, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(inspect(&app_dir(&app_state, &app_id)?))
}

/// The unpacked app (staged as stage, or as app_id) replaces the installed one
/// (an update keeps the container)
#[tauri::command]
pub fn store_commit(app_id: String, stage: Option<String>, state: State<'_, Mutex<AppState>>) -> Result<bool, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let stage = staging(&app_state, stage.as_deref().unwrap_or(&app_id))?;
    let target = app_dir(&app_state, &app_id)?;
    let old = stage.join("old");
    let had = target.exists();
    if had {
        fs::rename(&target, &old).map_err(|e| format!("Rename error: {}", e))?;
    }
    if let Err(e) = fs::rename(stage.join("app"), &target) {
        if had {
            let _ = fs::rename(&old, &target);
        }
        return Err(format!("Rename error: {}", e));
    }
    let _ = fs::remove_dir_all(&stage);
    Ok(true)
}

#[tauri::command]
pub fn store_discard(app_id: String, state: State<'_, Mutex<AppState>>) -> Result<bool, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let _ = fs::remove_dir_all(staging(&app_state, &app_id)?);
    Ok(true)
}

/// The app and its whole container: nothing it made is left
#[tauri::command]
pub fn store_remove(app_id: String, state: State<'_, Mutex<AppState>>) -> Result<bool, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    for dir in [app_dir(&app_state, &app_id)?, container(&app_state, &app_id)?] {
        if dir.exists() {
            fs::remove_dir_all(&dir).map_err(|e| format!("Remove error: {}", e))?;
        }
    }
    Ok(true)
}

/// The developer certificates the store last listed as revoked (list: replace
/// them); what is kept
#[tauri::command]
pub fn store_revoked(list: Option<Vec<String>>, state: State<'_, Mutex<AppState>>) -> Result<Vec<String>, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let path = store_root(&app_state).join(".revoked");
    if let Some(list) = list {
        fs::create_dir_all(store_root(&app_state)).map_err(|e| format!("Create error: {}", e))?;
        fs::write(&path, list.join("\n")).map_err(|e| format!("Write error: {}", e))?;
    }
    Ok(fs::read_to_string(&path).unwrap_or_default().lines().map(str::to_string).collect())
}

/// Installed store apps, raw app.json data marked "source": "store"
pub fn installed_store_apps(app_state: &AppState) -> Vec<serde_json::Value> {
    let mut apps = Vec::new();
    if let Ok(entries) = fs::read_dir(store_root(app_state)) {
        for entry in entries.flatten() {
            if entry.file_name().to_string_lossy().starts_with('.') {
                continue;
            }
            let text = fs::read_to_string(entry.path().join("app.json")).unwrap_or_default();
            if let Ok(mut m) = serde_json::from_str::<serde_json::Value>(&text) {
                if let Some(obj) = m.as_object_mut() {
                    obj.insert("installed".into(), serde_json::json!(true));
                    obj.insert("system".into(), serde_json::json!(false));
                    obj.insert("source".into(), serde_json::json!("store"));
                    apps.push(m);
                }
            }
        }
    }
    apps
}

/// The OS code every page includes, whatever the app (the device's
/// app_scheme.c has the same list): the shared runtime and the on-screen
/// keyboard shared/bridge.js loads into the page
const OS_DIRS: &[&str] = &["shared", "keyboard"];

/// The file a zyl-app path names: an OS path from that directory of the
/// image's apps (os_dir), anything else the app's own directory; nothing outside
fn resolve(app_dir: &Path, os_dir: &Path, path: &str) -> Option<PathBuf> {
    if !path.starts_with('/') || path.split('/').any(|p| p == "..") {
        return None;
    }
    let os = OS_DIRS.iter().find_map(|d| path[1..].strip_prefix(d)?.strip_prefix('/').map(|rest| (*d, rest)));
    let base = match os {
        Some((d, _)) => os_dir.join(d),
        None => app_dir.to_path_buf(),
    };
    let rel = os.map_or(&path[1..], |(_, rest)| rest);
    let full = base.join(rel).canonicalize().ok()?;
    let root = base.canonicalize().ok()?;
    (full.starts_with(&root) && full.is_file()).then_some(full)
}

fn mime(path: &Path) -> &'static str {
    match path.extension().and_then(|e| e.to_str()).unwrap_or("").to_ascii_lowercase().as_str() {
        "html" | "htm" => "text/html",
        "js" | "mjs" => "text/javascript",
        "css" => "text/css",
        "json" => "application/json",
        "svg" => "image/svg+xml",
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "gif" => "image/gif",
        "webp" => "image/webp",
        "woff2" => "font/woff2",
        "woff" => "font/woff",
        "ttf" => "font/ttf",
        "mp3" => "audio/mpeg",
        "ogg" => "audio/ogg",
        "wav" => "audio/wav",
        "mp4" => "video/mp4",
        "webm" => "video/webm",
        "txt" => "text/plain",
        _ => "application/octet-stream",
    }
}

/// The CSP the OS made for a store app from its permissions (sandbox.js), set
/// before each launch; serve() sends it with every file, as the WAM does
#[tauri::command]
pub fn store_app_policy(app_id: String, csp: String, state: State<'_, Mutex<AppState>>) -> Result<(), String> {
    let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    container(&app_state, &app_id)?;
    app_state.app_policies.insert(app_id, csp);
    Ok(())
}

/// zyl-app://<id>/<path>: a store app as its own origin, as on the device
pub fn serve(app: &tauri::AppHandle, request: &tauri::http::Request<Vec<u8>>) -> tauri::http::Response<Cow<'static, [u8]>> {
    let not_found = || {
        tauri::http::Response::builder().status(404).body(Cow::Borrowed(&b""[..])).unwrap()
    };
    let uri = request.uri();
    let (Some(id), path) = (uri.host(), uri.path()) else { return not_found() };
    let state = app.state::<Mutex<AppState>>();
    let Ok(app_state) = state.lock() else { return not_found() };
    let Ok(dir) = app_dir(&app_state, id) else { return not_found() };
    let os_dir = bundled_app_dirs().into_iter().find(|d| d.join("shared").is_dir());
    let Some(file) = os_dir.and_then(|o| resolve(&dir, &o, path)) else { return not_found() };
    // No policy from the OS, nothing served: closed, never a default
    let Some(csp) = app_state.app_policies.get(id) else { return not_found() };
    match fs::read(&file) {
        Ok(bytes) => tauri::http::Response::builder()
            .header("Content-Type", mime(&file))
            .header("Content-Security-Policy", csp.as_str())
            .body(Cow::Owned(bytes))
            .unwrap(),
        Err(_) => not_found(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn entries_cannot_leave() {
        assert!(safe_entry("app.json") && safe_entry("js/app.js") && safe_entry("assets/"));
        assert!(!safe_entry("../x") && !safe_entry("/etc/x") && !safe_entry("a/../../x"));
        assert!(!safe_entry("a\\b") && !safe_entry("") && !safe_entry("a//b") && !safe_entry("./a"));
    }

    #[test]
    fn paths_stay_in_the_app_or_the_os_code() {
        let root = std::env::temp_dir().join(format!("zyl-store-rs-{}", std::process::id()));
        let (app, os) = (root.join("app"), root.join("os"));
        fs::create_dir_all(app.join("js")).unwrap();
        fs::create_dir_all(os.join("shared")).unwrap();
        fs::create_dir_all(os.join("keyboard/js")).unwrap();
        fs::create_dir_all(os.join("settings")).unwrap();
        fs::write(app.join("index.html"), "x").unwrap();
        fs::write(os.join("shared/bridge.js"), "x").unwrap();
        fs::write(os.join("keyboard/js/inpage.js"), "x").unwrap();
        fs::write(os.join("settings/x.js"), "x").unwrap();
        fs::write(root.join("secret"), "x").unwrap();
        assert!(resolve(&app, &os, "/index.html").is_some());
        assert!(resolve(&app, &os, "/shared/bridge.js").is_some());
        assert!(resolve(&app, &os, "/keyboard/js/inpage.js").is_some());
        assert!(resolve(&app, &os, "/settings/x.js").is_none());
        assert!(resolve(&app, &os, "/../secret").is_none());
        assert!(resolve(&app, &os, "/shared/../secret").is_none());
        assert!(resolve(&app, &os, "/js").is_none());
        assert_eq!(mime(Path::new("a.JS")), "text/javascript");
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn inspect_hashes_every_file_and_reads_the_seal() {
        let root = std::env::temp_dir().join(format!("zyl-store-inspect-{}", std::process::id()));
        fs::create_dir_all(root.join("js")).unwrap();
        fs::create_dir_all(root.join(".zyl-seal")).unwrap();
        fs::write(root.join("js/app.js"), "x=1\n").unwrap();
        fs::write(root.join(".zyl-seal/seal.sig"), "c2ln").unwrap();
        #[cfg(unix)]
        std::os::unix::fs::symlink("/etc/passwd", root.join("pw")).unwrap();
        let v = inspect(&root);
        assert_eq!(v["hashes"]["js/app.js"], "98752ee28d5484bdc2814fb70adb6a0b2fb31f6a9b8ee7ae81fd2fc9cf300b3b");
        assert_eq!(v["seal"]["seal.sig"], "c2ln");
        assert!(v["hashes"][".zyl-seal/seal.sig"].is_string());
        #[cfg(unix)]
        assert_eq!(v["hashes"]["pw"], "");
        let _ = fs::remove_dir_all(&root);
    }
}
