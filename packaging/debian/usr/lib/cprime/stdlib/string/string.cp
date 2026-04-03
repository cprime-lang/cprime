/*
 * stdlib/string/string.cp
 * ========================
 * C-Prime String Library
 *
 * C-Prime strings are fat pointers (ptr + len). They are NOT null-terminated.
 * All functions are borrow-safe — they take `str borrows and return new str values.
 *
 * Usage:
 *   import string;
 *   bool eq = string.eq("hello", "hello");
 *   str s = string.concat("foo", "bar");
 */

import core;
import mem;

/* ─── Basic queries ───────────────────────────────────────────────────────── */

/// Return byte length of string.
fn len(`str s) -> usize { return __builtin_strlen(s); }

/// Return true if string has zero length.
fn is_empty(`str s) -> bool { return __builtin_strlen(s) == 0; }

/// Get byte at position i. Panics if out of bounds.
fn char_at(`str s, usize i) -> char {
    assert(i < len(s), "string.char_at: index out of bounds");
    return (s as u8*)[i] as char;
}

/// Get byte at position i, or '\0' if out of bounds.
fn get(`str s, usize i) -> char {
    if i >= len(s) { return '\0'; }
    return (s as u8*)[i] as char;
}

/* ─── Comparison ──────────────────────────────────────────────────────────── */

fn eq(`str a, `str b) -> bool {
    usize la = len(a);
    usize lb = len(b);
    if la != lb { return false; }
    return mem.equal(a as u8*, b as u8*, la);
}

fn lt(`str a, `str b) -> bool {
    return __builtin_strcmp(a, b) < 0;
}

fn compare(`str a, `str b) -> i32 {
    return __builtin_strcmp(a, b);
}

/* ─── Search ──────────────────────────────────────────────────────────────── */

fn contains(`str haystack, `str needle) -> bool {
    return find(haystack, needle) >= 0;
}

fn find(`str haystack, `str needle) -> i64 {
    usize hl = len(haystack);
    usize nl = len(needle);
    if nl == 0 { return 0; }
    if nl > hl { return -1; }
    usize i = 0;
    while i <= hl - nl {
        if mem.equal((haystack as u8*) + i, needle as u8*, nl) {
            return i as i64;
        }
        i = i + 1;
    }
    return -1;
}

fn starts_with(`str s, `str prefix) -> bool {
    usize pl = len(prefix);
    if pl > len(s) { return false; }
    return mem.equal(s as u8*, prefix as u8*, pl);
}

fn ends_with(`str s, `str suffix) -> bool {
    usize sl = len(s);
    usize ul = len(suffix);
    if ul > sl { return false; }
    return mem.equal((s as u8*) + (sl - ul), suffix as u8*, ul);
}

fn find_char(`str s, char c) -> i32 {
    usize i = 0;
    usize sl = len(s);
    while i < sl {
        if char_at(s, i) == c { return i as i32; }
        i = i + 1;
    }
    return -1;
}

fn rfind_char(`str s, char c) -> i32 {
    i64 i = len(s) as i64 - 1;
    while i >= 0 {
        if char_at(s, i as usize) == c { return i as i32; }
        i = i - 1;
    }
    return -1;
}

/* ─── Slicing ─────────────────────────────────────────────────────────────── */

fn slice(`str s, usize start, usize end_) -> str {
    usize sl = len(s);
    if start > sl { start = sl; }
    if end_  > sl { end_  = sl; }
    if start > end_ { start = end_; }
    return from_raw((s as u8*) + start, end_ - start);
}

fn from_raw(u8* ptr, usize length) -> str {
    return __builtin_make_str(ptr, length);
}

fn from_char(char c) -> str {
    u8* buf = mem.alloc(2);
    buf[0] = c as u8;
    buf[1] = 0;
    return from_raw(buf, 1);
}

/* ─── Building ────────────────────────────────────────────────────────────── */

fn concat(`str a, `str b) -> str {
    usize la = len(a);
    usize lb = len(b);
    u8*   buf = mem.alloc(la + lb + 1);
    mem.copy(buf,      a as u8*, la);
    mem.copy(buf + la, b as u8*, lb);
    buf[la + lb] = 0;
    return from_raw(buf, la + lb);
}

fn push_char(`str s, char c) -> str {
    usize sl = len(s);
    u8*   buf = mem.alloc(sl + 2);
    mem.copy(buf, s as u8*, sl);
    buf[sl]     = c as u8;
    buf[sl + 1] = 0;
    return from_raw(buf, sl + 1);
}

