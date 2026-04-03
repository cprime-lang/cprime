/*
 * cppm — Update Checker
 * pkgman/src/updater/check.cp
 * ============================
 * Checks GitHub releases API for new versions of cpc, cpg, and cppm.
 * Runs in the background on every cppm invocation (async, non-blocking).
 * Writes update status to ~/.cppm/update_cache.json to avoid hammering the API.
 *
 * Cache TTL: 24 hours (configurable via ~/.cppm/config.json)
 */

import core;
import io;
import net;
import os;
import json;
import string;
import fmt;

const str RELEASES_API     = "https://api.github.com/repos/cprime-lang/cprime/releases/latest";
const str UPDATE_CACHE     = "~/.cppm/update_cache.json";
const i64 CACHE_TTL_SECS   = 86400;   /* 24 hours */
const str CPPM_VERSION     = "0.1.0-alpha";

struct UpdateInfo {
    str  latest_version;
    str  release_url;
    str  release_notes_url;
    bool has_deb;
    bool has_vslnx;
    str  deb_url;
    str  vslnx_url;
    i64  published_ts;
}

/* ─── Check and notify ────────────────────────────────────────────────────── */
fn check_for_updates(bool verbose) -> i32 {
    /* Read cache */
    if cache_is_fresh() && !verbose {
        Option<str> cached_msg = read_cache_msg();
        match cached_msg {
            Some(msg) -> { if !string.is_empty(msg) { io.println(msg); } },
            None      -> {},
        }
        return 0;
    }

    if verbose { io.println("[cppm] Contacting update server..."); }

    match fetch_latest_release() {
        Err(e) -> {
            if verbose {
                io.eprintf("[cppm] Warning: could not check for updates: %s\n", e);
            }
            write_cache_msg("");  /* don't keep retrying on network failure */
            return 0;
        },
        Ok(info) -> {
            write_cache(info);

            if version_newer(info.latest_version, CPPM_VERSION) {
                str msg = fmt.sprintf(
                    "\n\033[33m╔══════════════════════════════════════════════╗\n"
                    "║  \033[1mNew C-Prime release available!\033[0m\033[33m              ║\n"
                    "║  Latest:  %-34s║\n"
                    "║  Current: %-34s║\n"
                    "║                                              ║\n"
                    "║  Download: %-34s║\n"
                    "║  Install:  sudo dpkg -i cprime_*.deb         ║\n"
                    "╚══════════════════════════════════════════════╝\033[0m\n",
                    info.latest_version,
                    CPPM_VERSION,
                    info.release_url
                );
                io.println(msg);
                write_cache_msg(msg);
                if verbose { return 0; }
            } else {
                if verbose {
                    io.printf("[cppm] \033[32mUp to date\033[0m — C-Prime v%s is the latest.\n",
                              CPPM_VERSION);
                }
                write_cache_msg("");
            }
            return 0;
        },
    }
}

/* ─── Background check (spawned async from main) ─────────────────────────── */
fn check_updates_background() -> void {
    /* This runs in a detached goroutine-style call.
       It checks silently and only prints if a new version is found. */
    if cache_is_fresh() { return; }
    check_for_updates(false);
}

/* ─── Fetch from GitHub API ───────────────────────────────────────────────── */
fn fetch_latest_release() -> Result<UpdateInfo, str> {
    Result<str, str> resp = net.http_get_with_headers(RELEASES_API, [
        { name: "User-Agent", value: fmt.sprintf("cppm/%s", CPPM_VERSION) },
        { name: "Accept",     value: "application/vnd.github.v3+json" },
    ]);

    str body = match resp {
        Err(e) -> return Err(e),
        Ok(b)  -> b,
    };

    Result<JsonValue, str> parsed = json.parse(body);
    JsonValue data = match parsed {
        Err(e) -> return Err(fmt.sprintf("JSON parse error: %s", e)),
        Ok(v)  -> v,
    };

    UpdateInfo info;
    info.latest_version    = json.get_str(`data, "tag_name");
    info.release_url       = json.get_str(`data, "html_url");
    info.published_ts      = os.now_unix();

    /* Scan assets for .deb and .vslnx */
    JsonValue assets = json.get_array(`data, "assets");
    for asset in assets {
        str name = json.get_str(`asset, "name");
        if string.ends_with(name, ".deb") {
            info.has_deb  = true;
            info.deb_url  = json.get_str(`asset, "browser_download_url");
        }
        if string.ends_with(name, ".vslnx") {
            info.has_vslnx  = true;
            info.vslnx_url  = json.get_str(`asset, "browser_download_url");
        }
    }

    return Ok(info);
}

/* ─── Version comparison ──────────────────────────────────────────────────── */
fn version_newer(`str a, `str b) -> bool {
    /* Strip leading 'v' */
    str va = if string.starts_with(a, "v") {
        string.slice(a, 1, string.len(a))
    } else { a };
    str vb = if string.starts_with(b, "v") {
        string.slice(b, 1, string.len(b))
    } else { b };

    /* Split on '.' and compare numerically */
    str[] pa = string.split(va, '.');
    str[] pb = string.split(vb, '.');
    usize len = if pa.len > pb.len { pa.len } else { pb.len };

    usize i = 0;
    while i < len {
        i64 na = if i < pa.len { string.parse_i64(pa[i]) } else { 0 };
        i64 nb = if i < pb.len { string.parse_i64(pb[i]) } else { 0 };
        if na > nb { return true;  }
        if na < nb { return false; }
        i = i + 1;
    }
    return false;
}

/* ─── Cache helpers ───────────────────────────────────────────────────────── */
fn cache_is_fresh() -> bool {
    if !os.file_exists(UPDATE_CACHE) { return false; }
    i64 mtime = os.file_mtime(UPDATE_CACHE);
    i64 now   = os.now_unix();
    return (now - mtime) < CACHE_TTL_SECS;
}

fn read_cache_msg() -> Option<str> {
    if !os.file_exists(UPDATE_CACHE) { return None; }
    match io.read_file(UPDATE_CACHE) {
        Err(_)   -> return None,
        Ok(body) -> {
            match json.parse(body) {
                Err(_)   -> return None,
                Ok(data) -> {
                    str msg = json.get_str(`data, "notification");
                    return Some(msg);
                },
            }
        },
    }
}

fn write_cache(`UpdateInfo info) -> void {
    str content = fmt.sprintf(
        "{\"latest\":\"%s\",\"url\":\"%s\",\"ts\":%d,\"notification\":\"\"}",
        info.latest_version, info.release_url, info.published_ts);
    os.mkdir_p("~/.cppm");
    io.write_file(UPDATE_CACHE, content);
}

fn write_cache_msg(`str msg) -> void {
    str escaped = string.replace(msg, "\"", "\\\"");
    str content = fmt.sprintf(
        "{\"ts\":%d,\"notification\":\"%s\"}", os.now_unix(), escaped);
    os.mkdir_p("~/.cppm");
    io.write_file(UPDATE_CACHE, content);
}
