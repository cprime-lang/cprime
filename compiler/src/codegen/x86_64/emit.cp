/*
 * cpc — x86_64 Instruction Emitter
 * compiler/src/codegen/x86_64/emit.cp
 * ======================================
 * Emits raw x86_64 machine code bytes into a CodeBuf.
 * This is the C-Prime equivalent of bootstrap/src/codegen/x86_64_emit.c.
 *
 * Reference: Intel SDM Vol 2, System V AMD64 ABI
 *
 * REX prefix: 0x40 | W(3) | R(2) | X(1) | B(0)
 *   W=1  → 64-bit operand
 *   R    → extends ModRM.reg
 *   B    → extends ModRM.rm
 *
 * TextPatch system:
 *   External calls and RIP-relative string loads can't be resolved
 *   until the assembler knows all virtual addresses.
 *   We emit NOP placeholder bytes + record a TextPatch (assembly mnemonic).
 *   write_asm_and_link() substitutes them when writing the .s file.
 */

import core;
import mem;
import io;
import string;
import fmt;

/* ─── Register IDs ────────────────────────────────────────────────────────── */
const i32 RAX = 0;  const i32 RCX = 1;  const i32 RDX = 2;  const i32 RBX = 3;
const i32 RSP = 4;  const i32 RBP = 5;  const i32 RSI = 6;  const i32 RDI = 7;
const i32 R8  = 8;  const i32 R9  = 9;  const i32 R10 = 10; const i32 R11 = 11;
const i32 R12 = 12; const i32 R13 = 13; const i32 R14 = 14; const i32 R15 = 15;

const str[] REG_NAMES = [
    "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
    "r8", "r9", "r10","r11","r12","r13","r14","r15"
];

/* ─── Code buffer ─────────────────────────────────────────────────────────── */
struct CodeBuf {
    u8*   data;
    usize len;
    usize cap;
}

fn CodeBuf.new() -> CodeBuf {
    return CodeBuf { data: null, len: 0, cap: 0 };
}

fn CodeBuf.push(`mut CodeBuf self, u8 byte) -> void {
    if self.len >= self.cap {
        self.cap = if self.cap == 0 { 4096 } else { self.cap * 2 };
        self.data = mem.realloc(self.data, self.cap);
    }
    self.data[self.len] = byte;
    self.len = self.len + 1;
}

fn CodeBuf.push_u16(`mut CodeBuf self, u16 v) -> void {
    self.push((v & 0xFF) as u8);
    self.push(((v >> 8) & 0xFF) as u8);
}

fn CodeBuf.push_u32(`mut CodeBuf self, u32 v) -> void {
    self.push((v & 0xFF) as u8);
    self.push(((v >> 8)  & 0xFF) as u8);
    self.push(((v >> 16) & 0xFF) as u8);
    self.push(((v >> 24) & 0xFF) as u8);
}

fn CodeBuf.push_u64(`mut CodeBuf self, u64 v) -> void {
    self.push_u32((v & 0xFFFFFFFF) as u32);
    self.push_u32(((v >> 32) & 0xFFFFFFFF) as u32);
}

/* ─── Text patch ──────────────────────────────────────────────────────────── */
struct TextPatch {
    usize offset;      /* position of NOP placeholder in text buffer */
    usize nop_count;   /* number of NOP bytes emitted */
    str   asm_text;    /* replacement assembly mnemonic */
}

/* ─── String literal pool ─────────────────────────────────────────────────── */
struct StringEntry {
    str   text;
    usize rodata_offset;
}

/* ─── Label ───────────────────────────────────────────────────────────────── */
struct Label {
    str   name;
    usize code_offset;   /* resolved offset; usize_max = unresolved */
    bool  is_global;
}

const usize UNRESOLVED = 0xFFFFFFFFFFFFFFFF;

/* ─── Relocation ──────────────────────────────────────────────────────────── */
struct Reloc {
    usize patch_offset;    /* byte offset in text to patch */
    usize target_label;    /* label id */
    i32   addend;
}

/* ─── Emit context ────────────────────────────────────────────────────────── */
struct EmitCtx {
    CodeBuf     text;
    CodeBuf     rodata;

    Label*      labels;
    usize       label_count;
    usize       label_cap;

