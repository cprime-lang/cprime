/*
 * C-Prime Bootstrap Code Generator -- codegen.h
 * Interface for the x86_64 code generator and ELF binary writer.
 * THIS FILE IS THE AUTHORITATIVE CLEAN VERSION.
 * If you see duplicate structs, delete this file and replace with this content.
 */

#ifndef CPRIME_CODEGEN_H
#define CPRIME_CODEGEN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "parser.h"

typedef enum {
    RAX=0,RCX=1,RDX=2,RBX=3,RSP=4,RBP=5,RSI=6,RDI=7,
    R8=8,R9=9,R10=10,R11=11,R12=12,R13=13,R14=14,R15=15,
    EAX=16,ECX=17,EDX=18,REG_NONE=255
} Reg;

#define ARG_REG_COUNT 6
static const Reg ARG_REGS[ARG_REG_COUNT] = { RDI, RSI, RDX, RCX, R8, R9 };
#define RETURN_REG RAX

typedef enum { SYM_LOCAL, SYM_GLOBAL, SYM_FUNCTION, SYM_PARAM, SYM_STRING, SYM_CONST_STR } SymKind;

typedef struct Symbol {
    char name[128]; SymKind kind; int stack_offset;
    size_t label; bool is_mut; struct Symbol* next;
} Symbol;

typedef struct { Symbol* head; } SymTable;
typedef struct { char* data; size_t len; size_t section_offset; } StringLit;
typedef struct { uint8_t* buf; size_t len; size_t cap; } CodeBuf;

typedef struct {
    size_t patch_offset; size_t target_label;
    int addend; bool is_rip_relative; bool is_absolute;
} Reloc;

typedef struct {
    char name[128]; size_t code_offset; bool is_global;
} Label;

/* TextPatch: replaces NOP placeholder bytes with real assembly mnemonics */
typedef struct {
    size_t offset;      /* byte offset in text buffer (start of NOP placeholder) */
    size_t instr_len;   /* number of NOP bytes emitted */
    char   text[512];   /* replacement assembly text, e.g. "call puts@PLT" */
} TextPatch;

typedef struct {
    const char*  input_filename;
    const char*  output_filename;
    CodeBuf      text;
    CodeBuf      rodata;
    CodeBuf      data;
    CodeBuf      bss;
    Label*  labels;
    size_t  label_count;
    size_t  label_cap;
    Reloc*  relocs;
    size_t  reloc_count;
    size_t  reloc_cap;
    StringLit* strings;
    size_t     string_count;
    size_t     string_cap;
    SymTable*  scope_stack[64];
    int        scope_depth;
    int        stack_frame_size;
    int        local_offset;
    const char** externals;
    size_t       external_count;
    size_t       external_cap;
    char   current_fn[128];
    size_t fn_epilog_label;
    size_t continue_label;
    size_t break_label;
    TextPatch* text_patches;
    size_t     text_patch_count;
    size_t     text_patch_cap;
    bool   dump_asm;
    bool   had_error;
    size_t label_counter;
} CodegenContext;

/* API */
CodegenContext* codegen_create(const char* input, const char* output);
int  codegen_emit(CodegenContext* ctx, ASTNode* ast);
int  codegen_write_elf(CodegenContext* ctx);
void codegen_dump_asm(CodegenContext* ctx);
void codegen_free(CodegenContext* ctx);

/* Instruction emitters */
void emit_byte(CodegenContext* ctx, uint8_t b);
void emit_word(CodegenContext* ctx, uint16_t w);
void emit_dword(CodegenContext* ctx, uint32_t d);
void emit_qword(CodegenContext* ctx, uint64_t q);
void emit_mov_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_mov_reg_imm64(CodegenContext* ctx, Reg dst, int64_t imm);
void emit_mov_reg_mem(CodegenContext* ctx, Reg dst, Reg base, int32_t offset);
void emit_mov_mem_reg(CodegenContext* ctx, Reg base, int32_t offset, Reg src);
void emit_add_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_add_reg_imm32(CodegenContext* ctx, Reg dst, int32_t imm);
void emit_sub_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_sub_reg_imm32(CodegenContext* ctx, Reg dst, int32_t imm);
void emit_imul_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_idiv_reg(CodegenContext* ctx, Reg divisor);
void emit_cqo(CodegenContext* ctx);
void emit_and_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_and_reg_imm8(CodegenContext* ctx, Reg dst, int8_t imm);
void emit_or_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_xor_reg_reg(CodegenContext* ctx, Reg dst, Reg src);
void emit_not_reg(CodegenContext* ctx, Reg r);
void emit_neg_reg(CodegenContext* ctx, Reg r);
void emit_cmp_reg_reg(CodegenContext* ctx, Reg a, Reg b);
void emit_cmp_reg_imm32(CodegenContext* ctx, Reg r, int32_t imm);
void emit_test_reg_reg(CodegenContext* ctx, Reg a, Reg b);
void emit_sete(CodegenContext* ctx, Reg r);
void emit_setne(CodegenContext* ctx, Reg r);
void emit_setl(CodegenContext* ctx, Reg r);
void emit_setle(CodegenContext* ctx, Reg r);
void emit_setg(CodegenContext* ctx, Reg r);
void emit_setge(CodegenContext* ctx, Reg r);
void emit_movzx_reg8(CodegenContext* ctx, Reg dst, Reg src);
void emit_jmp(CodegenContext* ctx, size_t label);
void emit_je(CodegenContext* ctx, size_t label);
void emit_jne(CodegenContext* ctx, size_t label);
void emit_jz(CodegenContext* ctx, size_t label);
void emit_push(CodegenContext* ctx, Reg r);
void emit_pop(CodegenContext* ctx, Reg r);
void emit_push_imm32(CodegenContext* ctx, int32_t imm);
void emit_call_extern(CodegenContext* ctx, const char* name);
void emit_call_label(CodegenContext* ctx, size_t label);
void emit_ret(CodegenContext* ctx);
void emit_leave(CodegenContext* ctx);
void emit_prologue(CodegenContext* ctx, int frame_size);
void emit_epilogue(CodegenContext* ctx);
void emit_lea_rip(CodegenContext* ctx, Reg dst, size_t string_idx);
void emit_syscall(CodegenContext* ctx);
size_t new_label(CodegenContext* ctx, const char* name);
void   bind_label(CodegenContext* ctx, size_t label_id);
void   resolve_relocs(CodegenContext* ctx);
size_t declare_external(CodegenContext* ctx, const char* name);

#endif /* CPRIME_CODEGEN_H */
