/*
 * stdlib/collections/vec.cp
 * ==========================
 * C-Prime Dynamic Array (Vec<T>)
 *
 * A heap-allocated growable array. Owns its elements.
 * Elements are moved into the Vec when pushed.
 *
 * Usage:
 *   import collections.vec;
 *
 *   Vec<i32> v = Vec.new();
 *   v.push(1);
 *   v.push(2);
 *   v.push(3);
 *   io.println(v.get(0));   /* 1 */
 *   io.println(v.len());    /* 3 */
 *   v.destroy();
 */

import core;
import mem;

const usize VEC_INITIAL_CAPACITY = 8;
const f64   VEC_GROWTH_FACTOR    = 2.0;

struct Vec<T> {
    T*    data;
    usize len;
    usize cap;
}

/* ─── Construction ────────────────────────────────────────────────────────── */

fn Vec.new<T>() -> Vec<T> {
    return Vec<T> {
        data: null,
        len:  0,
        cap:  0,
    };
}

fn Vec.with_capacity<T>(usize cap) -> Result<Vec<T>, str> {
    T* data = mem.alloc_array<T>(cap);
    if data == null { return Err("Vec allocation failed"); }
    return Ok(Vec<T> { data: data, len: 0, cap: cap });
}

fn Vec.from_slice<T>(`T[] slice) -> Result<Vec<T>, str> {
    Vec<T> v = match Vec.with_capacity<T>(slice.len) {
        Ok(v)  -> v,
        Err(e) -> return Err(e),
    };
    mem.copy(v.data as u8*, slice.ptr as u8*, slice.len * size_of<T>());
    v.len = slice.len;
    return Ok(v);
}

/* ─── Growth ──────────────────────────────────────────────────────────────── */

fn Vec.grow<T>(`mut Vec<T> self) -> Result<void, str> {
    usize new_cap = if self.cap == 0 {
        VEC_INITIAL_CAPACITY
    } else {
        (self.cap as f64 * VEC_GROWTH_FACTOR) as usize
    };

    T* new_data = mem.realloc(self.data as u8*, new_cap * size_of<T>()) as T*;
    if new_data == null { return Err("Vec realloc failed — out of memory"); }

    self.data = new_data;
    self.cap  = new_cap;
    return Ok(void);
}

fn Vec.reserve<T>(`mut Vec<T> self, usize additional) -> Result<void, str> {
    usize needed = self.len + additional;
    if needed <= self.cap { return Ok(void); }

    T* new_data = mem.realloc(self.data as u8*, needed * size_of<T>()) as T*;
    if new_data == null { return Err("Vec reserve failed — out of memory"); }

    self.data = new_data;
    self.cap  = needed;
    return Ok(void);
}

/* ─── Mutation ────────────────────────────────────────────────────────────── */

fn Vec.push<T>(`mut Vec<T> self, T item) -> Result<void, str> {
    if self.len >= self.cap {
        match self.grow() {
            Err(e) -> return Err(e),
            Ok(_)  -> {},
        }
    }
    self.data[self.len] = item;
    self.len = self.len + 1;
    return Ok(void);
}

fn Vec.pop<T>(`mut Vec<T> self) -> Option<T> {
    if self.len == 0 { return None; }
    self.len = self.len - 1;
    return Some(self.data[self.len]);
}

fn Vec.insert<T>(`mut Vec<T> self, usize index, T item) -> Result<void, str> {
    if index > self.len { return Err("Vec.insert: index out of bounds"); }
    if self.len >= self.cap {
        match self.grow() {
            Err(e) -> return Err(e),
            Ok(_)  -> {},
        }
    }
    /* Shift elements right */
    usize i = self.len;
    while i > index {
        self.data[i] = self.data[i - 1];
        i = i - 1;
    }
    self.data[index] = item;
    self.len = self.len + 1;
    return Ok(void);
}

