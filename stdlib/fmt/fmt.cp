/*
 * stdlib/fmt/fmt.cp
 * ===================
 * C-Prime Format Library
 *
 * Provides sprintf-style string formatting.
 * Safe wrappers around libc printf family.
 *
 * Usage:
 *   import fmt;
 *   str s = fmt.sprintf("Hello %s, you are %d years old", name, age);
 */

import core;
import mem;
import string;

/* ─── sprintf ─────────────────────────────────────────────────────────────── */

/// Format a string. Returns a heap-allocated string.
/// Uses printf format specifiers: %d %s %f %x %c %p %%
fn sprintf(`str format, ...) -> str {
    /* Use a dynamic buffer that grows if needed */
    usize cap = 256;
    u8*   buf = mem.alloc(cap);

    while true {
        i32 needed = __builtin_vsnprintf(buf, cap, format, ...);
        if needed < 0 { break; }                      /* error */
        if (needed as usize) < cap { break; }          /* fit */
        /* Need more space */
        cap = (needed as usize) + 1;
        buf = mem.realloc(buf, cap);
    }

    return string.from_raw(buf, __builtin_strlen(buf));
}

/// Format a string into an existing buffer. Returns number of bytes written.
fn snprintf(u8* buf, usize size, `str format, ...) -> i32 {
    return __builtin_vsnprintf(buf, size, format, ...);
}

/// Format a variadic argument list. Used internally by sprintf.
fn vsprintf(`str format, ...) -> str {
    return sprintf(format, ...);
}

/* ─── Number to string ────────────────────────────────────────────────────── */

fn i32_to_str(i32 n) -> str { return sprintf("%d", n); }
fn i64_to_str(i64 n) -> str { return sprintf("%lld", n); }
fn u32_to_str(u32 n) -> str { return sprintf("%u", n); }
fn u64_to_str(u64 n) -> str { return sprintf("%llu", n); }
fn f32_to_str(f32 n) -> str { return sprintf("%f", n as f64); }
fn f64_to_str(f64 n) -> str { return sprintf("%f", n); }
fn f64_to_str_prec(f64 n, u32 prec) -> str { return sprintf(sprintf("%%.%df", prec), n); }
fn bool_to_str(bool b) -> str { return if b { "true" } else { "false" }; }

/// Format integer as hex: 0xDEADBEEF
fn hex(u64 n) -> str { return sprintf("0x%llX", n); }
fn hex_lower(u64 n) -> str { return sprintf("0x%llx", n); }

/// Format integer with thousands separator: 1,234,567
fn thousands(i64 n) -> str {
    if n < 0 { return string.concat("-", thousands(-n)); }
    if n < 1000 { return i64_to_str(n); }
    str rest = thousands(n / 1000);
    return sprintf("%s,%03lld", rest, n % 1000);
}

/// Left-pad a string to width with fill character.
fn pad_left(`str s, usize width, char fill) -> str {
    usize sl = string.len(s);
    if sl >= width { return s; }
    return string.concat(string.repeat(string.from_char(fill), width - sl), s);
}

/// Right-pad a string to width with fill character.
fn pad_right(`str s, usize width, char fill) -> str {
    usize sl = string.len(s);
    if sl >= width { return s; }
    return string.concat(s, string.repeat(string.from_char(fill), width - sl));
}

/// Center a string within width with fill character.
fn center(`str s, usize width, char fill) -> str {
    usize sl = string.len(s);
    if sl >= width { return s; }
    usize total_pad = width - sl;
    usize left_pad  = total_pad / 2;
    usize right_pad = total_pad - left_pad;
    return string.concat(
        string.concat(string.repeat(string.from_char(fill), left_pad), s),
        string.repeat(string.from_char(fill), right_pad)
    );
}

/* ─── Human-readable sizes ────────────────────────────────────────────────── */

fn bytes_human(u64 n) -> str {
    if n < 1024         { return sprintf("%llu B",   n); }
    if n < 1024*1024    { return sprintf("%.1f KB",  n as f64 / 1024.0); }
    if n < 1024*1024*1024 { return sprintf("%.1f MB", n as f64 / (1024.0 * 1024.0)); }
    return sprintf("%.2f GB", n as f64 / (1024.0 * 1024.0 * 1024.0));
}
