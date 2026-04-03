/*
 * stdlib/os/os.cp
 * ================
 * C-Prime OS Interface Library
 *
 * Provides cross-platform(ish) access to the operating system:
 *   - File system operations
 *   - Process management
 *   - Environment variables
 *   - System information
 *
 * Usage:
 *   import os;
 *   bool exists = os.file_exists("/etc/hostname");
 *   i32 ret = os.exec("ls", ["-la"]);
 */

import core;
import mem;
import string;

/* ─── File system ─────────────────────────────────────────────────────────── */

fn file_exists(`str path) -> bool {
    return __builtin_access(path, 0) == 0;   /* F_OK = 0 */
}

fn dir_exists(`str path) -> bool {
    /* stat the path and check if it's a directory */
    return __builtin_is_directory(path);
}

fn mkdir_p(`str path) -> Result<void, str> {
    /* Equivalent of mkdir -p */
    usize i = 1;
    while i <= string.len(path) {
        if i == string.len(path) || string.char_at(path, i) == '/' {
            str dir = string.slice(path, 0, i);
            i32 rc = __builtin_mkdir(dir, 0o755);
            if rc != 0 && __builtin_errno() != 17 {   /* EEXIST = 17 */
                return Err(fmt.sprintf("mkdir_p: cannot create '%s': %s",
                    dir, __builtin_strerror(__builtin_errno())));
            }
        }
        i = i + 1;
    }
    return Ok(void);
}

fn remove(`str path) -> void {
    __builtin_unlink(path);
}

fn remove_dir_all(`str path) -> Result<void, str> {
    /* Equivalent of rm -rf */
    i32 rc = __builtin_remove_tree(path);
    if rc != 0 {
        return Err(fmt.sprintf("remove_dir_all: cannot remove '%s'", path));
    }
    return Ok(void);
}

fn read_file(`str path) -> Result<str, str> {
    return io.read_file(path);
}

fn write_file(`str path, `str content) -> Result<void, str> {
    return io.write_file(path, content);
}

fn list_dir(`str path) -> Vec<str> {
    return __builtin_list_directory(path);
}

fn file_size(`str path) -> Result<u64, str> {
    i64 sz = __builtin_file_size(path);
    if sz < 0 { return Err(fmt.sprintf("file_size: cannot stat '%s'", path)); }
    return Ok(sz as u64);
}

fn file_mtime(`str path) -> i64 {
    return __builtin_file_mtime(path);
}

fn copy_file(`str src, `str dst) -> Result<void, str> {
    Result<str, str> content = read_file(src);
    str data = match content {
        Err(e) -> return Err(e),
        Ok(d)  -> d,
    };
    return write_file(dst, data);
}

fn rename_file(`str src, `str dst) -> Result<void, str> {
    i32 rc = __builtin_rename(src, dst);
    if rc != 0 {
        return Err(fmt.sprintf("rename: %s", __builtin_strerror(__builtin_errno())));
    }
    return Ok(void);
}

fn get_cwd() -> str {
    return __builtin_getcwd();
}

fn is_tty(i32 fd) -> bool {
    return __builtin_isatty(fd) != 0;
}

/* ─── Process management ──────────────────────────────────────────────────── */

fn get_pid() -> i32 {
    return __builtin_getpid();
}

fn get_args() -> str[] {
    return __builtin_argv();
}

fn get_argc() -> i32 {
    return __builtin_argc();
}

fn exit(i32 code) -> void {
    __builtin_exit(code);
}

/// Execute a command, wait for it to finish, return its exit code.
fn exec(`str program, str[] args) -> i32 {
    return __builtin_exec_wait(program, args);
}

/// Execute a command and replace the current process (exec, not fork+exec).
fn exec_replace(`str program, str[] args) -> i32 {
    return __builtin_execvp(program, args);
}

/// Spawn a background process without waiting for it.
fn spawn_background(`str program, str[] args) -> i32 {
    return __builtin_spawn_bg(program, args);
}

/// Spawn a detached closure in a new thread.
fn spawn_detached(fn() -> void f) -> void {
    __builtin_spawn_thread(f);
}

/* ─── Environment variables ───────────────────────────────────────────────── */

fn getenv(`str name) -> Option<str> {
    str val = __builtin_getenv(name);
    if string.is_empty(val) { return None; }
    return Some(val);
}

fn setenv(`str name, `str value) -> Result<void, str> {
    i32 rc = __builtin_setenv(name, value, 1);
    if rc != 0 { return Err("setenv failed"); }
    return Ok(void);
}

/* ─── Time ────────────────────────────────────────────────────────────────── */

/// Current UNIX timestamp (seconds since epoch).
fn now_unix() -> i64 {
    return __builtin_time(null);
}

/// Current time in milliseconds (for benchmarking).
fn now_millis() -> u64 {
    return __builtin_clock_millis();
}

/// Sleep for ms milliseconds.
fn sleep_ms(u32 ms) -> void {
    __builtin_usleep(ms * 1000);
}

/* ─── System information ──────────────────────────────────────────────────── */

fn get_hostname() -> str {
    return __builtin_gethostname();
}

fn get_username() -> str {
    return match getenv("USER") {
        Some(u) -> u,
        None    -> match getenv("LOGNAME") {
            Some(u) -> u,
            None    -> "unknown",
        },
    };
}

fn num_cpus() -> u32 {
    return __builtin_nprocs() as u32;
}

fn total_ram() -> u64 {
    return __builtin_total_ram();
}
