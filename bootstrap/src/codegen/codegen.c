/*
 * C-Prime Bootstrap Code Generator — codegen.c
 * ===============================================
 * Walks the AST and emits x86_64 machine code.
 *
 * Code generation strategy:
 *   - Each expression is compiled into RAX (the "value register")
 *   - Binary ops: left → RAX, push; right → RAX; pop RCX; combine
 *   - Function calls: args into RDI, RSI, RDX, RCX, R8, R9 (System V ABI)
 *   - All locals live on the stack (no register allocation yet)
 *   - Strings are placed in .rodata and loaded via LEA
 *
 * This is a single-pass, direct-emit code generator — no IR.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#include "../../include/codegen.h"


/* ─── Struct registry ─────────────────────────────────────────────────────── */
#define MAX_STRUCT_DEFS   64
#define MAX_STRUCT_FIELDS 32
#define MAX_VAR_TYPES    256

typedef struct {
    char  name[128];
    char  fields[MAX_STRUCT_FIELDS][128];
    size_t field_count;
} StructDef;

typedef struct { char var_name[128]; char struct_name[128]; } VarType;

static StructDef g_structs[MAX_STRUCT_DEFS];
static size_t    g_struct_count = 0;
static VarType   g_var_types[MAX_VAR_TYPES];
static size_t    g_var_type_count = 0;

static void register_struct(ASTNode* def) {
    if (g_struct_count >= MAX_STRUCT_DEFS) return;
    StructDef* s = &g_structs[g_struct_count++];
    strncpy(s->name, def->as.struct_def.name, sizeof(s->name) - 1);
    s->field_count = 0;
    for (size_t i = 0; i < def->as.struct_def.field_count && i < MAX_STRUCT_FIELDS; i++) {
        ASTNode* field = def->as.struct_def.fields[i];
        strncpy(s->fields[i], field->as.var_decl.name, sizeof(s->fields[0]) - 1);
        s->field_count++;
    }
}

static int field_offset(const char* sname, const char* fname) {
    for (size_t i = 0; i < g_struct_count; i++) {
        if (strcmp(g_structs[i].name, sname) != 0) continue;
        for (size_t j = 0; j < g_structs[i].field_count; j++)
            if (strcmp(g_structs[i].fields[j], fname) == 0) return (int)(j * 8);
    }
    return 0; /* default: field 0 */
}

static void record_var_struct(const char* var, const char* stype) {
    for (size_t i = 0; i < g_var_type_count; i++)
        if (strcmp(g_var_types[i].var_name, var) == 0) {
            strncpy(g_var_types[i].struct_name, stype, 127); return;
        }
    if (g_var_type_count >= MAX_VAR_TYPES) return;
    strncpy(g_var_types[g_var_type_count].var_name,    var,   127);
    strncpy(g_var_types[g_var_type_count].struct_name, stype, 127);
    g_var_type_count++;
}

static const char* var_struct_type(const char* var) {
    for (size_t i = 0; i < g_var_type_count; i++)
        if (strcmp(g_var_types[i].var_name, var) == 0)
            return g_var_types[i].struct_name;
    return "";
}

/* ─── Scope / Symbol Table ────────────────────────────────────────────────── */
static SymTable* scope_new(void) {
    return calloc(1, sizeof(SymTable));
}

static Symbol* scope_find_local(SymTable* s, const char* name) {
    for (Symbol* sym = s->head; sym; sym = sym->next)
        if (strcmp(sym->name, name) == 0) return sym;
    return NULL;
}

static void scope_push(CodegenContext* ctx) {
    assert(ctx->scope_depth < 63);
    ctx->scope_stack[++ctx->scope_depth] = scope_new();
}

static void scope_pop(CodegenContext* ctx) {
    SymTable* s = ctx->scope_stack[ctx->scope_depth--];
    /* Free all symbols */
    Symbol* sym = s->head;
    while (sym) { Symbol* n = sym->next; free(sym); sym = n; }
    free(s);
}

static Symbol* sym_define(CodegenContext* ctx, const char* name, SymKind kind) {
    Symbol* s = calloc(1, sizeof(Symbol));
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->kind = kind;

    if (kind == SYM_LOCAL || kind == SYM_PARAM) {
        ctx->local_offset -= 8;   /* each local takes 8 bytes */
        s->stack_offset = ctx->local_offset;
    }

    SymTable* tbl = ctx->scope_stack[ctx->scope_depth];
    s->next = tbl->head;
    tbl->head = s;
    return s;
}

static Symbol* sym_lookup(CodegenContext* ctx, const char* name) {
    for (int d = ctx->scope_depth; d >= 0; d--) {
        Symbol* s = scope_find_local(ctx->scope_stack[d], name);
        if (s) return s;
    }
    return NULL;
}

/* ─── String literal interning ────────────────────────────────────────────── */
static size_t intern_string(CodegenContext* ctx, const char* str) {
    size_t str_len = strlen(str) + 1; /* include null terminator */

    /* Check if already interned */
    for (size_t i = 0; i < ctx->string_count; i++) {
        if (ctx->strings[i].len == str_len &&
            memcmp(ctx->strings[i].data, str, str_len) == 0)
            return i;
    }

    /* Add new string */
    if (ctx->string_count >= ctx->string_cap) {
        ctx->string_cap = ctx->string_cap == 0 ? 16 : ctx->string_cap * 2;
        ctx->strings = realloc(ctx->strings, ctx->string_cap * sizeof(StringLit));
    }

    StringLit* s = &ctx->strings[ctx->string_count];
    s->data = malloc(str_len);
    memcpy(s->data, str, str_len);
    s->len = str_len;
    s->section_offset = ctx->rodata.len;

    /* Write to rodata buffer */
    for (size_t i = 0; i < str_len; i++) {
        size_t old_len = ctx->rodata.len;
        if (old_len >= ctx->rodata.cap) {
            ctx->rodata.cap = ctx->rodata.cap == 0 ? 1024 : ctx->rodata.cap * 2;
            ctx->rodata.buf = realloc(ctx->rodata.buf, ctx->rodata.cap);
        }
        ctx->rodata.buf[ctx->rodata.len++] = (uint8_t)str[i];
    }

    return ctx->string_count++;
}

