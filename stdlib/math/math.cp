/*
 * stdlib/math/math.cp
 * ====================
 * C-Prime Math Library — wraps libm functions with safe C-Prime types.
 *
 * Usage:
 *   import math;
 *   f64 d = math.sqrt(2.0);
 *   f64 r = math.sin(math.PI / 2.0);
 */

import core;

const f64 PI      = 3.14159265358979323846;
const f64 TAU     = 6.28318530717958647693;
const f64 E       = 2.71828182845904523536;
const f64 SQRT2   = 1.41421356237309504880;
const f64 LN2     = 0.69314718055994530942;
const f64 LN10    = 2.30258509299404568402;
const f64 LOG2E   = 1.44269504088896340736;
const f64 LOG10E  = 0.43429448190325182765;
const f64 INF     = __builtin_inf();
const f64 NAN     = __builtin_nan();

/* ─── Square root & powers ────────────────────────────────────────────────── */
fn sqrt(f64 x) -> f64  { return __builtin_sqrt(x);     }
fn cbrt(f64 x) -> f64  { return __builtin_cbrt(x);     }
fn pow(f64 x, f64 y) -> f64 { return __builtin_pow(x, y); }
fn exp(f64 x) -> f64   { return __builtin_exp(x);      }
fn exp2(f64 x) -> f64  { return __builtin_exp2(x);     }
fn log(f64 x) -> f64   { return __builtin_log(x);      }
fn log2(f64 x) -> f64  { return __builtin_log2(x);     }
fn log10(f64 x) -> f64 { return __builtin_log10(x);    }
fn hypot(f64 a, f64 b) -> f64 { return __builtin_hypot(a, b); }

/* ─── Trigonometry ────────────────────────────────────────────────────────── */
fn sin(f64 x) -> f64   { return __builtin_sin(x);  }
fn cos(f64 x) -> f64   { return __builtin_cos(x);  }
fn tan(f64 x) -> f64   { return __builtin_tan(x);  }
fn asin(f64 x) -> f64  { return __builtin_asin(x); }
fn acos(f64 x) -> f64  { return __builtin_acos(x); }
fn atan(f64 x) -> f64  { return __builtin_atan(x); }
fn atan2(f64 y, f64 x) -> f64 { return __builtin_atan2(y, x); }
fn sinh(f64 x) -> f64  { return __builtin_sinh(x); }
fn cosh(f64 x) -> f64  { return __builtin_cosh(x); }
fn tanh(f64 x) -> f64  { return __builtin_tanh(x); }

/// Convert degrees to radians.
fn radians(f64 deg) -> f64 { return deg * PI / 180.0; }
/// Convert radians to degrees.
fn degrees(f64 rad) -> f64 { return rad * 180.0 / PI; }

/* ─── Rounding ────────────────────────────────────────────────────────────── */
fn floor(f64 x) -> f64 { return __builtin_floor(x); }
fn ceil(f64 x)  -> f64 { return __builtin_ceil(x);  }
fn round(f64 x) -> f64 { return __builtin_round(x); }
fn trunc(f64 x) -> f64 { return __builtin_trunc(x); }
fn fract(f64 x) -> f64 { return x - floor(x);        }

/* ─── Min / Max / Clamp ───────────────────────────────────────────────────── */
fn min_f64(f64 a, f64 b) -> f64 { return if a < b { a } else { b }; }
fn max_f64(f64 a, f64 b) -> f64 { return if a > b { a } else { b }; }
fn clamp_f64(f64 v, f64 lo, f64 hi) -> f64 { return max_f64(lo, min_f64(v, hi)); }
fn min_i32(i32 a, i32 b) -> i32 { return if a < b { a } else { b }; }
fn max_i32(i32 a, i32 b) -> i32 { return if a > b { a } else { b }; }
fn clamp_i32(i32 v, i32 lo, i32 hi) -> i32 { return max_i32(lo, min_i32(v, hi)); }
fn min_i64(i64 a, i64 b) -> i64 { return if a < b { a } else { b }; }
fn max_i64(i64 a, i64 b) -> i64 { return if a > b { a } else { b }; }

/* ─── Absolute value ──────────────────────────────────────────────────────── */
fn abs_f64(f64 x) -> f64 { return if x < 0.0 { -x } else { x }; }
fn abs_i32(i32 x) -> i32 { return if x < 0   { -x } else { x }; }
fn abs_i64(i64 x) -> i64 { return if x < 0   { -x } else { x }; }

/* ─── Integer math ────────────────────────────────────────────────────────── */
fn gcd(i64 a, i64 b) -> i64 {
    while b != 0 { i64 t = b; b = a % b; a = t; }
    return abs_i64(a);
}

fn lcm(i64 a, i64 b) -> i64 {
    if a == 0 || b == 0 { return 0; }
    return abs_i64(a / gcd(a, b) * b);
}

fn is_prime(i64 n) -> bool {
    if n < 2  { return false; }
    if n == 2 { return true;  }
    if n % 2 == 0 { return false; }
    i64 i = 3;
    while i * i <= n {
        if n % i == 0 { return false; }
        i = i + 2;
    }
    return true;
}

/* ─── Float predicates ────────────────────────────────────────────────────── */
fn is_nan(f64 x) -> bool  { return x != x; }
fn is_inf(f64 x) -> bool  { return x == INF || x == -INF; }
fn is_finite(f64 x) -> bool { return !is_nan(x) && !is_inf(x); }

/* ─── Linear interpolation ────────────────────────────────────────────────── */
fn lerp(f64 a, f64 b, f64 t) -> f64 { return a + (b - a) * t; }
fn smoothstep(f64 edge0, f64 edge1, f64 x) -> f64 {
    f64 t = clamp_f64((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