fn repeat(`str s, usize n) -> str {
    usize sl = len(s);
    if n == 0 || sl == 0 { return ""; }
    u8* buf = mem.alloc(sl * n + 1);
    usize i = 0;
    while i < n {
        mem.copy(buf + i * sl, s as u8*, sl);
        i = i + 1;
    }
    buf[sl * n] = 0;
    return from_raw(buf, sl * n);
}

fn replace(`str s, `str old, `str new) -> str {
    str result = "";
    usize i = 0;
    usize sl = len(s);
    usize ol = len(old);
    while i < sl {
        if i + ol <= sl && mem.equal((s as u8*) + i, old as u8*, ol) {
            result = concat(result, new);
            i = i + ol;
        } else {
            result = push_char(result, char_at(s, i));
            i = i + 1;
        }
    }
    return result;
}

/* ─── Case conversion ─────────────────────────────────────────────────────── */

fn to_upper(`str s) -> str {
    usize sl = len(s);
    u8*   buf = mem.alloc(sl + 1);
    usize i = 0;
    while i < sl {
        u8 c = (s as u8*)[i];
        if c >= 'a' as u8 && c <= 'z' as u8 { c = c - 32; }
        buf[i] = c;
        i = i + 1;
    }
    buf[sl] = 0;
    return from_raw(buf, sl);
}

fn to_lower(`str s) -> str {
    usize sl = len(s);
    u8*   buf = mem.alloc(sl + 1);
    usize i = 0;
    while i < sl {
        u8 c = (s as u8*)[i];
        if c >= 'A' as u8 && c <= 'Z' as u8 { c = c + 32; }
        buf[i] = c;
        i = i + 1;
    }
    buf[sl] = 0;
    return from_raw(buf, sl);
}

/* ─── Trimming ────────────────────────────────────────────────────────────── */

fn trim_start(`str s) -> str {
    usize i = 0;
    usize sl = len(s);
    while i < sl && (char_at(s,i) == ' ' || char_at(s,i) == '\t' ||
                     char_at(s,i) == '\n' || char_at(s,i) == '\r') {
        i = i + 1;
    }
    return slice(s, i, sl);
}

fn trim_end(`str s) -> str {
    usize sl = len(s);
    i64 i = sl as i64 - 1;
    while i >= 0 && (char_at(s, i as usize) == ' ' || char_at(s, i as usize) == '\t' ||
                     char_at(s, i as usize) == '\n' || char_at(s, i as usize) == '\r') {
        i = i - 1;
    }
    return slice(s, 0, (i + 1) as usize);
}

fn trim(`str s) -> str { return trim_start(trim_end(s)); }

/* ─── Splitting ───────────────────────────────────────────────────────────── */

fn split(`str s, char delim) -> str[] {
    /* Count parts first */
    u32 count = 1;
    usize i = 0;
    usize sl = len(s);
    while i < sl {
        if char_at(s, i) == delim { count = count + 1; }
        i = i + 1;
    }

    str* parts = mem.alloc_array<str>(count as usize);
    u32 pi  = 0;
    usize start = 0;
    i = 0;
    while i < sl {
        if char_at(s, i) == delim {
            parts[pi] = slice(s, start, i);
            pi    = pi + 1;
            start = i + 1;
        }
        i = i + 1;
    }
    parts[pi] = slice(s, start, sl);

    str[] result;
    result.ptr = parts;
    result.len = count as usize;
    return result;
}

/* ─── Parsing ─────────────────────────────────────────────────────────────── */

fn parse_i64(`str s) -> i64 {
    return __builtin_atoll(s);
}

fn parse_f64(`str s) -> f64 {
    return __builtin_atof(s);
}

fn parse_bool(`str s) -> Result<bool, str> {
    if eq(s, "true") || eq(s, "1")  { return Ok(true);  }
    if eq(s, "false")|| eq(s, "0")  { return Ok(false); }
    return Err(fmt.sprintf("cannot parse '%s' as bool", s));
}