/* ─── Error helper ────────────────────────────────────────────────────────── */
static void cg_error(CodegenContext* ctx, SourceLoc loc, const char* msg) {
    fprintf(stderr, "\033[31m[codegen] %s:%d: error: %s\033[0m\n",
            loc.filename, loc.line, msg);
    ctx->had_error = true;
}

/* ─── Forward declarations ────────────────────────────────────────────────── */
static void gen_expr(CodegenContext* ctx, ASTNode* n);
static void gen_stmt(CodegenContext* ctx, ASTNode* n);
static void gen_block(CodegenContext* ctx, ASTNode* n);

/* ─── Expression Code Generation ─────────────────────────────────────────── */
/* Result is always in RAX after gen_expr returns */

static void gen_expr(CodegenContext* ctx, ASTNode* n) {
    if (!n) { emit_xor_reg_reg(ctx, RAX, RAX); return; }

    switch (n->kind) {

    /* ── Integer literal ── */
    case AST_INT_LIT:
        emit_mov_reg_imm64(ctx, RAX, n->as.lit.int_val);
        break;

    /* ── Float literal (stored as int bits for now) ── */
    case AST_FLOAT_LIT: {
        /* Store the double in a union and load as int64 */
        union { double d; int64_t i; } u;
        u.d = n->as.lit.float_val;
        emit_mov_reg_imm64(ctx, RAX, u.i);
        break;
    }

    /* ── Bool literal ── */
    case AST_BOOL_LIT:
        emit_mov_reg_imm64(ctx, RAX, n->as.lit.bool_val ? 1 : 0);
        break;

    /* ── None / Void ── */
    case AST_NONE_LIT:
    case AST_VOID_EXPR:
        emit_xor_reg_reg(ctx, RAX, RAX);
        break;

    /* ── String literal: load address into RAX ── */
    case AST_STRING_LIT: {
        size_t idx = intern_string(ctx, n->as.lit.str_val);
        emit_lea_rip(ctx, RAX, idx);
        break;
    }

    /* ── Identifier: load from stack ── */
    case AST_IDENT: {
        /* Special bootstrap identifiers */
        if (strcmp(n->as.ident.name, "null")  == 0 ||
            strcmp(n->as.ident.name, "false") == 0 ||
            strcmp(n->as.ident.name, "None")  == 0) {
            emit_xor_reg_reg(ctx, RAX, RAX);
            break;
        }
        if (strcmp(n->as.ident.name, "true") == 0) {
            emit_mov_reg_imm64(ctx, RAX, 1);
            break;
        }

        Symbol* sym = sym_lookup(ctx, n->as.ident.name);
        if (!sym) {
            /* Try to find a function label */
            bool found_label = false;
            for (size_t i = 0; i < ctx->label_count; i++) {
                if (strcmp(ctx->labels[i].name, n->as.ident.name) == 0) {
                    emit_mov_reg_imm64(ctx, RAX, 0); /* placeholder */
                    found_label = true;
                    break;
                }
            }
            if (!found_label) {
                /* Try const table */
                char msg[256];
                snprintf(msg, sizeof(msg), "undefined identifier '%s'", n->as.ident.name);
                cg_error(ctx, n->loc, msg);
                emit_xor_reg_reg(ctx, RAX, RAX);
            }
            break;
        }
        if (sym->kind == SYM_CONST_STR) {
            /* Load string address: offset stored as 0x70000000 + string_index */
            size_t str_idx = (size_t)(sym->stack_offset - 0x70000000);
            emit_lea_rip(ctx, RAX, str_idx);
        } else if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
            emit_mov_reg_mem(ctx, RAX, RBP, sym->stack_offset);
        } else {
            emit_xor_reg_reg(ctx, RAX, RAX);
        }
        break;
    }

    /* ── Borrow: just evaluate the inner expression (no runtime cost) ── */
    case AST_BORROW:
    case AST_BORROW_MUT:
        gen_expr(ctx, n->as.borrow.expr);
        break;

    /* ── Deref: treat RAX as pointer, load [RAX] ── */
    case AST_DEREF:
        gen_expr(ctx, n->as.unary.operand);
        emit_mov_reg_mem(ctx, RAX, RAX, 0);
        break;

    /* ── Unary minus / not ── */
    case AST_UNARY:
        gen_expr(ctx, n->as.unary.operand);
        if (n->as.unary.op == TOK_MINUS) emit_neg_reg(ctx, RAX);
        else if (n->as.unary.op == TOK_NOT) {
            /* !x: compare to 0, sete al, movzx rax, al */
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_sete(ctx, RAX);
            emit_movzx_reg8(ctx, RAX, RAX);
        }
        break;

    /* ── Binary expression ── */
    case AST_BINARY: {
        /* Short-circuit for && and || */
        if (n->as.binary.op == TOK_AND) {
            size_t false_lbl = new_label(ctx, ".and_false");
            size_t end_lbl   = new_label(ctx, ".and_end");
            gen_expr(ctx, n->as.binary.left);
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_je(ctx, false_lbl);
            gen_expr(ctx, n->as.binary.right);
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_je(ctx, false_lbl);
            emit_mov_reg_imm64(ctx, RAX, 1);
            emit_jmp(ctx, end_lbl);
            bind_label(ctx, false_lbl);
            emit_xor_reg_reg(ctx, RAX, RAX);
            bind_label(ctx, end_lbl);
            break;
        }
        if (n->as.binary.op == TOK_OR) {
            size_t true_lbl = new_label(ctx, ".or_true");
            size_t end_lbl  = new_label(ctx, ".or_end");
            gen_expr(ctx, n->as.binary.left);
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_jne(ctx, true_lbl);
            gen_expr(ctx, n->as.binary.right);
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_jne(ctx, true_lbl);
            emit_xor_reg_reg(ctx, RAX, RAX);
            emit_jmp(ctx, end_lbl);
            bind_label(ctx, true_lbl);
            emit_mov_reg_imm64(ctx, RAX, 1);
            bind_label(ctx, end_lbl);
            break;
        }

        /* Standard binary: left → push; right → RAX; pop RCX */
        gen_expr(ctx, n->as.binary.left);
        emit_push(ctx, RAX);
        gen_expr(ctx, n->as.binary.right);
        emit_mov_reg_reg(ctx, RCX, RAX); /* RCX = right */
        emit_pop(ctx, RAX);              /* RAX = left */

        switch (n->as.binary.op) {
            case TOK_PLUS:     emit_add_reg_reg(ctx, RAX, RCX); break;
            case TOK_MINUS:    emit_sub_reg_reg(ctx, RAX, RCX); break;
            case TOK_STAR:     emit_imul_reg_reg(ctx, RAX, RCX); break;
            case TOK_SLASH:
                emit_cqo(ctx);
                emit_idiv_reg(ctx, RCX);
                break;
            case TOK_PERCENT:
                emit_cqo(ctx);
                emit_idiv_reg(ctx, RCX);
                emit_mov_reg_reg(ctx, RAX, RDX); /* remainder in RDX */
                break;
            case TOK_EQ:
                emit_cmp_reg_reg(ctx, RAX, RCX);
                emit_sete(ctx, RAX);
                emit_movzx_reg8(ctx, RAX, RAX);
                break;
            case TOK_NEQ:
                emit_cmp_reg_reg(ctx, RAX, RCX);
                emit_setne(ctx, RAX);
                emit_movzx_reg8(ctx, RAX, RAX);
                break;
            case TOK_LT:
                emit_cmp_reg_reg(ctx, RAX, RCX);
                emit_setl(ctx, RAX);
                emit_movzx_reg8(ctx, RAX, RAX);
                break;
            case TOK_LTE:
                emit_cmp_reg_reg(ctx, RAX, RCX);
                emit_setle(ctx, RAX);
                emit_movzx_reg8(ctx, RAX, RAX);
                break;
            case TOK_GT:
                emit_cmp_reg_reg(ctx, RAX, RCX);
                emit_setg(ctx, RAX);
                emit_movzx_reg8(ctx, RAX, RAX);
                break;
            case TOK_GTE:
                emit_cmp_reg_reg(ctx, RAX, RCX);
                emit_setge(ctx, RAX);
                emit_movzx_reg8(ctx, RAX, RAX);
                break;
            default:
                cg_error(ctx, n->loc, "unsupported binary operator");
                break;
        }
        break;
    }

    /* ── Field access: a.b ── */
    case AST_FIELD_ACCESS: {
        /* Load object base pointer into RAX */
        gen_expr(ctx, n->as.field.object);
        /* Compute field offset from struct definition */
        const char* stype = "";
        if (n->as.field.object->kind == AST_IDENT) {
            stype = var_struct_type(n->as.field.object->as.ident.name);
        }
        int off = field_offset(stype, n->as.field.field);
        /* Load the field: RAX = [RAX + off] */
        emit_mov_reg_mem(ctx, RAX, RAX, off);
        break;
    }

    /* ── Function call ── */
    case AST_CALL:
    case AST_METHOD_CALL: {
        ASTNode* callee = n->as.call.callee;
        size_t   argc   = n->as.call.arg_count;
        ASTNode** args  = n->as.call.args;

        /* Determine the function name */
        char fn_name[256] = {0};
        if (callee->kind == AST_IDENT) {
            strncpy(fn_name, callee->as.ident.name, sizeof(fn_name) - 1);
        } else if (callee->kind == AST_FIELD_ACCESS) {
            /* module.function or object.method */
            char obj_name[128] = {0};
            if (callee->as.field.object->kind == AST_IDENT)
                strncpy(obj_name, callee->as.field.object->as.ident.name, sizeof(obj_name) - 1);
            snprintf(fn_name, sizeof(fn_name), "%s.%s", obj_name, callee->as.field.field);
        }

        /* Evaluate arguments and push onto stack (right-to-left for safety) */
        /* Then move into argument registers (left-to-right per SysV ABI) */

        /* Push args in reverse order */
        for (int i = (int)argc - 1; i >= 0; i--) {
            gen_expr(ctx, args[i]);
            emit_push(ctx, RAX);
        }

        /* Pop into argument registers */
        const Reg arg_regs[] = { RDI, RSI, RDX, RCX, R8, R9 };
        size_t reg_args = argc < 6 ? argc : 6;
        for (size_t i = 0; i < reg_args; i++) {
            emit_pop(ctx, arg_regs[i]);
        }

        /* Stack alignment: System V AMD64 ABI requires RSP to be 16-byte aligned
           before any CALL instruction. Rather than trying to guess the drift
           from nested calls and loop iterations, we use the guaranteed approach:
             1. Save RSP in R11 (caller-saved, safe to clobber across a call)
             2. AND RSP, -16  (force alignment down to 16-byte boundary)
             3. Make the call  (CALL pushes return addr: RSP -= 8, unaligned inside callee)
             4. Restore RSP from R11 (undoes the AND and any arg-stack residue)
           This is identical to what clang/GCC emit for calls in leaf functions. */
        /* Stack is 16-byte aligned at all call sites because:
           prologue = push rbp + sub rsp,128 + push rbx + push r12
           = -152 bytes; incoming RSP%16==8 → after prologue RSP%16==0.
           All arg push/pop pairs are symmetric (net 0 RSP change). */

        /* Map C-Prime module.func calls to libc */
        const char* libc_name = NULL;
        if (strcmp(fn_name, "io.println") == 0 || strcmp(fn_name, "io.print") == 0) {
            /* puts(s) */
            libc_name = "puts";
        } else if (strcmp(fn_name, "io.printf") == 0 || strcmp(fn_name, "io.eprintf") == 0) {
            libc_name = "printf";
        } else if (strcmp(fn_name, "io.eprintln") == 0) {
            libc_name = "puts";
        } else if (strcmp(fn_name, "mem.alloc") == 0) {
            libc_name = "calloc";
            /* calloc(1, n) — we need to shift args */
        } else if (strcmp(fn_name, "mem.free") == 0) {
            libc_name = "free";
        } else if (strcmp(fn_name, "os.exit") == 0) {
            libc_name = "exit";
        } else if (strcmp(fn_name, "string.eq") == 0) {
            /* strcmp(a,b) == 0 */
            emit_call_extern(ctx, "strcmp");
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_sete(ctx, RAX);
            emit_movzx_reg8(ctx, RAX, RAX);
            break;
        } else if (strcmp(fn_name, "string.is_empty") == 0) {
            /* check if strlen(s) == 0 */
            emit_call_extern(ctx, "strlen");
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_sete(ctx, RAX);
            emit_movzx_reg8(ctx, RAX, RAX);
            break;
        } else if (strcmp(fn_name, "string.len") == 0) {
            libc_name = "strlen";
        } else if (strcmp(fn_name, "fmt.sprintf") == 0) {
            libc_name = "sprintf";
        } else if (strcmp(fn_name, "os.read_file")           == 0 ||
                   strcmp(fn_name, "os.read_file_or_empty") == 0) {
            libc_name = "__cprime_read_file_or_empty";
        } else if (strcmp(fn_name, "os.get_args")  == 0) {
            libc_name = "__cprime_get_args";
        } else if (strcmp(fn_name, "os.get_argc")  == 0) {
            libc_name = "__cprime_get_argc";
        } else if (strcmp(fn_name, "os.file_exists") == 0) {
            libc_name = "__cprime_file_exists";
        } else if (strcmp(fn_name, "string.starts_with") == 0) {
            /* strncmp(s, prefix, strlen(prefix)) == 0 */
            libc_name = "__cprime_starts_with";
        } else if (strcmp(fn_name, "string.ends_with") == 0) {
            libc_name = "__cprime_ends_with";
        } else if (strcmp(fn_name, "string.contains") == 0) {
            libc_name = "__cprime_str_contains";
        } else if (strcmp(fn_name, "string.concat") == 0) {
            libc_name = "__cprime_concat";
        } else if (strcmp(fn_name, "string.slice") == 0) {
            libc_name = "__cprime_slice";
        } else if (strcmp(fn_name, "string.to_lower") == 0) {
            libc_name = "__cprime_to_lower";
        } else if (strcmp(fn_name, "fmt.sprintf") == 0 ||
                   strcmp(fn_name, "io.sprintf")  == 0) {
            libc_name = "sprintf";
        } else if (strcmp(fn_name, "io.flush_stdout") == 0 ||
                   strcmp(fn_name, "io.flush")        == 0) {
            /* fflush(stdout) */
            libc_name = "__cprime_flush";
        } else if (strcmp(fn_name, "token_stream_dump") == 0 ||
                   strcmp(fn_name, "ast_dump")           == 0 ||
                   strcmp(fn_name, "optimize_ast")       == 0) {
            libc_name = "__cprime_noop";
        } else if (strcmp(fn_name, "lex_source")         == 0 ||
                   strcmp(fn_name, "parse_tokens")        == 0 ||
                   strcmp(fn_name, "semantic_analyze")    == 0 ||
                   strcmp(fn_name, "codegen_emit")        == 0) {
            /* These are the full compiler pipeline — stubs for bootstrap */
            libc_name = "__cprime_not_impl";
        } else {
            /* Try direct call to user-defined function */
            /* Look up in label table */
            size_t lbl = SIZE_MAX;
            for (size_t li = 0; li < ctx->label_count; li++) {
                if (strcmp(ctx->labels[li].name, fn_name) == 0) {
                    lbl = li; break;
                }
            }
            if (lbl != SIZE_MAX) {
                emit_call_label(ctx, lbl);
            } else {
                /* Unknown function — emit as extern */
                emit_call_extern(ctx, fn_name);
            }
            break;
        }

        if (libc_name) emit_call_extern(ctx, libc_name);
        break;
    }

    /* ── Some/Ok/Err: just pass through the inner value ── */
    case AST_SOME:
    case AST_OK:
        gen_expr(ctx, n->as.wrap.inner);
        break;
    case AST_ERR:
        /* For bootstrap: Err just holds a string, we negate RAX as sentinel */
        gen_expr(ctx, n->as.wrap.inner);
        emit_neg_reg(ctx, RAX); /* negative value signals error */
        break;

    /* ── Cast: ignore for bootstrap (treat as no-op) ── */
    case AST_CAST:
        gen_expr(ctx, n->as.cast.expr);
        break;

    /* ── Index: array[i] — load [RAX + RCX*8] ── */
    case AST_INDEX:
        gen_expr(ctx, n->as.index.index);
        emit_push(ctx, RAX);
        gen_expr(ctx, n->as.index.array);
        emit_pop(ctx, RCX);
        /* RAX = base, RCX = index — compute [RAX + RCX*8] */
        /* mul index by 8 */
        emit_mov_reg_imm64(ctx, RDX, 8);
        emit_imul_reg_reg(ctx, RCX, RDX);
        emit_add_reg_reg(ctx, RAX, RCX);
        emit_mov_reg_mem(ctx, RAX, RAX, 0);
        break;

    /* ── If expression ── */
    case AST_IF: {
        size_t else_lbl = new_label(ctx, ".if_else");
        size_t end_lbl  = new_label(ctx, ".if_end");
        gen_expr(ctx, n->as.if_stmt.cond);
        emit_cmp_reg_imm32(ctx, RAX, 0);
        emit_je(ctx, else_lbl);
        gen_stmt(ctx, n->as.if_stmt.then_block);
        emit_jmp(ctx, end_lbl);
        bind_label(ctx, else_lbl);
        if (n->as.if_stmt.else_block) gen_stmt(ctx, n->as.if_stmt.else_block);
        bind_label(ctx, end_lbl);
        break;
    }

    /* ── Struct initializer: allocate at fixed RBP-relative offsets ── */
    /* We allocate each field as a named local so the struct lives permanently
       in the frame (not on the RSP-scratch area). This prevents subsequent
       function calls from clobbering the struct data.                       */
    case AST_STRUCT_INIT: {
        size_t nfields = n->as.struct_init.field_count;

        if (nfields == 0) {
            emit_mov_reg_reg(ctx, RAX, RBP);
            break;
        }

        /* Allocate nfields consecutive stack slots going DOWNWARD from RBP.
           Fields must be in ASCENDING address order so that field i is at
           struct_ptr + i*8 (matching field_offset() in gen_expr).
           
           With stack growing downward:
             field 0 → deepest slot  (most negative RBP offset, allocated last)
             field N-1 → shallowest  (least negative, allocated first, nearest RBP)
           
           After pre-allocating all slots:
             local_offset = L_before - nfields*8
             field i stored at RBP + (local_offset + 8 + i*8)
             struct_ptr = &field[0] = RBP + (local_offset + 8)
        */

        /* Pre-allocate all N field slots going DOWNWARD from current local_offset.
           struct_base = local_offset after allocation = most negative offset.
           field[i] is at [RBP + struct_base + i*8].
           For N=11 fields starting at local_offset=-8:
             struct_base = -8 - 11*8 = -96
             field[0] at [RBP-96], field[10] at [RBP-96+80]=[RBP-16]
             All safely below RBP (within the 2048-byte frame). */
        for (size_t fi = 0; fi < nfields; fi++) {
            ctx->local_offset -= 8;
        }
        int struct_base = ctx->local_offset; /* lowest addr = field[0] */

        /* Fill each field: field[i] at [RBP + struct_base + i*8] */
        for (size_t fi = 0; fi < nfields; fi++) {
            gen_expr(ctx, n->as.struct_init.field_values[fi]);
            emit_mov_mem_reg(ctx, RBP, struct_base + (int)(fi * 8), RAX);
        }

        /* RAX = struct pointer = RBP + struct_base */
        emit_mov_reg_reg(ctx, RAX, RBP);
        emit_add_reg_imm32(ctx, RAX, struct_base);
        break;
    }

    default:
        /* For bootstrap: silently skip unsupported nodes instead of erroring */
        emit_xor_reg_reg(ctx, RAX, RAX);
        break;
    }
}

