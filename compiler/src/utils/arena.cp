/*
 * cpc — Arena Allocator
 * compiler/src/utils/arena.cp
 * ============================
 * A bump allocator used throughout the compiler for AST nodes,
 * type information, symbol table entries, and IR nodes.
 *
 * Why: Compiler data structures are allocated during a single compilation,
 *      then freed all at once. Using an arena is 5–10× faster than
 *      individual malloc/free calls and produces zero fragmentation.
 *
 * Usage:
 *   CompileArena arena = CompileArena.new(8 * 1024 * 1024);  // 8 MB
 *   ASTNode* node = arena.alloc<ASTNode>();
 *   // ... use nodes ...
 *   arena.destroy();  // frees all nodes at once
 */

import core;
import mem;

const usize ARENA_ALIGN     = 16;     /* all allocations aligned to 16 bytes */
const usize ARENA_CHUNK_SZ  = 1024 * 1024;  /* 1 MB per chunk */

/* ─── Arena chunk ─────────────────────────────────────────────────────────── */
struct ArenaChunk {
    u8*         data;
    usize       used;
    usize       cap;
    ArenaChunk* next;
}

fn chunk_new(usize cap) -> ArenaChunk* {
    ArenaChunk* c = mem.alloc_type<ArenaChunk>();
    c.data = mem.alloc(cap);
    c.used = 0;
    c.cap  = cap;
    c.next = null;
    return c;
}

fn chunk_alloc(`mut ArenaChunk* c, usize size) -> Option<u8*> {
    /* Align up */
    usize aligned = (size + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1);
    if c.used + aligned > c.cap { return None; }
    u8* ptr = c.data + c.used;
    mem.zero(ptr, aligned);   /* zero-init all arena allocations */
    c.used = c.used + aligned;
    return Some(ptr);
}

/* ─── Compile Arena ───────────────────────────────────────────────────────── */
struct CompileArena {
    ArenaChunk* head;        /* current chunk to allocate from */
    ArenaChunk* first;       /* first chunk (for freeing) */
    usize       total_used;  /* bytes allocated in total */
    usize       total_cap;   /* total capacity across all chunks */
    u64         alloc_count; /* number of allocations */
}

fn CompileArena.new(usize initial_cap) -> CompileArena {
    usize cap = if initial_cap < ARENA_CHUNK_SZ { ARENA_CHUNK_SZ } else { initial_cap };
    ArenaChunk* first = chunk_new(cap);
    return CompileArena {
        head:        first,
        first:       first,
        total_used:  0,
        total_cap:   cap,
        alloc_count: 0,
    };
}

fn CompileArena.alloc(`mut CompileArena self, usize size) -> u8* {
    self.alloc_count = self.alloc_count + 1;

    /* Try current chunk */
    match chunk_alloc(`mut self.head, size) {
        Some(ptr) -> {
            self.total_used = self.total_used + size;
            return ptr;
        },
        None -> {
            /* Grow: allocate a new chunk large enough */
            usize new_cap = if size > ARENA_CHUNK_SZ { size * 2 } else { ARENA_CHUNK_SZ };
            ArenaChunk* new_chunk = chunk_new(new_cap);
            new_chunk.next = self.head;
            self.head      = new_chunk;
            self.total_cap = self.total_cap + new_cap;
            /* Retry (guaranteed to succeed now) */
            match chunk_alloc(`mut self.head, size) {
                Some(ptr) -> {
                    self.total_used = self.total_used + size;
                    return ptr;
                },
                None -> {
                    /* Should never happen */
                    panic("CompileArena: allocation failed even after growing");
                },
            }
        },
    }
}

fn CompileArena.alloc_zeroed(`mut CompileArena self, usize size) -> u8* {
    u8* ptr = self.alloc(size);
    mem.zero(ptr, size);
    return ptr;
}

fn CompileArena.str_copy(`mut CompileArena self, `str s) -> str {
    usize len = string.len(s);
    u8* buf = self.alloc(len + 1);
    mem.copy(buf, s as u8*, len);
    buf[len] = 0;
    return string.from_raw(buf, len);
}

fn CompileArena.reset(`mut CompileArena self) -> void {
    /* Reset all chunks but keep the memory */
    ArenaChunk* c = self.first;
    while c != null {
        c.used = 0;
        c = c.next;
    }
    self.head       = self.first;
    self.total_used = 0;
    self.alloc_count = 0;
}

fn CompileArena.destroy(`mut CompileArena self) -> void {
    ArenaChunk* c = self.first;
    while c != null {
        ArenaChunk* next = c.next;
        mem.free(c.data);
        mem.free(c as u8*);
        c = next;
    }
    self.head        = null;
    self.first       = null;
    self.total_used  = 0;
    self.total_cap   = 0;
    self.alloc_count = 0;
}

fn CompileArena.stats(`CompileArena self) -> void {
    io.printf("[arena] used=%zu cap=%zu allocs=%llu\n",
              self.total_used, self.total_cap, self.alloc_count);
}
