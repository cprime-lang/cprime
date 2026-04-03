/*
 * C-Prime Bootstrap — x86_64_emit.c
 * ===================================
 * Emits real x86_64 machine code bytes into CodegenContext.text.
 *
 * All instructions follow the Intel x86_64 encoding. References:
 *   - Intel® 64 and IA-32 Architectures SDM Vol. 2
 *   - System V AMD64 ABI
 *
 * REX prefix: 0x40 | W(bit3) | R(bit2) | X(bit1) | B(bit0)
 *   W=1: 64-bit operand   R: ModRM.reg extension
 *   B:   ModRM.rm extension  X: SIB.index extension
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "../../include/codegen.h"

static void tp_push(CodegenContext* ctx, size_t off, size_t len, const char* text);

/* ─── Code Buffer ─────────────────────────────────────────────────────────── */
static void buf_grow(CodeBuf* b, size_t needed) {
    if (b->len + needed <= b->cap) return;
    size_t new_cap = b->cap == 0 ? 4096 : b->cap * 2;
    while (new_cap < b->len + needed) new_cap *= 2;
    b->buf = realloc(b->buf, new_cap);
    if (!b->buf) { fprintf(stderr, "[codegen] OOM\n"); exit(1); }
    b->cap = new_cap;
}

void emit_byte(CodegenContext* ctx, uint8_t b) {
    buf_grow(&ctx->text, 1);
    ctx->text.buf[ctx->text.len++] = b;
}
void emit_word(CodegenContext* ctx, uint16_t w) {
    buf_grow(&ctx->text, 2);
    ctx->text.buf[ctx->text.len++] = (uint8_t)(w);
    ctx->text.buf[ctx->text.len++] = (uint8_t)(w >> 8);
}
void emit_dword(CodegenContext* ctx, uint32_t d) {
    buf_grow(&ctx->text, 4);
    ctx->text.buf[ctx->text.len++] = (uint8_t)(d);
    ctx->text.buf[ctx->text.len++] = (uint8_t)(d >> 8);
    ctx->text.buf[ctx->text.len++] = (uint8_t)(d >> 16);
    ctx->text.buf[ctx->text.len++] = (uint8_t)(d >> 24);
}
void emit_qword(CodegenContext* ctx, uint64_t q) {
    emit_dword(ctx, (uint32_t)(q));
    emit_dword(ctx, (uint32_t)(q >> 32));
}

static void rodata_byte(CodegenContext* ctx, uint8_t b) {
    buf_grow(&ctx->rodata, 1);
    ctx->rodata.buf[ctx->rodata.len++] = b;
}

/* ─── REX helpers ─────────────────────────────────────────────────────────── */
/* REX.W = 1: 64-bit operand size */
static uint8_t rex(bool w, bool r, bool x, bool b) {
    return 0x40 | ((uint8_t)w << 3) | ((uint8_t)r << 2) |
                  ((uint8_t)x << 1) | (uint8_t)b;
}
static bool needs_rex_b(Reg r) { return r >= R8 && r <= R15; }
static bool needs_rex_r(Reg r) { return r >= R8 && r <= R15; }
static uint8_t rm_byte(uint8_t mod, Reg reg, Reg rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* ─── MOV ─────────────────────────────────────────────────────────────────── */
/* MOV r64, r64 */
void emit_mov_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(dst)));
    emit_byte(ctx, 0x89);  /* MOV r/m64, r64 */
    emit_byte(ctx, rm_byte(3, src, dst));
}

/* MOV r64, imm64 */
void emit_mov_reg_imm64(CodegenContext* ctx, Reg dst, int64_t imm) {
    emit_byte(ctx, rex(true, false, false, needs_rex_b(dst)));
    emit_byte(ctx, 0xB8 | (dst & 7));  /* MOV r64, imm64 */
    emit_qword(ctx, (uint64_t)imm);
}

