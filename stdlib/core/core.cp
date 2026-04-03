/*
 * stdlib/core/core.cp
 * ====================
 * C-Prime Core Standard Library
 * Provides fundamental types and operations available in every C-Prime program.
 * This module is implicitly imported — you never need to write "import core".
 *
 * Includes:
 *   - Option<T>
 *   - Result<T, E>
 *   - panic() / assert()
 *   - Core traits (Eq, Ord, Clone, Drop)
 *   - Type conversions
 */

/* ─── Option<T> ───────────────────────────────────────────────────────────── */

/// Option<T> represents a value that may or may not exist.
/// Use it instead of null pointers.
///
/// Example:
///   Option<i32> val = find_item(key);
///   match val {
///       Some(x) -> io.println(x),
///       None    -> io.println("not found"),
///   }
enum Option<T> {
    Some(T),
    None,
}

fn Option.is_some<T>(`Option<T> self) -> bool {
    match *self {
        Some(_) -> return true,
        None    -> return false,
    }
}

fn Option.is_none<T>(`Option<T> self) -> bool {
    return !self.is_some();
}

fn Option.unwrap<T>(Option<T> self) -> T {
    match self {
        Some(v) -> return v,
        None    -> panic("called Option.unwrap() on None"),
    }
}

fn Option.unwrap_or<T>(Option<T> self, T default_val) -> T {
    match self {
        Some(v) -> return v,
        None    -> return default_val,
    }
}

fn Option.map<T, U>(Option<T> self, fn(T) -> U f) -> Option<U> {
    match self {
        Some(v) -> return Some(f(v)),
        None    -> return None,
    }
}

fn Option.and_then<T, U>(Option<T> self, fn(T) -> Option<U> f) -> Option<U> {
    match self {
        Some(v) -> return f(v),
        None    -> return None,
    }
}

/* ─── Result<T, E> ────────────────────────────────────────────────────────── */

/// Result<T, E> represents either success (Ok) or failure (Err).
/// Use it for operations that can fail instead of error codes.
///
/// Example:
///   Result<i32, str> result = parse_number("42");
///   match result {
///       Ok(n)  -> io.println(n),
///       Err(e) -> io.println(e),
///   }
enum Result<T, E> {
    Ok(T),
    Err(E),
}

fn Result.is_ok<T, E>(`Result<T, E> self) -> bool {
    match *self {
        Ok(_)  -> return true,
        Err(_) -> return false,
    }
}

fn Result.is_err<T, E>(`Result<T, E> self) -> bool {
    return !self.is_ok();
}

fn Result.unwrap<T, E>(Result<T, E> self) -> T {
    match self {
        Ok(v)  -> return v,
        Err(e) -> panic("called Result.unwrap() on Err"),
    }
}

fn Result.unwrap_err<T, E>(Result<T, E> self) -> E {
    match self {
        Ok(_)  -> panic("called Result.unwrap_err() on Ok"),
        Err(e) -> return e,
    }
}

fn Result.unwrap_or<T, E>(Result<T, E> self, T default_val) -> T {
    match self {
        Ok(v)  -> return v,
        Err(_) -> return default_val,
    }
}

fn Result.map<T, U, E>(Result<T, E> self, fn(T) -> U f) -> Result<U, E> {
    match self {
        Ok(v)  -> return Ok(f(v)),
        Err(e) -> return Err(e),
    }
}

fn Result.map_err<T, E, F>(Result<T, E> self, fn(E) -> F f) -> Result<T, F> {
    match self {
        Ok(v)  -> return Ok(v),
        Err(e) -> return Err(f(e)),
    }
}

/* ─── Panic & Assert ──────────────────────────────────────────────────────── */

/// Terminate the program with an error message.
/// Prints file, line, and message to stderr.
fn panic(`str message) -> void {
    /* Implemented as a compiler intrinsic — outputs to stderr then calls exit(1) */
    __builtin_panic(message);
}

/// Assert a condition. Panics if false.
fn assert(bool condition, `str message) -> void {
    if !condition {
        panic(message);
    }
}

/// Assert in debug builds only. Compiled out in release builds.
fn debug_assert(bool condition, `str message) -> void {
    if __builtin_is_debug() {
        assert(condition, message);
    }
}

/* ─── Numeric Conversions ─────────────────────────────────────────────────── */

fn i32_to_i64(i32 x) -> i64 { return x as i64; }
fn i64_to_i32(i64 x) -> Result<i32, str> {
    if x > 2147483647 || x < -2147483648 {
        return Err("i64 value out of i32 range");
    }
    return Ok(x as i32);
}
fn i32_to_f64(i32 x) -> f64 { return x as f64; }
fn f64_to_i32(f64 x) -> i32 { return x as i32; }
fn u8_to_char(u8 x)  -> char { return x as char; }
fn char_to_u8(char x) -> u8  { return x as u8; }

/* ─── Min / Max / Clamp ───────────────────────────────────────────────────── */

fn min_i32(i32 a, i32 b) -> i32 { if a < b { return a; } return b; }
fn max_i32(i32 a, i32 b) -> i32 { if a > b { return a; } return b; }
fn clamp_i32(i32 val, i32 lo, i32 hi) -> i32 {
    return max_i32(lo, min_i32(val, hi));
}

fn min_f64(f64 a, f64 b) -> f64 { if a < b { return a; } return b; }
fn max_f64(f64 a, f64 b) -> f64 { if a > b { return a; } return b; }
fn clamp_f64(f64 val, f64 lo, f64 hi) -> f64 {
    return max_f64(lo, min_f64(val, hi));
}

/* ─── Swap ────────────────────────────────────────────────────────────────── */

/// Swap two values. Uses the borrow operator.
fn swap<T>(val `mut T a, val `mut T b) -> void {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

/* ─── Sizeof ──────────────────────────────────────────────────────────────── */

/// Returns the size of a type in bytes (compile-time constant).
fn size_of<T>() -> usize {
    return __builtin_sizeof(T);
}

fn align_of<T>() -> usize {
    return __builtin_alignof(T);
}