/* ─── Statement Code Generation ───────────────────────────────────────────── */
static void gen_block(CodegenContext* ctx, ASTNode* block) {
    if (!block) return;
    scope_push(ctx);
    for (size_t i = 0; i < block->as.list.count; i++)
        gen_stmt(ctx, block->as.list.items[i]);
    scope_pop(ctx);
}

static void gen_stmt(CodegenContext* ctx, ASTNode* n) {
    if (!n) return;

    switch (n->kind) {

    case AST_BLOCK:
    case AST_UNSAFE_BLOCK:
        gen_block(ctx, n);
        break;

    /* ── Variable declaration ── */
    case AST_VAR_DECL: {
        Symbol* sym = sym_define(ctx, n->as.var_decl.name, SYM_LOCAL);
        /* Record struct type for field access (so p.name → [p_ptr + offset]) */
        if (n->as.var_decl.type &&
            n->as.var_decl.type->kind == AST_TYPE_NAMED) {
            record_var_struct(n->as.var_decl.name,
                              n->as.var_decl.type->as.type.name);
        }
        if (n->as.var_decl.init) {
            gen_expr(ctx, n->as.var_decl.init);
            emit_mov_mem_reg(ctx, RBP, sym->stack_offset, RAX);
        } else {
            /* zero-initialize */
            emit_xor_reg_reg(ctx, RAX, RAX);
            emit_mov_mem_reg(ctx, RBP, sym->stack_offset, RAX);
        }
        break;
    }

    /* ── Assignment ── */
    case AST_ASSIGN: {
        gen_expr(ctx, n->as.assign.rhs);
        /* Store to LHS */
        ASTNode* lhs = n->as.assign.lhs;
        if (lhs->kind == AST_IDENT) {
            Symbol* sym = sym_lookup(ctx, lhs->as.ident.name);
            if (!sym) { cg_error(ctx, lhs->loc, "undefined variable in assignment"); break; }
            if (n->as.assign.op != TOK_ASSIGN) {
                /* Compound: load old, apply op, store */
                emit_push(ctx, RAX);
                emit_mov_reg_mem(ctx, RCX, RBP, sym->stack_offset);
                emit_pop(ctx, RAX);
                switch (n->as.assign.op) {
                    case TOK_PLUS_EQ:  emit_add_reg_reg(ctx, RCX, RAX); break;
                    case TOK_MINUS_EQ: emit_sub_reg_reg(ctx, RCX, RAX); break;
                    case TOK_STAR_EQ:  emit_imul_reg_reg(ctx, RCX, RAX); break;
                    default: break;
                }
                emit_mov_reg_reg(ctx, RAX, RCX);
            }
            emit_mov_mem_reg(ctx, RBP, sym->stack_offset, RAX);

        } else if (lhs->kind == AST_FIELD_ACCESS) {
            /*
             * obj.field = val
             *
             * This handles two cases:
             *
             * Case A — local struct value (e.g. `Options opts = {...}; opts.help = true`):
             *   `opts` is a SYM_LOCAL whose value IS the struct pointer (we store
             *   the stack address of the struct in the slot).
             *
             * Case B — borrow param (e.g. `fn f(\`mut Options opts) { opts.help = true }`):
             *   `opts` is a SYM_PARAM whose slot holds a POINTER to the caller's struct.
             *   Loading the slot gives us the address; then [addr + offset] is the field.
             *
             * In both cases:
             *   1. RAX = rhs value  (already computed above)
             *   2. Push RAX
             *   3. gen_expr(object) → RAX = address of struct base
             *   4. Pop RCX (rhs)
             *   5. [RAX + field_offset] = RCX
             */
            emit_push(ctx, RAX);                      /* save rhs value          */
            gen_expr(ctx, lhs->as.field.object);       /* RAX = struct base addr  */
            emit_pop(ctx, RCX);                        /* RCX = rhs value         */

            /* Resolve field offset using struct registry */
            const char* stype = "";
            if (lhs->as.field.object->kind == AST_IDENT) {
                stype = var_struct_type(lhs->as.field.object->as.ident.name);
            }
            int off = field_offset(stype, lhs->as.field.field);
            emit_mov_mem_reg(ctx, RAX, off, RCX);      /* [RAX + off] = RCX       */

        } else if (lhs->kind == AST_DEREF) {
            /* *ptr = val */
            emit_push(ctx, RAX);
            gen_expr(ctx, lhs->as.unary.operand);
            emit_pop(ctx, RCX);
            emit_mov_mem_reg(ctx, RAX, 0, RCX);
        }
        break;
    }

    /* ── Return ── */
    case AST_RETURN:
        if (n->as.ret.value) gen_expr(ctx, n->as.ret.value);
        else emit_xor_reg_reg(ctx, RAX, RAX);
        emit_jmp(ctx, ctx->fn_epilog_label);
        break;

    /* ── If statement ── */
    case AST_IF: {
        size_t else_lbl = new_label(ctx, ".if_else");
        size_t end_lbl  = new_label(ctx, ".if_end");
        gen_expr(ctx, n->as.if_stmt.cond);
        emit_cmp_reg_imm32(ctx, RAX, 0);
        emit_je(ctx, else_lbl);
        gen_block(ctx, n->as.if_stmt.then_block);
        emit_jmp(ctx, end_lbl);
        bind_label(ctx, else_lbl);
        if (n->as.if_stmt.else_block) {
            if (n->as.if_stmt.else_block->kind == AST_IF)
                gen_stmt(ctx, n->as.if_stmt.else_block);
            else
                gen_block(ctx, n->as.if_stmt.else_block);
        }
        bind_label(ctx, end_lbl);
        break;
    }

    /* ── While loop ── */
    case AST_WHILE: {
        size_t loop_lbl = new_label(ctx, ".while_top");
        size_t end_lbl  = new_label(ctx, ".while_end");
        size_t prev_break    = ctx->break_label;
        size_t prev_continue = ctx->continue_label;
        ctx->break_label    = end_lbl;
        ctx->continue_label = loop_lbl;

        bind_label(ctx, loop_lbl);
        gen_expr(ctx, n->as.while_loop.cond);
        emit_cmp_reg_imm32(ctx, RAX, 0);
        emit_je(ctx, end_lbl);
        gen_block(ctx, n->as.while_loop.body);
        emit_jmp(ctx, loop_lbl);
        bind_label(ctx, end_lbl);

        ctx->break_label    = prev_break;
        ctx->continue_label = prev_continue;
        break;
    }

    /* ── For range loop: for i in lo..hi ── */
    case AST_FOR_RANGE: {
        size_t loop_lbl = new_label(ctx, ".for_top");
        size_t end_lbl  = new_label(ctx, ".for_end");

        scope_push(ctx);
        Symbol* var = sym_define(ctx, n->as.for_range.var, SYM_LOCAL);

        /* var = lo */
        gen_expr(ctx, n->as.for_range.start);
        emit_mov_mem_reg(ctx, RBP, var->stack_offset, RAX);

        bind_label(ctx, loop_lbl);

        /* cond: var < hi */
        if (n->as.for_range.end) {
            emit_mov_reg_mem(ctx, RAX, RBP, var->stack_offset);
            emit_push(ctx, RAX);
            gen_expr(ctx, n->as.for_range.end);
            emit_mov_reg_reg(ctx, RCX, RAX);
            emit_pop(ctx, RAX);
            emit_cmp_reg_reg(ctx, RAX, RCX);
            emit_setl(ctx, RAX); /* setl al */
            emit_movzx_reg8(ctx, RAX, RAX);
            emit_cmp_reg_imm32(ctx, RAX, 0);
            emit_je(ctx, end_lbl);
        }

        gen_block(ctx, n->as.for_range.body);

        /* var++ */
        emit_mov_reg_mem(ctx, RAX, RBP, var->stack_offset);
        emit_add_reg_imm32(ctx, RAX, 1);
        emit_mov_mem_reg(ctx, RBP, var->stack_offset, RAX);
        emit_jmp(ctx, loop_lbl);

        bind_label(ctx, end_lbl);
        scope_pop(ctx);
        break;
    }

    /* ── Match statement ── */
    case AST_MATCH: {
        /* Evaluate subject into a temp stack slot */
        gen_expr(ctx, n->as.match.subject);
        /* For bootstrap: simple equality-based dispatch */
        int subject_offset = ctx->local_offset - 8;
        ctx->local_offset -= 8;
        emit_mov_mem_reg(ctx, RBP, subject_offset, RAX);

        size_t end_lbl = new_label(ctx, ".match_end");

        for (size_t i = 0; i < n->as.match.arm_count; i++) {
            ASTNode* arm = n->as.match.arms[i];
            size_t next_lbl = new_label(ctx, ".match_next");

            ASTNode* pat = arm->as.arm.pattern;

            /* Wildcard _ : always matches */
            if (pat->kind == AST_WILDCARD) {
                gen_stmt(ctx, arm->as.arm.body);
                emit_jmp(ctx, end_lbl);
                bind_label(ctx, next_lbl);
                break;
            }

            /* Ok/Some: check if value > 0 (positive = ok in our sentinel scheme) */
            if (pat->kind == AST_OK || pat->kind == AST_SOME) {
                emit_mov_reg_mem(ctx, RAX, RBP, subject_offset);
                emit_cmp_reg_imm32(ctx, RAX, 0);
                emit_jz(ctx, next_lbl);
                /* Bind inner variable if present */
                if (arm->as.arm.bind[0]) {
                    scope_push(ctx);
                    Symbol* bind = sym_define(ctx, arm->as.arm.bind, SYM_LOCAL);
                    emit_mov_mem_reg(ctx, RBP, bind->stack_offset, RAX);
                }
                gen_stmt(ctx, arm->as.arm.body);
                if (arm->as.arm.bind[0]) scope_pop(ctx);
                emit_jmp(ctx, end_lbl);
                bind_label(ctx, next_lbl);
                continue;
            }

            /* Err: check if value < 0 */
            if (pat->kind == AST_ERR) {
                emit_mov_reg_mem(ctx, RAX, RBP, subject_offset);
                emit_cmp_reg_imm32(ctx, RAX, 0);
                emit_setge(ctx, RCX);
                emit_movzx_reg8(ctx, RCX, RCX);
                emit_test_reg_reg(ctx, RCX, RCX);
                emit_jne(ctx, next_lbl);
                if (arm->as.arm.bind[0]) {
                    scope_push(ctx);
                    Symbol* bind = sym_define(ctx, arm->as.arm.bind, SYM_LOCAL);
                    emit_neg_reg(ctx, RAX); /* un-negate to get the error value */
                    emit_mov_mem_reg(ctx, RBP, bind->stack_offset, RAX);
                }
                gen_stmt(ctx, arm->as.arm.body);
                if (arm->as.arm.bind[0]) scope_pop(ctx);
                emit_jmp(ctx, end_lbl);
                bind_label(ctx, next_lbl);
                continue;
            }

            /* None: check if value == 0 */
            if (pat->kind == AST_NONE_LIT) {
                emit_mov_reg_mem(ctx, RAX, RBP, subject_offset);
                emit_cmp_reg_imm32(ctx, RAX, 0);
                emit_jne(ctx, next_lbl);
                gen_stmt(ctx, arm->as.arm.body);
                emit_jmp(ctx, end_lbl);
                bind_label(ctx, next_lbl);
                continue;
            }

            /* Literal / expression match: eval pattern, compare */
            emit_mov_reg_mem(ctx, RCX, RBP, subject_offset);
            emit_push(ctx, RCX);
            gen_expr(ctx, pat);
            emit_pop(ctx, RCX);
            emit_cmp_reg_reg(ctx, RAX, RCX);
            emit_jne(ctx, next_lbl);
            gen_stmt(ctx, arm->as.arm.body);
            emit_jmp(ctx, end_lbl);
            bind_label(ctx, next_lbl);
        }

        bind_label(ctx, end_lbl);
        ctx->local_offset += 8; /* free subject slot */
        break;
    }

    /* ── Expression statement ── */
    case AST_EXPR_STMT:
        gen_expr(ctx, n->as.unary.operand);
        break;

    default:
        /* Try as expression */
        gen_expr(ctx, n);
        break;
    }
}