/* MOV r64, [base + offset] */
void emit_mov_reg_mem(CodegenContext* ctx, Reg dst, Reg base, int32_t off) {
    emit_byte(ctx, rex(true, needs_rex_r(dst), false, needs_rex_b(base)));
    emit_byte(ctx, 0x8B);  /* MOV r64, r/m64 */
    if (off == 0 && (base & 7) != 5) {
        emit_byte(ctx, rm_byte(0, dst, base));
        if ((base & 7) == 4) emit_byte(ctx, 0x24); /* SIB for RSP */
    } else if (off >= -128 && off <= 127) {
        emit_byte(ctx, rm_byte(1, dst, base));
        if ((base & 7) == 4) emit_byte(ctx, 0x24);
        emit_byte(ctx, (uint8_t)(int8_t)off);
    } else {
        emit_byte(ctx, rm_byte(2, dst, base));
        if ((base & 7) == 4) emit_byte(ctx, 0x24);
        emit_dword(ctx, (uint32_t)off);
    }
}

/* MOV [base + offset], r64 */
void emit_mov_mem_reg(CodegenContext* ctx, Reg base, int32_t off, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(base)));
    emit_byte(ctx, 0x89);  /* MOV r/m64, r64 */
    if (off == 0 && (base & 7) != 5) {
        emit_byte(ctx, rm_byte(0, src, base));
        if ((base & 7) == 4) emit_byte(ctx, 0x24);
    } else if (off >= -128 && off <= 127) {
        emit_byte(ctx, rm_byte(1, src, base));
        if ((base & 7) == 4) emit_byte(ctx, 0x24);
        emit_byte(ctx, (uint8_t)(int8_t)off);
    } else {
        emit_byte(ctx, rm_byte(2, src, base));
        if ((base & 7) == 4) emit_byte(ctx, 0x24);
        emit_dword(ctx, (uint32_t)off);
    }
}

/* ─── Arithmetic ──────────────────────────────────────────────────────────── */
void emit_add_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(dst)));
    emit_byte(ctx, 0x01); emit_byte(ctx, rm_byte(3, src, dst));
}
void emit_add_reg_imm32(CodegenContext* ctx, Reg dst, int32_t imm) {
    if (imm >= -128 && imm <= 127) {
        emit_byte(ctx, rex(true, false, false, needs_rex_b(dst)));
        emit_byte(ctx, 0x83); emit_byte(ctx, rm_byte(3, 0, dst));
        emit_byte(ctx, (uint8_t)(int8_t)imm);
    } else {
        emit_byte(ctx, rex(true, false, false, needs_rex_b(dst)));
        emit_byte(ctx, 0x81); emit_byte(ctx, rm_byte(3, 0, dst));
        emit_dword(ctx, (uint32_t)imm);
    }
}
void emit_sub_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(dst)));
    emit_byte(ctx, 0x29); emit_byte(ctx, rm_byte(3, src, dst));
}
void emit_sub_reg_imm32(CodegenContext* ctx, Reg dst, int32_t imm) {
    if (imm >= -128 && imm <= 127) {
        emit_byte(ctx, rex(true, false, false, needs_rex_b(dst)));
        emit_byte(ctx, 0x83); emit_byte(ctx, rm_byte(3, 5, dst));
        emit_byte(ctx, (uint8_t)(int8_t)imm);
    } else {
        emit_byte(ctx, rex(true, false, false, needs_rex_b(dst)));
        emit_byte(ctx, 0x81); emit_byte(ctx, rm_byte(3, 5, dst));
        emit_dword(ctx, (uint32_t)imm);
    }
}
void emit_imul_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(dst), false, needs_rex_b(src)));
    emit_byte(ctx, 0x0F); emit_byte(ctx, 0xAF);
    emit_byte(ctx, rm_byte(3, dst, src));
}
void emit_cqo(CodegenContext* ctx) {
    emit_byte(ctx, 0x48); emit_byte(ctx, 0x99); /* REX.W CQO */
}
void emit_idiv_reg(CodegenContext* ctx, Reg r) {
    emit_byte(ctx, rex(true, false, false, needs_rex_b(r)));
    emit_byte(ctx, 0xF7); emit_byte(ctx, rm_byte(3, 7, r));
}