    Reloc*      relocs;
    usize       reloc_count;
    usize       reloc_cap;

    TextPatch*  patches;
    usize       patch_count;
    usize       patch_cap;

    StringEntry* strings;
    usize        string_count;
    usize        string_cap;

    str*        externals;
    usize       external_count;
    usize       external_cap;
}

fn EmitCtx.new() -> EmitCtx {
    EmitCtx e;
    e.text          = CodeBuf.new();
    e.rodata        = CodeBuf.new();
    e.labels        = null;
    e.label_count   = 0;
    e.label_cap     = 0;
    e.relocs        = null;
    e.reloc_count   = 0;
    e.reloc_cap     = 0;
    e.patches       = null;
    e.patch_count   = 0;
    e.patch_cap     = 0;
    e.strings       = null;
    e.string_count  = 0;
    e.string_cap    = 0;
    e.externals     = null;
    e.external_count = 0;
    e.external_cap  = 0;
    return e;
}

/* ─── REX helpers ─────────────────────────────────────────────────────────── */
fn rex_byte(bool w, bool r, bool x, bool b) -> u8 {
    return (0x40 | (if w { 8 } else { 0 }) |
                   (if r { 4 } else { 0 }) |
                   (if x { 2 } else { 0 }) |
                   (if b { 1 } else { 0 })) as u8;
}
fn needs_rex(i32 r) -> bool { return r >= 8; }
fn modrm(u8 mod_, i32 reg, i32 rm) -> u8 {
    return ((mod_ as i32 * 64) | ((reg & 7) * 8) | (rm & 7)) as u8;
}

/* ─── Raw byte emitters ───────────────────────────────────────────────────── */
fn emit_byte(`mut EmitCtx e, u8 b) -> void { e.text.push(b); }

/* ─── MOV instructions ────────────────────────────────────────────────────── */
fn emit_mov_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(dst)));
    emit_byte(`mut e, 0x89);
    emit_byte(`mut e, modrm(3, src, dst));
}

fn emit_mov_reg_imm64(`mut EmitCtx e, i32 dst, i64 imm) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(dst)));
    emit_byte(`mut e, (0xB8 | (dst & 7)) as u8);
    e.text.push_u64(imm as u64);
}

fn emit_mov_reg_mem(`mut EmitCtx e, i32 dst, i32 base, i32 off) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(dst), false, needs_rex(base)));
    emit_byte(`mut e, 0x8B);
    if off == 0 && (base & 7) != 5 {
        emit_byte(`mut e, modrm(0, dst, base));
        if (base & 7) == 4 { emit_byte(`mut e, 0x24); }
    } else if off >= -128 && off <= 127 {
        emit_byte(`mut e, modrm(1, dst, base));
        if (base & 7) == 4 { emit_byte(`mut e, 0x24); }
        emit_byte(`mut e, off as u8);
    } else {
        emit_byte(`mut e, modrm(2, dst, base));
        if (base & 7) == 4 { emit_byte(`mut e, 0x24); }
        e.text.push_u32(off as u32);
    }
}