/* ─── Function Code Generation ────────────────────────────────────────────── */
static void gen_fn(CodegenContext* ctx, ASTNode* fn) {
    char fn_label[256];
    if (fn->as.fn_def.is_method) {
        snprintf(fn_label, sizeof(fn_label), "%s.%s",
                 fn->as.fn_def.type_name, fn->as.fn_def.name);
    } else {
        /* Rename user 'main' -> 'cprime_main' to avoid clash with C wrapper */
        if (strcmp(fn->as.fn_def.name, "main") == 0) {
            strncpy(fn_label, "cprime_main", sizeof(fn_label) - 1);
        } else {
            strncpy(fn_label, fn->as.fn_def.name, sizeof(fn_label) - 1);
        }
    }

    /* Create label for this function */
    size_t lbl = new_label(ctx, fn_label);
    ctx->labels[lbl].is_global = true;
    bind_label(ctx, lbl);

    /* Set up function context */
    strncpy(ctx->current_fn, fn_label, sizeof(ctx->current_fn) - 1);
    /* Unique epilog label per function to avoid assembler duplicate-label errors */
    {
        char epilog_name[256];
        snprintf(epilog_name, sizeof(epilog_name), ".%s_epilog", fn_label);
        ctx->fn_epilog_label = new_label(ctx, epilog_name);
    }
    ctx->local_offset    = 0;

    /* First pass: count locals for frame size estimation.
       We reserve 128 bytes initially; locals grow downward. */
    /* Large frame: 2048 bytes accommodates structs, many locals, and loop vars.
       Each struct field takes one slot, plus all local variables. */
    int estimated_frame = 2048;

    emit_prologue(ctx, estimated_frame);

    /* Save callee-saved registers (RBX, R12-R15) */
    /* Push exactly 2 callee-saved registers so that after prologue:
       push rbp (-8) + sub rsp,128 (-128) + push rbx (-8) + push r12 (-8)
       = -152 bytes from original RSP. Since incoming RSP%16==8 (post-CALL),
       after push rbp: RSP%16==0, after sub+2 pushes: RSP%16==0. ALIGNED. */
    emit_push(ctx, RBX);
    emit_push(ctx, R12);

    /* Push parameters: move from arg regs to stack */
    scope_push(ctx);
    for (size_t i = 0; i < fn->as.fn_def.param_count; i++) {
        ASTNode* param = fn->as.fn_def.param_list[i];
        Symbol* sym = sym_define(ctx, param->as.var_decl.name, SYM_PARAM);
        if (i < 6) {
            emit_mov_mem_reg(ctx, RBP, sym->stack_offset, ARG_REGS[i]);
        }
        /* Record struct type for this param (handles `Person p and Person p) */
        ASTNode* ptype = param->as.var_decl.type;
        if (ptype) {
            /* Unwrap borrow: `Person → inner = Person */
            if ((ptype->kind == AST_TYPE_REF || ptype->kind == AST_TYPE_REF_MUT) &&
                ptype->as.type.inner &&
                ptype->as.type.inner->kind == AST_TYPE_NAMED) {
                record_var_struct(param->as.var_decl.name,
                                  ptype->as.type.inner->as.type.name);
            } else if (ptype->kind == AST_TYPE_NAMED) {
                record_var_struct(param->as.var_decl.name,
                                  ptype->as.type.name);
            }
        }
    }

    /* Generate function body */
    if (fn->as.fn_def.body) {
        for (size_t i = 0; i < fn->as.fn_def.body->as.list.count; i++)
            gen_stmt(ctx, fn->as.fn_def.body->as.list.items[i]);
    }

    /* Epilogue */
    bind_label(ctx, ctx->fn_epilog_label);
    scope_pop(ctx);

    /* Restore callee-saved registers */
    /* Restore the 2 callee-saved registers in reverse order */
    emit_pop(ctx, R12);
    emit_pop(ctx, RBX);

    emit_epilogue(ctx);
}