/* ─── Logic ───────────────────────────────────────────────────────────────── */
void emit_and_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(dst)));
    emit_byte(ctx, 0x21); emit_byte(ctx, rm_byte(3, src, dst));
}
/* AND reg, imm8 (sign-extended) — used for AND RSP, -16 alignment */
void emit_and_reg_imm8(CodegenContext* ctx, Reg dst, int8_t imm) {
    emit_byte(ctx, rex(true, false, false, needs_rex_b(dst)));
    emit_byte(ctx, 0x83);
    emit_byte(ctx, rm_byte(3, 4, dst));   /* /4 = AND */
    emit_byte(ctx, (uint8_t)imm);
}
void emit_or_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(dst)));
    emit_byte(ctx, 0x09); emit_byte(ctx, rm_byte(3, src, dst));
}
void emit_xor_reg_reg(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(src), false, needs_rex_b(dst)));
    emit_byte(ctx, 0x31); emit_byte(ctx, rm_byte(3, src, dst));
}
void emit_not_reg(CodegenContext* ctx, Reg r) {
    emit_byte(ctx, rex(true, false, false, needs_rex_b(r)));
    emit_byte(ctx, 0xF7); emit_byte(ctx, rm_byte(3, 2, r));
}
void emit_neg_reg(CodegenContext* ctx, Reg r) {
    emit_byte(ctx, rex(true, false, false, needs_rex_b(r)));
    emit_byte(ctx, 0xF7); emit_byte(ctx, rm_byte(3, 3, r));
}

/* ─── Comparison ──────────────────────────────────────────────────────────── */
void emit_cmp_reg_reg(CodegenContext* ctx, Reg a, Reg b) {
    emit_byte(ctx, rex(true, needs_rex_r(b), false, needs_rex_b(a)));
    emit_byte(ctx, 0x39); emit_byte(ctx, rm_byte(3, b, a));
}
void emit_cmp_reg_imm32(CodegenContext* ctx, Reg r, int32_t imm) {
    emit_byte(ctx, rex(true, false, false, needs_rex_b(r)));
    emit_byte(ctx, 0x81); emit_byte(ctx, rm_byte(3, 7, r));
    emit_dword(ctx, (uint32_t)imm);
}
void emit_test_reg_reg(CodegenContext* ctx, Reg a, Reg b) {
    emit_byte(ctx, rex(true, needs_rex_r(b), false, needs_rex_b(a)));
    emit_byte(ctx, 0x85); emit_byte(ctx, rm_byte(3, b, a));
}

/* SETcc — write 1 byte result to low byte of register */
static void emit_setcc(CodegenContext* ctx, uint8_t cc, Reg r) {
    if (needs_rex_b(r)) emit_byte(ctx, rex(false, false, false, true));
    emit_byte(ctx, 0x0F); emit_byte(ctx, cc); emit_byte(ctx, rm_byte(3, 0, r));
}
void emit_sete(CodegenContext* ctx, Reg r)  { emit_setcc(ctx, 0x94, r); }
void emit_setne(CodegenContext* ctx, Reg r) { emit_setcc(ctx, 0x95, r); }
void emit_setl(CodegenContext* ctx, Reg r)  { emit_setcc(ctx, 0x9C, r); }
void emit_setle(CodegenContext* ctx, Reg r) { emit_setcc(ctx, 0x9E, r); }
void emit_setg(CodegenContext* ctx, Reg r)  { emit_setcc(ctx, 0x9F, r); }
void emit_setge(CodegenContext* ctx, Reg r) { emit_setcc(ctx, 0x9D, r); }

/* MOVZX r64, r8 */
void emit_movzx_reg8(CodegenContext* ctx, Reg dst, Reg src) {
    emit_byte(ctx, rex(true, needs_rex_r(dst), false, needs_rex_b(src)));
    emit_byte(ctx, 0x0F); emit_byte(ctx, 0xB6);
    emit_byte(ctx, rm_byte(3, dst, src));
}

/* ─── Stack ────────────────────────────────────────────────────────────────── */
void emit_push(CodegenContext* ctx, Reg r) {
    if (needs_rex_b(r)) emit_byte(ctx, rex(false, false, false, true));
    emit_byte(ctx, 0x50 | (r & 7));
}
void emit_pop(CodegenContext* ctx, Reg r) {
    if (needs_rex_b(r)) emit_byte(ctx, rex(false, false, false, true));
    emit_byte(ctx, 0x58 | (r & 7));
}
void emit_push_imm32(CodegenContext* ctx, int32_t imm) {
    emit_byte(ctx, 0x68); emit_dword(ctx, (uint32_t)imm);
}