fn emit_mov_mem_reg(`mut EmitCtx e, i32 base, i32 off, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(base)));
    emit_byte(`mut e, 0x89);
    if off == 0 && (base & 7) != 5 {
        emit_byte(`mut e, modrm(0, src, base));
        if (base & 7) == 4 { emit_byte(`mut e, 0x24); }
    } else if off >= -128 && off <= 127 {
        emit_byte(`mut e, modrm(1, src, base));
        if (base & 7) == 4 { emit_byte(`mut e, 0x24); }
        emit_byte(`mut e, off as u8);
    } else {
        emit_byte(`mut e, modrm(2, src, base));
        if (base & 7) == 4 { emit_byte(`mut e, 0x24); }
        e.text.push_u32(off as u32);
    }
}

/* ─── Arithmetic ──────────────────────────────────────────────────────────── */
fn emit_add_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(dst)));
    emit_byte(`mut e, 0x01); emit_byte(`mut e, modrm(3, src, dst));
}
fn emit_add_reg_imm32(`mut EmitCtx e, i32 dst, i32 imm) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(dst)));
    if imm >= -128 && imm <= 127 {
        emit_byte(`mut e, 0x83); emit_byte(`mut e, modrm(3, 0, dst));
        emit_byte(`mut e, imm as u8);
    } else {
        emit_byte(`mut e, 0x81); emit_byte(`mut e, modrm(3, 0, dst));
        e.text.push_u32(imm as u32);
    }
}
fn emit_sub_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(dst)));
    emit_byte(`mut e, 0x29); emit_byte(`mut e, modrm(3, src, dst));
}
fn emit_sub_reg_imm32(`mut EmitCtx e, i32 dst, i32 imm) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(dst)));
    if imm >= -128 && imm <= 127 {
        emit_byte(`mut e, 0x83); emit_byte(`mut e, modrm(3, 5, dst));
        emit_byte(`mut e, imm as u8);
    } else {
        emit_byte(`mut e, 0x81); emit_byte(`mut e, modrm(3, 5, dst));
        e.text.push_u32(imm as u32);
    }
}
fn emit_imul_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(dst), false, needs_rex(src)));
    emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0xAF);
    emit_byte(`mut e, modrm(3, dst, src));
}
fn emit_cqo(`mut EmitCtx e) -> void { emit_byte(`mut e, 0x48); emit_byte(`mut e, 0x99); }
fn emit_idiv_reg(`mut EmitCtx e, i32 r) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(r)));
    emit_byte(`mut e, 0xF7); emit_byte(`mut e, modrm(3, 7, r));
}

/* ─── Logic ───────────────────────────────────────────────────────────────── */
fn emit_and_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(dst)));
    emit_byte(`mut e, 0x21); emit_byte(`mut e, modrm(3, src, dst));
}
fn emit_or_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(dst)));
    emit_byte(`mut e, 0x09); emit_byte(`mut e, modrm(3, src, dst));
}
fn emit_xor_reg_reg(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(src), false, needs_rex(dst)));
    emit_byte(`mut e, 0x31); emit_byte(`mut e, modrm(3, src, dst));
}
fn emit_not_reg(`mut EmitCtx e, i32 r) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(r)));
    emit_byte(`mut e, 0xF7); emit_byte(`mut e, modrm(3, 2, r));
}
fn emit_neg_reg(`mut EmitCtx e, i32 r) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(r)));
    emit_byte(`mut e, 0xF7); emit_byte(`mut e, modrm(3, 3, r));
}