/* ─── Context Create / Free ───────────────────────────────────────────────── */
CodegenContext* codegen_create(const char* input, const char* output) {
    CodegenContext* ctx = calloc(1, sizeof(CodegenContext));
    if (!ctx) return NULL;

    ctx->input_filename  = input;
    ctx->output_filename = output;
    ctx->scope_stack[0]  = scope_new();  /* global scope */
    ctx->scope_depth     = 0;
    ctx->label_counter   = 0;

    return ctx;
}

void codegen_free(CodegenContext* ctx) {
    if (!ctx) return;
    for (int i = 0; i <= ctx->scope_depth; i++) {
        if (!ctx->scope_stack[i]) continue;
        Symbol* s = ctx->scope_stack[i]->head;
        while (s) { Symbol* n = s->next; free(s); s = n; }
        free(ctx->scope_stack[i]);
    }
    free(ctx->text.buf);
    free(ctx->rodata.buf);
    free(ctx->data.buf);
    free(ctx->labels);
    free(ctx->relocs);
    for (size_t i = 0; i < ctx->string_count; i++) free(ctx->strings[i].data);
    free(ctx->strings);
    for (size_t i = 0; i < ctx->external_count; i++) free((char*)ctx->externals[i]);
    free(ctx->externals);
    free(ctx->text_patches);
    free(ctx);
}