/* ─── Jumps ────────────────────────────────────────────────────────────────── */
/* We emit 32-bit relative jumps and add a relocation to patch them later */
static void emit_jump_common(CodegenContext* ctx, uint8_t opcode1, uint8_t opcode2,
                              size_t label_id) {
    if (opcode1 != 0) emit_byte(ctx, opcode1);
    emit_byte(ctx, opcode2);

    /* Record relocation */
    if (ctx->reloc_count >= ctx->reloc_cap) {
        ctx->reloc_cap = ctx->reloc_cap == 0 ? 64 : ctx->reloc_cap * 2;
        ctx->relocs = realloc(ctx->relocs, ctx->reloc_cap * sizeof(Reloc));
    }
    Reloc r = {0};
    r.patch_offset    = ctx->text.len;
    r.target_label    = label_id;
    r.addend          = -4;
    r.is_rip_relative = true;
    ctx->relocs[ctx->reloc_count++] = r;

    emit_dword(ctx, 0xDEADBEEF); /* placeholder */
}

void emit_jmp(CodegenContext* ctx, size_t label) { emit_jump_common(ctx, 0, 0xE9, label); }
void emit_je(CodegenContext* ctx, size_t label)   { emit_jump_common(ctx, 0x0F, 0x84, label); }
void emit_jne(CodegenContext* ctx, size_t label)  { emit_jump_common(ctx, 0x0F, 0x85, label); }
void emit_jz(CodegenContext* ctx, size_t label)   { emit_jump_common(ctx, 0x0F, 0x84, label); }

/* ─── Call ─────────────────────────────────────────────────────────────────── */
/* Call to a known C-Prime function label */
void emit_call_label(CodegenContext* ctx, size_t label_id) {
    emit_byte(ctx, 0xE8); /* CALL rel32 */
    if (ctx->reloc_count >= ctx->reloc_cap) {
        ctx->reloc_cap = ctx->reloc_cap == 0 ? 64 : ctx->reloc_cap * 2;
        ctx->relocs = realloc(ctx->relocs, ctx->reloc_cap * sizeof(Reloc));
    }
    Reloc r = {0};
    r.patch_offset    = ctx->text.len;
    r.target_label    = label_id;
    r.addend          = -4;
    r.is_rip_relative = true;
    ctx->relocs[ctx->reloc_count++] = r;
    emit_dword(ctx, 0); /* placeholder */
}

/* Call extern: emit 5-byte NOP + record text patch for as to resolve via PLT */
void emit_call_extern(CodegenContext* ctx, const char* name) {
    declare_external(ctx, name);   /* keep the extern list for linker cmdline */
    char text[256];
    snprintf(text, sizeof(text), "call %s@PLT", name);
    size_t off = ctx->text.len;
    for (int i = 0; i < 5; i++) emit_byte(ctx, 0x90); /* 5 NOP placeholder */
    tp_push(ctx, off, 5, text);
}

void emit_ret(CodegenContext* ctx)   { emit_byte(ctx, 0xC3); }
void emit_leave(CodegenContext* ctx) { emit_byte(ctx, 0xC9); }

/* ─── Function prologue/epilogue ──────────────────────────────────────────── */
void emit_prologue(CodegenContext* ctx, int frame_size) {
    emit_push(ctx, RBP);                        /* push rbp */
    emit_mov_reg_reg(ctx, RBP, RSP);            /* mov rbp, rsp */
    /* Align frame to 16 bytes */
    int aligned = (frame_size + 15) & ~15;
    if (aligned > 0) {
        emit_sub_reg_imm32(ctx, RSP, aligned);  /* sub rsp, frame_size */
    }
}

void emit_epilogue(CodegenContext* ctx) {
    emit_leave(ctx);  /* mov rsp, rbp; pop rbp */
    emit_ret(ctx);
}

static void tp_push(CodegenContext* ctx, size_t off, size_t len, const char* text) {
    if (ctx->text_patch_count >= ctx->text_patch_cap) {
        ctx->text_patch_cap = ctx->text_patch_cap == 0 ? 64 : ctx->text_patch_cap * 2;
        ctx->text_patches = realloc(ctx->text_patches, ctx->text_patch_cap * sizeof(TextPatch));
    }
    TextPatch* tp = &ctx->text_patches[ctx->text_patch_count++];
    tp->offset    = off;
    tp->instr_len = len;
    strncpy(tp->text, text, sizeof(tp->text) - 1);
}

