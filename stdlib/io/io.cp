/*
 * stdlib/io/io.cp
 * ================
 * C-Prime I/O Standard Library
 * Wraps OS-level I/O with safe, ergonomic APIs.
 *
 * Usage:
 *   import io;
 *   io.println("Hello!");
 *   str line = io.read_line();
 */

import core;
import mem;

/* ─── Output ──────────────────────────────────────────────────────────────── */

/// Print a string followed by a newline.
fn println(`str s) -> void {
    __builtin_write(1, s.ptr, s.len);
    __builtin_write(1, "\n", 1);
}

/// Print a string without a newline.
fn print(`str s) -> void {
    __builtin_write(1, s.ptr, s.len);
}

/// Printf-style formatted output.
/// Format string uses C-style format specifiers: %d, %s, %f, %x, etc.
fn printf(`str fmt, ...) -> void {
    /* Implemented as a compiler intrinsic wrapping libc printf */
    __builtin_printf(fmt, ...);
}

/// Print to stderr.
fn eprint(`str s) -> void {
    __builtin_write(2, s.ptr, s.len);
}

/// Print to stderr with newline.
fn eprintln(`str s) -> void {
    __builtin_write(2, s.ptr, s.len);
    __builtin_write(2, "\n", 1);
}

/// Printf to stderr.
fn eprintf(`str fmt, ...) -> void {
    __builtin_fprintf(2, fmt, ...);
}

/* ─── Input ───────────────────────────────────────────────────────────────── */

/// Read a single line from stdin (strips trailing newline).
/// Returns Err if EOF or read error.
fn read_line() -> Result<str, str> {
    /* Dynamic buffer — grows as needed */
    usize cap = 128;
    u8* buf   = mem.alloc(cap);
    usize len = 0;

    while true {
        if len >= cap {
            cap = cap * 2;
            buf = mem.realloc(buf, cap);
            if buf == null { return Err("out of memory"); }
        }

        i32 c = __builtin_getchar();
        if c == -1 {   /* EOF */
            if len == 0 { mem.free(buf); return Err("EOF"); }
            break;
        }
        if c == '\n' { break; }
        buf[len] = c as u8;
        len = len + 1;
    }

    return Ok(str.from_raw(buf, len));
}

/// Read all of stdin until EOF.
fn read_all_stdin() -> Result<str, str> {
    usize cap = 4096;
    u8*   buf = mem.alloc(cap);
    usize len = 0;

    while true {
        if len >= cap {
            cap = cap * 2;
            buf = mem.realloc(buf, cap);
            if buf == null { return Err("out of memory"); }
        }
        i32 c = __builtin_getchar();
        if c == -1 { break; }
        buf[len] = c as u8;
        len = len + 1;
    }

    return Ok(str.from_raw(buf, len));
}

/// Prompt the user and read a line.
fn prompt(`str message) -> Result<str, str> {
    print(message);
    return read_line();
}

/* ─── File I/O ────────────────────────────────────────────────────────────── */

enum FileMode {
    Read,
    Write,
    Append,
    ReadWrite,
}

struct File {
    usize handle;    /* OS file descriptor */
    bool  is_open;
    str   path;
}

fn open(`str path, FileMode mode) -> Result<File, str> {
    i32 flags = match mode {
        FileMode.Read      -> 0,       /* O_RDONLY */
        FileMode.Write     -> 577,     /* O_WRONLY | O_CREAT | O_TRUNC */
        FileMode.Append    -> 1089,    /* O_WRONLY | O_CREAT | O_APPEND */
        FileMode.ReadWrite -> 2,       /* O_RDWR */
    };

    i32 fd = __builtin_open(path.ptr, flags, 0o644);
    if fd < 0 {
        return Err(__builtin_strerror(__builtin_errno()));
    }

    return Ok(File { handle: fd as usize, is_open: true, path: path });
}

fn File.read_all(`mut File self) -> Result<str, str> {
    if !self.is_open { return Err("file is not open"); }

    /* Get file size */
    i64 size = __builtin_lseek(self.handle as i32, 0, 2);   /* SEEK_END */
    __builtin_lseek(self.handle as i32, 0, 0);               /* SEEK_SET */

    if size < 0 { return Err("could not get file size"); }

    u8* buf = mem.alloc(size as usize + 1);
    if buf == null { return Err("out of memory"); }

    isize n = __builtin_read(self.handle as i32, buf, size as usize);
    if n < 0 {
        mem.free(buf);
        return Err("read error");
    }

    buf[n as usize] = 0;
    return Ok(str.from_raw(buf, n as usize));
}

fn File.write(`mut File self, `str data) -> Result<usize, str> {
    if !self.is_open { return Err("file is not open"); }
    isize n = __builtin_write(self.handle as i32, data.ptr, data.len);
    if n < 0 { return Err("write error"); }
    return Ok(n as usize);
}

fn File.close(`mut File self) -> void {
    if self.is_open {
        __builtin_close(self.handle as i32);
        self.is_open = false;
    }
}

/// Read entire file to string (convenience wrapper).
fn read_file(`str path) -> Result<str, str> {
    Result<File, str> file_result = open(path, FileMode.Read);
    File f = match file_result {
        Ok(f)  -> f,
        Err(e) -> return Err(e),
    };
    Result<str, str> content = f.read_all();
    f.close();
    return content;
}

/// Write string to file (convenience wrapper).
fn write_file(`str path, `str content) -> Result<void, str> {
    Result<File, str> file_result = open(path, FileMode.Write);
    File f = match file_result {
        Ok(f)  -> f,
        Err(e) -> return Err(e),
    };
    match f.write(content) {
        Err(e) -> { f.close(); return Err(e); },
        Ok(_)  -> {},
    }
    f.close();
    return Ok(void);
}