/* ─── Top-level emit ──────────────────────────────────────────────────────── */
int codegen_emit(CodegenContext* ctx, ASTNode* ast) {
    if (!ast || ast->kind != AST_PROGRAM) return 1;

    /* First pass: register all function names as labels (forward decls) */
    for (size_t i = 0; i < ast->as.list.count; i++) {
        ASTNode* item = ast->as.list.items[i];
        if (item->kind == AST_FN_DEF) {
            char lbl[256];
            if (item->as.fn_def.is_method)
                snprintf(lbl, sizeof(lbl), "%s.%s",
                         item->as.fn_def.type_name, item->as.fn_def.name);
            else
                strncpy(lbl, item->as.fn_def.name, sizeof(lbl) - 1);
            /* Pre-allocate label slot */
            new_label(ctx, lbl);
        }
    }

    /* Reset label count so gen_fn rebinds them */
    ctx->label_count = 0;

    /* Second pass: emit code */
    for (size_t i = 0; i < ast->as.list.count; i++) {
        ASTNode* item = ast->as.list.items[i];
        switch (item->kind) {
            case AST_FN_DEF:
                gen_fn(ctx, item);
                break;
            case AST_CONST_DEF: {
                /* For string consts: intern the string and create a symbol
                   that loads it via LEA when referenced */
                if (item->as.const_def.value &&
                    item->as.const_def.value->kind == AST_STRING_LIT) {
                    Symbol* s = sym_define(ctx, item->as.const_def.name, SYM_LOCAL);
                    /* Stash the string index in stack_offset as a sentinel:
                       use a large positive offset to distinguish from stack vars */
                    size_t idx = intern_string(ctx, item->as.const_def.value->as.lit.str_val);
                    s->stack_offset = (int)(0x70000000 + idx);
                    s->kind = SYM_CONST_STR;
                }
                break;
            }
            case AST_STRUCT_DEF:
                /* Register struct field layout for field-access codegen */
                register_struct(item);
                break;
            case AST_IMPORT:
                /* Imports handled by including stdlib .cp files */
                break;
            default:
                break;
        }
    }

    return ctx->had_error ? 1 : 0;
}