/* ─── LEA (load address of string in .rodata) ─────────────────────────────── */


void emit_lea_rip(CodegenContext* ctx, Reg dst, size_t string_idx) {
    /* Emit 7 NOP bytes as placeholder (REX + opcode + ModRM + disp32) */
    static const char* reg_names[] = {
        "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
        "r8", "r9", "r10","r11","r12","r13","r14","r15"
    };
    size_t off = ctx->text.len;
    char text[256];
    snprintf(text, sizeof(text), "lea %s, [rip + __cprime_str_%zu]",
             reg_names[dst < 16 ? dst : 0], string_idx);
    /* emit 7 NOP bytes (size of a REX LEA rip+disp32) */
    for (int i = 0; i < 7; i++) emit_byte(ctx, 0x90); /* NOP */
    tp_push(ctx, off, 7, text);
}

/* ─── Syscall ──────────────────────────────────────────────────────────────── */
void emit_syscall(CodegenContext* ctx) {
    emit_byte(ctx, 0x0F); emit_byte(ctx, 0x05);
}

/* ─── Labels ───────────────────────────────────────────────────────────────── */
size_t new_label(CodegenContext* ctx, const char* name) {
    if (ctx->label_count >= ctx->label_cap) {
        ctx->label_cap = ctx->label_cap == 0 ? 64 : ctx->label_cap * 2;
        ctx->labels = realloc(ctx->labels, ctx->label_cap * sizeof(Label));
    }
    Label* l = &ctx->labels[ctx->label_count];
    memset(l, 0, sizeof(Label));
    if (name) strncpy(l->name, name, sizeof(l->name) - 1);
    l->code_offset = SIZE_MAX; /* unresolved */
    return ctx->label_count++;
}

void bind_label(CodegenContext* ctx, size_t label_id) {
    assert(label_id < ctx->label_count);
    ctx->labels[label_id].code_offset = ctx->text.len;
}

/* ─── Resolve relocations ──────────────────────────────────────────────────── */
void resolve_relocs(CodegenContext* ctx) {
    for (size_t i = 0; i < ctx->reloc_count; i++) {
        Reloc* r = &ctx->relocs[i];

        /* Skip external / .rodata relocs (handled in ELF writer) */
        if (r->target_label >= 0x80000000UL) continue;

        if (r->target_label >= ctx->label_count) {
            fprintf(stderr, "[codegen] reloc to undefined label %zu\n", r->target_label);
            ctx->had_error = true;
            continue;
        }

        size_t target_off = ctx->labels[r->target_label].code_offset;
        if (target_off == SIZE_MAX) {
            fprintf(stderr, "[codegen] unresolved label '%s'\n",
                    ctx->labels[r->target_label].name);
            ctx->had_error = true;
            continue;
        }

        /* RIP-relative: patch = target - (patch_end) */
        int32_t delta = (int32_t)((int64_t)target_off -
                                  (int64_t)(r->patch_offset + 4) +
                                  r->addend + 4);
        uint8_t* p = &ctx->text.buf[r->patch_offset];
        p[0] = (uint8_t)(delta);
        p[1] = (uint8_t)(delta >> 8);
        p[2] = (uint8_t)(delta >> 16);
        p[3] = (uint8_t)(delta >> 24);
    }
}

/* ─── Externals ────────────────────────────────────────────────────────────── */
size_t declare_external(CodegenContext* ctx, const char* name) {
    /* Check if already declared */
    for (size_t i = 0; i < ctx->external_count; i++) {
        if (strcmp(ctx->externals[i], name) == 0) return i;
    }
    if (ctx->external_count >= ctx->external_cap) {
        ctx->external_cap = ctx->external_cap == 0 ? 16 : ctx->external_cap * 2;
        ctx->externals = realloc(ctx->externals, ctx->external_cap * sizeof(char*));
    }
    ctx->externals[ctx->external_count] = strdup(name);
    return ctx->external_count++;
}