fn Vec.remove<T>(`mut Vec<T> self, usize index) -> Result<T, str> {
    if index >= self.len { return Err("Vec.remove: index out of bounds"); }
    T item = self.data[index];
    usize i = index;
    while i < self.len - 1 {
        self.data[i] = self.data[i + 1];
        i = i + 1;
    }
    self.len = self.len - 1;
    return Ok(item);
}

fn Vec.clear<T>(`mut Vec<T> self) -> void {
    self.len = 0;
}

fn Vec.append_all<T>(`mut Vec<T> self, `Vec<T> other) -> Result<void, str> {
    match self.reserve(other.len) {
        Err(e) -> return Err(e),
        Ok(_)  -> {},
    }
    mem.copy(
        (self.data as u8*) + self.len * size_of<T>(),
        other.data as u8*,
        other.len * size_of<T>()
    );
    self.len = self.len + other.len;
    return Ok(void);
}

/* ─── Access ──────────────────────────────────────────────────────────────── */

fn Vec.get<T>(`Vec<T> self, usize index) -> Result<`T, str> {
    if index >= self.len {
        return Err("Vec.get: index out of bounds");
    }
    return Ok(`self.data[index]);
}

fn Vec.get_mut<T>(`mut Vec<T> self, usize index) -> Result<`mut T, str> {
    if index >= self.len {
        return Err("Vec.get_mut: index out of bounds");
    }
    return Ok(`mut self.data[index]);
}

fn Vec.first<T>(`Vec<T> self) -> Option<`T> {
    if self.len == 0 { return None; }
    return Some(`self.data[0]);
}

fn Vec.last<T>(`Vec<T> self) -> Option<`T> {
    if self.len == 0 { return None; }
    return Some(`self.data[self.len - 1]);
}

fn Vec.as_slice<T>(`Vec<T> self) -> T[] {
    return T[].from_raw(self.data, self.len);
}

/* ─── Queries ─────────────────────────────────────────────────────────────── */

fn Vec.len<T>(`Vec<T> self) -> usize  { return self.len; }
fn Vec.cap<T>(`Vec<T> self) -> usize  { return self.cap; }
fn Vec.is_empty<T>(`Vec<T> self) -> bool { return self.len == 0; }

fn Vec.contains<T>(`Vec<T> self, `T item) -> bool {
    usize i = 0;
    while i < self.len {
        if mem.equal(self.data[i] as u8*, item as u8*, size_of<T>()) {
            return true;
        }
        i = i + 1;
    }
    return false;
}

/* ─── Sorting ─────────────────────────────────────────────────────────────── */

fn Vec.sort<T>(`mut Vec<T> self, fn(`T, `T) -> i32 compare) -> void {
    /* Quicksort */
    _vec_quicksort(self.data, 0, self.len as i64 - 1, compare);
}

fn _vec_quicksort<T>(T* arr, i64 lo, i64 hi, fn(`T, `T) -> i32 cmp) -> void {
    if lo < hi {
        i64 p = _vec_partition(arr, lo, hi, cmp);
        _vec_quicksort(arr, lo, p - 1, cmp);
        _vec_quicksort(arr, p + 1, hi, cmp);
    }
}

fn _vec_partition<T>(T* arr, i64 lo, i64 hi, fn(`T, `T) -> i32 cmp) -> i64 {
    T pivot = arr[hi];
    i64 i = lo - 1;
    i64 j = lo;
    while j < hi {
        if cmp(`arr[j], `pivot) <= 0 {
            i = i + 1;
            T tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
        }
        j = j + 1;
    }
    T tmp = arr[i + 1]; arr[i + 1] = arr[hi]; arr[hi] = tmp;
    return i + 1;
}

/* ─── Destruction ─────────────────────────────────────────────────────────── */

fn Vec.destroy<T>(`mut Vec<T> self) -> void {
    if self.data != null {
        mem.free(self.data as u8*);
        self.data = null;
        self.len  = 0;
        self.cap  = 0;
    }
}