/* ─── Comparisons ─────────────────────────────────────────────────────────── */
fn emit_cmp_reg_reg(`mut EmitCtx e, i32 a, i32 b) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(b), false, needs_rex(a)));
    emit_byte(`mut e, 0x39); emit_byte(`mut e, modrm(3, b, a));
}
fn emit_cmp_reg_imm32(`mut EmitCtx e, i32 r, i32 imm) -> void {
    emit_byte(`mut e, rex_byte(true, false, false, needs_rex(r)));
    emit_byte(`mut e, 0x81); emit_byte(`mut e, modrm(3, 7, r));
    e.text.push_u32(imm as u32);
}
fn emit_test_reg_reg(`mut EmitCtx e, i32 a, i32 b) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(b), false, needs_rex(a)));
    emit_byte(`mut e, 0x85); emit_byte(`mut e, modrm(3, b, a));
}
fn emit_sete(`mut EmitCtx e, i32 r)  -> void { emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x94); emit_byte(`mut e, modrm(3, 0, r)); }
fn emit_setne(`mut EmitCtx e, i32 r) -> void { emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x95); emit_byte(`mut e, modrm(3, 0, r)); }
fn emit_setl(`mut EmitCtx e, i32 r)  -> void { emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x9C); emit_byte(`mut e, modrm(3, 0, r)); }
fn emit_setle(`mut EmitCtx e, i32 r) -> void { emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x9E); emit_byte(`mut e, modrm(3, 0, r)); }
fn emit_setg(`mut EmitCtx e, i32 r)  -> void { emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x9F); emit_byte(`mut e, modrm(3, 0, r)); }
fn emit_setge(`mut EmitCtx e, i32 r) -> void { emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x9D); emit_byte(`mut e, modrm(3, 0, r)); }
fn emit_movzx_reg8(`mut EmitCtx e, i32 dst, i32 src) -> void {
    emit_byte(`mut e, rex_byte(true, needs_rex(dst), false, needs_rex(src)));
    emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0xB6);
    emit_byte(`mut e, modrm(3, dst, src));
}

/* ─── Stack ───────────────────────────────────────────────────────────────── */
fn emit_push(`mut EmitCtx e, i32 r) -> void {
    if needs_rex(r) { emit_byte(`mut e, rex_byte(false, false, false, true)); }
    emit_byte(`mut e, (0x50 | (r & 7)) as u8);
}
fn emit_pop(`mut EmitCtx e, i32 r) -> void {
    if needs_rex(r) { emit_byte(`mut e, rex_byte(false, false, false, true)); }
    emit_byte(`mut e, (0x58 | (r & 7)) as u8);
}

/* ─── Jumps (with relocation) ────────────────────────────────────────────── */
fn add_reloc(`mut EmitCtx e, usize lbl, i32 addend) -> void {
    if e.reloc_count >= e.reloc_cap {
        e.reloc_cap = if e.reloc_cap == 0 { 64 } else { e.reloc_cap * 2 };
        e.relocs = mem.realloc_array<Reloc>(e.relocs, e.reloc_cap);
    }
    Reloc* r    = `mut e.relocs[e.reloc_count];
    r.patch_offset = e.text.len;
    r.target_label = lbl;
    r.addend       = addend;
    e.reloc_count  = e.reloc_count + 1;
}

fn emit_jmp(`mut EmitCtx e, usize lbl) -> void {
    emit_byte(`mut e, 0xE9);
    add_reloc(`mut e, lbl, -4);
    e.text.push_u32(0xDEADBEEF as u32);
}
fn emit_je(`mut EmitCtx e, usize lbl) -> void {
    emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x84);
    add_reloc(`mut e, lbl, -4);
    e.text.push_u32(0xDEADBEEF as u32);
}
fn emit_jne(`mut EmitCtx e, usize lbl) -> void {
    emit_byte(`mut e, 0x0F); emit_byte(`mut e, 0x85);
    add_reloc(`mut e, lbl, -4);
    e.text.push_u32(0xDEADBEEF as u32);
}

/* ─── External call (via TextPatch) ──────────────────────────────────────── */
fn add_patch(`mut EmitCtx e, usize nops, `str text) -> void {
    if e.patch_count >= e.patch_cap {
        e.patch_cap = if e.patch_cap == 0 { 64 } else { e.patch_cap * 2 };
        e.patches = mem.realloc_array<TextPatch>(e.patches, e.patch_cap);
    }
    TextPatch* tp  = `mut e.patches[e.patch_count];
    tp.offset      = e.text.len;
    tp.nop_count   = nops;
    tp.asm_text    = text;
    e.patch_count  = e.patch_count + 1;
}

fn emit_call_extern(`mut EmitCtx e, `str name) -> void {
    declare_external(`mut e, name);
    str patch_text = fmt.sprintf("call %s@PLT", name);
    add_patch(`mut e, 5, patch_text);
    /* 5 NOP placeholder bytes */
    usize i = 0;
    while i < 5 { emit_byte(`mut e, 0x90); i = i + 1; }
}

fn emit_call_label(`mut EmitCtx e, usize lbl) -> void {
    emit_byte(`mut e, 0xE8);
    add_reloc(`mut e, lbl, -4);
    e.text.push_u32(0);
}

/* ─── LEA rip-relative (via TextPatch) ───────────────────────────────────── */
fn emit_lea_rip(`mut EmitCtx e, i32 dst, usize str_idx) -> void {
    str reg_name = REG_NAMES[if dst < 16 { dst } else { 0 }];
    str patch_text = fmt.sprintf("lea %s, [rip + __cprime_str_%d]", reg_name, str_idx);
    add_patch(`mut e, 7, patch_text);
    usize i = 0;
    while i < 7 { emit_byte(`mut e, 0x90); i = i + 1; }
}

