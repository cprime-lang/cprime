/*
 * stdlib/mem/mem.cp
 * ==================
 * C-Prime Memory Management Library
 *
 * Provides safe memory allocation wrappers.
 * In safe C-Prime code, prefer using owned values and borrows.
 * These functions are mostly used internally by stdlib data structures.
 * Direct use should be inside `unsafe { }` blocks.
 *
 * Usage:
 *   import mem;
 *
 *   unsafe {
 *       u8* buf = mem.alloc(1024);
 *       mem.copy(buf, src, 1024);
 *       mem.free(buf);
 *   }
 */

import core;

/* ─── Allocation ──────────────────────────────────────────────────────────── */

/// Allocate n bytes. Returns null on failure.
/// Always zero-initializes the memory.
fn alloc(usize n) -> u8* {
    return __builtin_calloc(1, n) as u8*;
}

/// Allocate n bytes without initialization (faster, but uninitialized).
fn alloc_uninit(usize n) -> u8* {
    return __builtin_malloc(n) as u8*;
}

/// Allocate memory for a specific type.
fn alloc_type<T>() -> T* {
    return alloc(size_of<T>()) as T*;
}

/// Allocate an array of n elements of type T.
fn alloc_array<T>(usize n) -> T* {
    return alloc(size_of<T>() * n) as T*;
}

/// Resize an allocation. May move the memory.
fn realloc(u8* ptr, usize new_size) -> u8* {
    return __builtin_realloc(ptr as void*, new_size) as u8*;
}

/// Free allocated memory. Sets ptr to null after freeing.
fn free(u8* ptr) -> void {
    if ptr != null {
        __builtin_free(ptr as void*);
    }
}

/* ─── Memory Operations ───────────────────────────────────────────────────── */

/// Copy n bytes from src to dst. Regions must not overlap.
fn copy(u8* dst, `u8* src, usize n) -> void {
    __builtin_memcpy(dst as void*, src as void*, n);
}

/// Copy n bytes, safe for overlapping regions.
fn move_bytes(u8* dst, `u8* src, usize n) -> void {
    __builtin_memmove(dst as void*, src as void*, n);
}

/// Fill n bytes with value.
fn set(u8* dst, u8 value, usize n) -> void {
    __builtin_memset(dst as void*, value as i32, n);
}

/// Zero out n bytes.
fn zero(u8* dst, usize n) -> void {
    set(dst, 0, n);
}

/// Compare two memory regions. Returns 0 if equal.
fn compare(`u8* a, `u8* b, usize n) -> i32 {
    return __builtin_memcmp(a as void*, b as void*, n);
}

/// Check if two memory regions are equal.
fn equal(`u8* a, `u8* b, usize n) -> bool {
    return compare(a, b, n) == 0;
}

/* ─── Arena Allocator ─────────────────────────────────────────────────────── */

/// Arena — a bump allocator for fast, grouped allocations.
/// All memory is freed at once when the arena is destroyed.
/// Great for per-request or per-frame allocations.
///
/// Example:
///   Arena arena = Arena.new(1024 * 1024);   /* 1MB arena */
///   u8* buf = arena.alloc(256);
///   /* ... use buf ... */
///   arena.destroy();                         /* frees everything at once */
struct Arena {
    u8*   base;     /* Start of arena memory */
    usize cap;      /* Total capacity in bytes */
    usize used;     /* Bytes currently used */
}

fn Arena.new(usize capacity) -> Result<Arena, str> {
    u8* base = alloc(capacity);
    if base == null {
        return Err("arena allocation failed — out of memory");
    }
    return Ok(Arena { base: base, cap: capacity, used: 0 });
}

fn Arena.alloc(`mut Arena self, usize size) -> Result<u8*, str> {
    /* Align to 8 bytes */
    usize aligned = (size + 7) & ~7;
    if self.used + aligned > self.cap {
        return Err("arena out of space");
    }
    u8* ptr = self.base + self.used;
    self.used = self.used + aligned;
    return Ok(ptr);
}

fn Arena.reset(`mut Arena self) -> void {
    self.used = 0;
    zero(self.base, self.cap);
}

fn Arena.destroy(`mut Arena self) -> void {
    if self.base != null {
        free(self.base);
        self.base = null;
        self.cap  = 0;
        self.used = 0;
    }
}

fn Arena.remaining(`Arena self) -> usize {
    return self.cap - self.used;
}

/* ─── Stack Allocator ─────────────────────────────────────────────────────── */

/// Allocate on the current stack frame.
/// Automatically freed when the function returns.
/// DO NOT return a pointer to stack-allocated memory — use heap alloc for that.
fn stack_alloc(usize n) -> u8* {
    return __builtin_alloca(n) as u8*;
}