/* ─── Function prologue / epilogue ───────────────────────────────────────── */
fn emit_prologue(`mut EmitCtx e, i32 frame) -> void {
    emit_push(`mut e, RBP);
    emit_mov_reg_reg(`mut e, RBP, RSP);
    i32 aligned = (frame + 15) & ~15;
    if aligned > 0 { emit_sub_reg_imm32(`mut e, RSP, aligned); }
}
fn emit_epilogue(`mut EmitCtx e) -> void {
    emit_byte(`mut e, 0xC9);  /* LEAVE */
    emit_byte(`mut e, 0xC3);  /* RET */
}

/* ─── Labels ──────────────────────────────────────────────────────────────── */
fn new_label(`mut EmitCtx e, `str name) -> usize {
    if e.label_count >= e.label_cap {
        e.label_cap = if e.label_cap == 0 { 64 } else { e.label_cap * 2 };
        e.labels = mem.realloc_array<Label>(e.labels, e.label_cap);
    }
    Label* l      = `mut e.labels[e.label_count];
    l.name        = name;
    l.code_offset = UNRESOLVED;
    l.is_global   = false;
    usize id      = e.label_count;
    e.label_count = e.label_count + 1;
    return id;
}

fn bind_label(`mut EmitCtx e, usize id) -> void {
    if id < e.label_count {
        e.labels[id].code_offset = e.text.len;
    }
}

/* ─── Resolve internal relocations ───────────────────────────────────────── */
fn resolve_relocs(`mut EmitCtx e) -> void {
    usize i = 0;
    while i < e.reloc_count {
        Reloc* r = `mut e.relocs[i];
        if r.target_label >= e.label_count { i = i + 1; continue; }
        Label* lbl = `e.labels[r.target_label];
        if lbl.code_offset == UNRESOLVED { i = i + 1; continue; }
        i64 delta = lbl.code_offset as i64 - (r.patch_offset as i64 + 4) + r.addend as i64 + 4;
        u32 d32 = delta as u32;
        e.text.data[r.patch_offset + 0] = (d32 & 0xFF) as u8;
        e.text.data[r.patch_offset + 1] = ((d32 >> 8)  & 0xFF) as u8;
        e.text.data[r.patch_offset + 2] = ((d32 >> 16) & 0xFF) as u8;
        e.text.data[r.patch_offset + 3] = ((d32 >> 24) & 0xFF) as u8;
        i = i + 1;
    }
}

/* ─── String intern ───────────────────────────────────────────────────────── */
fn EmitCtx.intern_string(`mut EmitCtx self, `str s) -> usize {
    /* Check if already interned */
    usize i = 0;
    while i < self.string_count {
        if string.eq(self.strings[i].text, s) { return i; }
        i = i + 1;
    }
    /* Add new entry */
    if self.string_count >= self.string_cap {
        self.string_cap = if self.string_cap == 0 { 16 } else { self.string_cap * 2 };
        self.strings = mem.realloc_array<StringEntry>(self.strings, self.string_cap);
    }
    StringEntry* entry    = `mut self.strings[self.string_count];
    entry.text            = s;
    entry.rodata_offset   = self.rodata.len;
    /* Write to rodata (null-terminated) */
    usize j = 0;
    while j < string.len(s) {
        self.rodata.push(string.char_at(s, j) as u8);
        j = j + 1;
    }
    self.rodata.push(0);   /* null terminator */
    usize id              = self.string_count;
    self.string_count     = self.string_count + 1;
    return id;
}

/* ─── External declaration ────────────────────────────────────────────────── */
fn declare_external(`mut EmitCtx e, `str name) -> void {
    usize i = 0;
    while i < e.external_count {
        if string.eq(e.externals[i], name) { return; }
        i = i + 1;
    }
    if e.external_count >= e.external_cap {
        e.external_cap = if e.external_cap == 0 { 16 } else { e.external_cap * 2 };
        e.externals = mem.realloc_array<str>(e.externals, e.external_cap);
    }
    e.externals[e.external_count] = name;
    e.external_count = e.external_count + 1;
}

/* ─── Debug dump ──────────────────────────────────────────────────────────── */
fn emit_dump_asm(`EmitCtx e) -> void {
    io.printf("=== Emitted code: %d bytes ===\n", e.text.len);
    io.printf(".rodata: %d bytes (%d strings)\n", e.rodata.len, e.string_count);
    io.printf("Labels: %d  Relocs: %d  Patches: %d  Externs: %d\n\n",
              e.label_count, e.reloc_count, e.patch_count, e.external_count);
}
