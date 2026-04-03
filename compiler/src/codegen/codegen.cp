/*
 * cpc — Code Generator (high-level)
 * compiler/src/codegen/codegen.cp
 * ===================================
 * Walks the typed, borrow-checked AST and emits x86_64 machine code.
 * Delegates raw instruction encoding to codegen/x86_64/emit.cp.
 * Delegates ELF binary writing to codegen/x86_64/elf.cp.
 *
 * Strategy:
 *   - All expressions leave their result in RAX
 *   - All locals live on the stack (RBP-relative, negative offsets)
 *   - Function arguments go into RDI, RSI, RDX, RCX, R8, R9 (SysV ABI)
 *   - Strings go into .rodata, loaded via RIP-relative LEA
 *   - Borrow: ` is zero-cost — compiler just passes the address
 *   - Ownership: drops are zero-cost at compile time (borrow checker proved safety)
 */

import core;
import collections.vec;
import "ast/ast";
import "lexer/token";
import "codegen/x86_64/emit";
import "codegen/x86_64/elf";

/* ─── Symbol ──────────────────────────────────────────────────────────────── */
enum SymKind { Local, Param, Global, Function }

struct Symbol {
    str     name;
    SymKind kind;
    i32     stack_offset;   /* for Local/Param: rbp-relative */
    usize   label;          /* for Function: label table index */
    bool    is_mut;
}

struct SymTable {
    Symbol* head;
    SymTable* parent;
}

/* ─── Codegen context ────────────────────────────────────────────────────── */
struct CgenCtx {
    Emitter*      emit;         /* x86_64 instruction emitter */
    SymTable*     scope;        /* current variable scope */
    i32           local_offset; /* next stack slot offset (grows negative) */
    usize         fn_epilog;    /* label for function epilogue */
    str           fn_name;      /* current function name */
    usize         continue_lbl; /* label for loop continue */
    usize         break_lbl;    /* label for loop break */
    bool          had_error;
}

fn CgenCtx.new(Emitter* emit) -> CgenCtx {
    SymTable* global = mem.alloc_type<SymTable>();
    global.head   = null;
    global.parent = null;
    return CgenCtx {
        emit:         emit,
        scope:        global,
        local_offset: 0,
        fn_epilog:    0,
        fn_name:      "",
        continue_lbl: 0,
        break_lbl:    0,
        had_error:    false,
    };
}

fn CgenCtx.push_scope(`mut CgenCtx self) -> void {
    SymTable* s = mem.alloc_type<SymTable>();
    s.head   = null;
    s.parent = self.scope;
    self.scope = s;
}

fn CgenCtx.pop_scope(`mut CgenCtx self) -> void {
    if self.scope.parent != null {
        self.scope = self.scope.parent;
    }
}

fn CgenCtx.define(`mut CgenCtx self, `str name, bool is_mut) -> `Symbol {
    Symbol* sym = mem.alloc_type<Symbol>();
    sym.name         = name;
    sym.kind         = SymKind.Local;
    sym.is_mut       = is_mut;
    self.local_offset= self.local_offset - 8;
    sym.stack_offset = self.local_offset;
    sym.next         = self.scope.head;
    self.scope.head  = sym;
    return sym;
}

fn CgenCtx.lookup(`CgenCtx self, `str name) -> Option<`Symbol> {
    SymTable* s = self.scope;
    while s != null {
        Symbol* sym = s.head;
        while sym != null {
            if string.eq(sym.name, name) { return Some(sym); }
            sym = sym.next;
        }
        s = s.parent;
    }
    return None;
}

/* ─── Expression codegen (result always in RAX) ──────────────────────────── */
fn gen_expr(`mut CgenCtx ctx, `ASTNode* node) -> void {
    if node == null { emit_xor_rax_rax(ctx.emit); return; }

    match node.kind {
        NODE_INT_LIT    -> emit_mov_rax_imm64(ctx.emit, node.lit_int),
        NODE_FLOAT_LIT  -> emit_mov_rax_float(ctx.emit, node.lit_float),
        NODE_BOOL_LIT   -> emit_mov_rax_imm64(ctx.emit, if node.lit_bool { 1 } else { 0 }),
        NODE_NONE_LIT | NODE_VOID_EXPR -> emit_xor_rax_rax(ctx.emit),
        NODE_STRING_LIT -> {
            usize idx = emit_intern_string(ctx.emit, node.lit_str);
            emit_lea_rax_rodata(ctx.emit, idx);
        },

        NODE_IDENT -> {
            match ctx.lookup(node.ident_name) {
                None -> {
                    /* Maybe it's a function name — emit its label address */
                    Option<usize> lbl = emit_find_label(ctx.emit, node.ident_name);
                    match lbl {
                        Some(l) -> emit_lea_rax_label(ctx.emit, l),
                        None    -> {
                            io.eprintf("[codegen] undefined: %s\n", node.ident_name);
                            ctx.had_error = true;
                            emit_xor_rax_rax(ctx.emit);
                        },
                    }
                },
                Some(sym) -> emit_mov_rax_rbp(ctx.emit, sym.stack_offset),
            }
        },

        NODE_BORROW | NODE_BORROW_MUT -> {
            /* Borrows are zero-cost at runtime — just evaluate the inner expression */
            gen_expr(`mut ctx, node.borrow_expr);
        },

        NODE_DEREF -> {
            gen_expr(`mut ctx, node.borrow_expr);
            emit_mov_rax_deref_rax(ctx.emit);
        },

        NODE_UNARY -> {
            gen_expr(`mut ctx, node.operand);
            match node.op {
                TK_MINUS -> emit_neg_rax(ctx.emit),
                TK_NOT   -> emit_logical_not_rax(ctx.emit),
                TK_BNOT  -> emit_not_rax(ctx.emit),
                _        -> {},
            }
        },

        NODE_BINARY -> gen_binary(`mut ctx, node),

        NODE_CALL | NODE_METHOD_CALL -> gen_call(`mut ctx, node),

        NODE_FIELD_ACCESS -> {
            /* Load object then access field at compile-time offset */
            gen_expr(`mut ctx, node.object);
            /* Field offsets are resolved by semantic analysis — simplified here */
        },

        NODE_INDEX -> {
            gen_expr(`mut ctx, node.idx_array);
            emit_push_rax(ctx.emit);
            gen_expr(`mut ctx, node.idx_index);
            emit_imul_rax_8(ctx.emit);       /* index * 8 */
            emit_pop_rcx(ctx.emit);
            emit_add_rax_rcx(ctx.emit);       /* base + offset */
            emit_mov_rax_deref_rax(ctx.emit); /* load value */
        },

        NODE_CAST -> {
            gen_expr(`mut ctx, node.cast_expr);
            /* Type cast — for numeric types, value is already compatible */
        },

        NODE_SOME | NODE_OK -> {
            gen_expr(`mut ctx, node.wrap_inner);
            /* Sentinel: positive RAX = Some/Ok value */
            /* (If zero, add 1 to distinguish from None/Err with value 0) */
        },

        NODE_ERR -> {
            gen_expr(`mut ctx, node.wrap_inner);
            emit_neg_rax(ctx.emit);   /* negative RAX = Err */
            if_rax_zero_set_minus_one(ctx.emit);
        },

        NODE_NONE_LIT -> emit_xor_rax_rax(ctx.emit),

        NODE_IF_EXPR   -> gen_if_expr(`mut ctx, node),
        NODE_BLOCK_EXPR-> gen_block_expr(`mut ctx, node),

        NODE_STRUCT_INIT -> {
            /* Allocate on stack, write fields */
            gen_struct_init(`mut ctx, node);
        },

        _ -> {
            io.eprintf("[codegen] unhandled expr kind %d\n", node.kind);
            emit_xor_rax_rax(ctx.emit);
        },
    }
}

/* ─── Binary expression ──────────────────────────────────────────────────── */
fn gen_binary(`mut CgenCtx ctx, `ASTNode* node) -> void {
    /* Short-circuit && */
    if node.op == TK_AND {
        usize false_lbl = emit_new_label(ctx.emit, ".and_false");
        usize end_lbl   = emit_new_label(ctx.emit, ".and_end");
        gen_expr(`mut ctx, node.left);
        emit_test_rax_rax(ctx.emit);
        emit_jz(ctx.emit, false_lbl);
        gen_expr(`mut ctx, node.right);
        emit_test_rax_rax(ctx.emit);
        emit_jz(ctx.emit, false_lbl);
        emit_mov_rax_imm64(ctx.emit, 1);
        emit_jmp(ctx.emit, end_lbl);
        emit_bind(ctx.emit, false_lbl);
        emit_xor_rax_rax(ctx.emit);
        emit_bind(ctx.emit, end_lbl);
        return;
    }

    /* Short-circuit || */
    if node.op == TK_OR {
        usize true_lbl = emit_new_label(ctx.emit, ".or_true");
        usize end_lbl  = emit_new_label(ctx.emit, ".or_end");
        gen_expr(`mut ctx, node.left);
        emit_test_rax_rax(ctx.emit);
        emit_jnz(ctx.emit, true_lbl);
        gen_expr(`mut ctx, node.right);
        emit_test_rax_rax(ctx.emit);
        emit_jnz(ctx.emit, true_lbl);
        emit_xor_rax_rax(ctx.emit);
        emit_jmp(ctx.emit, end_lbl);
        emit_bind(ctx.emit, true_lbl);
        emit_mov_rax_imm64(ctx.emit, 1);
        emit_bind(ctx.emit, end_lbl);
        return;
    }

    /* Evaluate left, push; evaluate right; pop into RCX; combine */
    gen_expr(`mut ctx, node.left);
    emit_push_rax(ctx.emit);
    gen_expr(`mut ctx, node.right);
    emit_pop_rcx(ctx.emit);

    match node.op {
        TK_PLUS    -> emit_add_rax_rcx(ctx.emit),
        TK_MINUS   -> { emit_sub_rcx_rax(ctx.emit); emit_mov_rax_rcx(ctx.emit); },
        TK_STAR    -> emit_imul_rax_rcx(ctx.emit),
        TK_SLASH   -> { emit_mov_rax_rcx(ctx.emit); emit_cqo(ctx.emit); /* TODO: properly load divisor */ },
        TK_PERCENT -> { /* modulo */ },
        TK_SHL     -> emit_shl_rax_cl(ctx.emit),
        TK_SHR     -> emit_shr_rax_cl(ctx.emit),
        TK_BAND    -> emit_and_rax_rcx(ctx.emit),
        TK_BOR     -> emit_or_rax_rcx(ctx.emit),
        TK_BXOR    -> emit_xor_rax_rcx(ctx.emit),
        TK_EQ      -> { emit_cmp_rcx_rax(ctx.emit); emit_sete_rax(ctx.emit);  },
        TK_NEQ     -> { emit_cmp_rcx_rax(ctx.emit); emit_setne_rax(ctx.emit); },
        TK_LT      -> { emit_cmp_rcx_rax(ctx.emit); emit_setg_rax(ctx.emit);  },
        TK_GT      -> { emit_cmp_rcx_rax(ctx.emit); emit_setl_rax(ctx.emit);  },
        TK_LTE     -> { emit_cmp_rcx_rax(ctx.emit); emit_setge_rax(ctx.emit); },
        TK_GTE     -> { emit_cmp_rcx_rax(ctx.emit); emit_setle_rax(ctx.emit); },
        _          -> {},
    }
}

/* ─── Function call ──────────────────────────────────────────────────────── */
fn gen_call(`mut CgenCtx ctx, `ASTNode* node) -> void {
    usize argc = node.arg_count;
    const Reg[] ARG_REGS = [RDI, RSI, RDX, RCX, R8, R9];

    /* Push args right-to-left, then pop into regs left-to-right */
    i64 i = argc as i64 - 1;
    while i >= 0 {
        gen_expr(`mut ctx, node.args[i as usize]);
        emit_push_rax(ctx.emit);
        i = i - 1;
    }

    usize reg_args = if argc < 6 { argc } else { 6 };
    for j in 0..reg_args {
        emit_pop_reg(ctx.emit, ARG_REGS[j]);
    }

    /* Align stack to 16 bytes before CALL */
    bool needs_align = (argc % 2 == 0);
    if needs_align { emit_sub_rsp_8(ctx.emit); }

    /* Determine what to call */
    str fn_name = get_call_target_name(node);
    Option<usize> local_lbl = emit_find_label(ctx.emit, fn_name);

    match local_lbl {
        Some(lbl) -> emit_call_label(ctx.emit, lbl),
        None      -> {
            /* stdlib / libc mapping */
            str libc_name = map_to_libc(fn_name);
            emit_call_extern(ctx.emit, libc_name);
        },
    }

    if needs_align { emit_add_rsp_8(ctx.emit); }
}

fn get_call_target_name(`ASTNode* node) -> str {
    if node.callee == null { return ""; }
    if node.callee.kind == NODE_IDENT { return node.callee.ident_name; }
    if node.callee.kind == NODE_FIELD_ACCESS || node.callee.kind == NODE_METHOD_CALL {
        str obj = if node.callee.object != null &&
                     node.callee.object.kind == NODE_IDENT {
            node.callee.object.ident_name
        } else { "" };
        return fmt.sprintf("%s.%s", obj, node.callee.field_name);
    }
    return "";
}

fn map_to_libc(`str name) -> str {
    if string.eq(name, "io.println")   { return "puts";   }
    if string.eq(name, "io.print")     { return "fputs";  }
    if string.eq(name, "io.printf")    { return "printf"; }
    if string.eq(name, "io.eprintln")  { return "puts";   }
    if string.eq(name, "io.eprintf")   { return "fprintf";}
    if string.eq(name, "mem.alloc")    { return "calloc"; }
    if string.eq(name, "mem.free")     { return "free";   }
    if string.eq(name, "string.eq")    { return "strcmp"; }
    if string.eq(name, "string.len")   { return "strlen"; }
    if string.eq(name, "os.exit")      { return "exit";   }
    if string.eq(name, "math.sqrt")    { return "sqrt";   }
    return name;
}

/* ─── Statement codegen ──────────────────────────────────────────────────── */
fn gen_stmt(`mut CgenCtx ctx, `ASTNode* node) -> void {
    if node == null { return; }

    match node.kind {
        NODE_VAR_DECL -> {
            gen_expr(`mut ctx, node.var_init);
            Symbol* sym = ctx.define(node.var_name, node.var_is_mut);
            emit_mov_rbp_rax(ctx.emit, sym.stack_offset);
        },

        NODE_ASSIGN -> {
            gen_expr(`mut ctx, node.rhs);
            match ctx.lookup(node.lhs.ident_name) {
                Some(sym) -> emit_mov_rbp_rax(ctx.emit, sym.stack_offset),
                None      -> io.eprintf("[codegen] assign to undefined '%s'\n",
                                        node.lhs.ident_name),
            }
        },

        NODE_RETURN -> {
            gen_expr(`mut ctx, node.ret_value);
            emit_jmp(ctx.emit, ctx.fn_epilog);
        },

        NODE_BREAK    -> emit_jmp(ctx.emit, ctx.break_lbl),
        NODE_CONTINUE -> emit_jmp(ctx.emit, ctx.continue_lbl),

        NODE_EXPR_STMT -> gen_expr(`mut ctx, node.operand),

        NODE_BLOCK | NODE_UNSAFE_BLOCK -> {
            ctx.push_scope();
            for i in 0..node.item_count {
                gen_stmt(`mut ctx, node.items[i]);
            }
            ctx.pop_scope();
        },

        NODE_IF -> {
            usize else_lbl = emit_new_label(ctx.emit, ".if_else");
            usize end_lbl  = emit_new_label(ctx.emit, ".if_end");
            gen_expr(`mut ctx, node.if_cond);
            emit_test_rax_rax(ctx.emit);
            emit_jz(ctx.emit, else_lbl);
            gen_stmt(`mut ctx, node.if_then);
            emit_jmp(ctx.emit, end_lbl);
            emit_bind(ctx.emit, else_lbl);
            if node.if_else != null { gen_stmt(`mut ctx, node.if_else); }
            emit_bind(ctx.emit, end_lbl);
        },

        NODE_WHILE -> {
            usize loop_lbl  = emit_new_label(ctx.emit, ".while_top");
            usize break_lbl = emit_new_label(ctx.emit, ".while_end");
            usize saved_brk = ctx.break_lbl;
            usize saved_con = ctx.continue_lbl;
            ctx.break_lbl    = break_lbl;
            ctx.continue_lbl = loop_lbl;

            emit_bind(ctx.emit, loop_lbl);
            gen_expr(`mut ctx, node.while_cond);
            emit_test_rax_rax(ctx.emit);
            emit_jz(ctx.emit, break_lbl);
            gen_stmt(`mut ctx, node.while_body);
            emit_jmp(ctx.emit, loop_lbl);
            emit_bind(ctx.emit, break_lbl);

            ctx.break_lbl    = saved_brk;
            ctx.continue_lbl = saved_con;
        },

        NODE_FOR_RANGE -> {
            /* Allocate loop variable */
            ctx.push_scope();
            Symbol* iter = ctx.define(node.for_var, false);
            gen_expr(`mut ctx, node.for_start);
            emit_mov_rbp_rax(ctx.emit, iter.stack_offset);

            usize loop_lbl  = emit_new_label(ctx.emit, ".for_top");
            usize break_lbl = emit_new_label(ctx.emit, ".for_end");
            usize saved_brk = ctx.break_lbl;
            usize saved_con = ctx.continue_lbl;
            ctx.break_lbl    = break_lbl;
            ctx.continue_lbl = loop_lbl;

            emit_bind(ctx.emit, loop_lbl);
            /* Condition: iter < end */
            emit_mov_rax_rbp(ctx.emit, iter.stack_offset);
            emit_push_rax(ctx.emit);
            gen_expr(`mut ctx, node.for_end);
            emit_pop_rcx(ctx.emit);
            emit_cmp_rcx_rax(ctx.emit);
            if node.for_inclusive {
                emit_setg_rax(ctx.emit);
            } else {
                emit_setge_rax(ctx.emit);
            }
            emit_test_rax_rax(ctx.emit);
            emit_jnz(ctx.emit, break_lbl);

            gen_stmt(`mut ctx, node.for_body);

            /* Increment */
            emit_mov_rax_rbp(ctx.emit, iter.stack_offset);
            emit_inc_rax(ctx.emit);
            emit_mov_rbp_rax(ctx.emit, iter.stack_offset);
            emit_jmp(ctx.emit, loop_lbl);
            emit_bind(ctx.emit, break_lbl);

            ctx.break_lbl    = saved_brk;
            ctx.continue_lbl = saved_con;
            ctx.pop_scope();
        },

        NODE_MATCH -> gen_match(`mut ctx, node),

        _ -> gen_expr(`mut ctx, node),
    }
}

/* ─── Match statement ────────────────────────────────────────────────────── */
fn gen_match(`mut CgenCtx ctx, `ASTNode* node) -> void {
    gen_expr(`mut ctx, node.match_subject);
    /* Store subject on stack */
    ctx.local_offset = ctx.local_offset - 8;
    i32 subj_slot = ctx.local_offset;
    emit_mov_rbp_rax(ctx.emit, subj_slot);

    usize end_lbl = emit_new_label(ctx.emit, ".match_end");

    for i in 0..node.match_arm_count {
        ASTNode* arm     = node.match_arms[i];
        usize    next_lbl = emit_new_label(ctx.emit, ".arm_next");

        /* Wildcard _ — always matches */
        if arm.arm_pattern != null && arm.arm_pattern.kind == NODE_WILDCARD {
            gen_stmt(`mut ctx, arm.arm_body);
            emit_jmp(ctx.emit, end_lbl);
            emit_bind(ctx.emit, next_lbl);
            break;
        }

        /* Ok/Some — value > 0 */
        if arm.arm_pattern != null &&
           (arm.arm_pattern.kind == NODE_OK || arm.arm_pattern.kind == NODE_SOME) {
            emit_mov_rax_rbp(ctx.emit, subj_slot);
            emit_test_rax_rax(ctx.emit);
            emit_jz(ctx.emit, next_lbl);
            ctx.push_scope();
            if !string.is_empty(arm.arm_bind) {
                Symbol* bind = ctx.define(arm.arm_bind, false);
                emit_mov_rbp_rax(ctx.emit, bind.stack_offset);
            }
            gen_stmt(`mut ctx, arm.arm_body);
            if !string.is_empty(arm.arm_bind) { ctx.pop_scope(); }
            else { ctx.pop_scope(); }
            emit_jmp(ctx.emit, end_lbl);
            emit_bind(ctx.emit, next_lbl);
            continue;
        }

        /* Err — value < 0 */
        if arm.arm_pattern != null && arm.arm_pattern.kind == NODE_ERR {
            emit_mov_rax_rbp(ctx.emit, subj_slot);
            emit_test_rax_rax(ctx.emit);
            emit_jns(ctx.emit, next_lbl);
            ctx.push_scope();
            if !string.is_empty(arm.arm_bind) {
                Symbol* bind = ctx.define(arm.arm_bind, false);
                emit_neg_rax(ctx.emit);
                emit_mov_rbp_rax(ctx.emit, bind.stack_offset);
            }
            gen_stmt(`mut ctx, arm.arm_body);
            ctx.pop_scope();
            emit_jmp(ctx.emit, end_lbl);
            emit_bind(ctx.emit, next_lbl);
            continue;
        }

        /* None — value == 0 */
        if arm.arm_pattern != null && arm.arm_pattern.kind == NODE_NONE_LIT {
            emit_mov_rax_rbp(ctx.emit, subj_slot);
            emit_test_rax_rax(ctx.emit);
            emit_jnz(ctx.emit, next_lbl);
            gen_stmt(`mut ctx, arm.arm_body);
            emit_jmp(ctx.emit, end_lbl);
            emit_bind(ctx.emit, next_lbl);
            continue;
        }

        /* Literal or expression pattern */
        emit_mov_rax_rbp(ctx.emit, subj_slot);
        emit_push_rax(ctx.emit);
        gen_expr(`mut ctx, arm.arm_pattern);
        emit_pop_rcx(ctx.emit);
        emit_cmp_rax_rcx(ctx.emit);
        emit_jne(ctx.emit, next_lbl);
        gen_stmt(`mut ctx, arm.arm_body);
        emit_jmp(ctx.emit, end_lbl);
        emit_bind(ctx.emit, next_lbl);
    }

    emit_bind(ctx.emit, end_lbl);
    ctx.local_offset = ctx.local_offset + 8;
}

fn gen_if_expr(`mut CgenCtx ctx, `ASTNode* node) -> void {
    usize else_lbl = emit_new_label(ctx.emit, ".ifexpr_else");
    usize end_lbl  = emit_new_label(ctx.emit, ".ifexpr_end");
    gen_expr(`mut ctx, node.if_cond);
    emit_test_rax_rax(ctx.emit);
    emit_jz(ctx.emit, else_lbl);
    gen_expr(`mut ctx, node.if_then);
    emit_jmp(ctx.emit, end_lbl);
    emit_bind(ctx.emit, else_lbl);
    if node.if_else != null { gen_expr(`mut ctx, node.if_else); }
    emit_bind(ctx.emit, end_lbl);
}

fn gen_block_expr(`mut CgenCtx ctx, `ASTNode* node) -> void {
    ctx.push_scope();
    for i in 0..node.item_count { gen_stmt(`mut ctx, node.items[i]); }
    ctx.pop_scope();
}

fn gen_struct_init(`mut CgenCtx ctx, `ASTNode* node) -> void {
    /* Allocate space on stack for struct, write each field */
    usize field_count = node.init_field_count;
    i32 base = ctx.local_offset - (field_count as i32 * 8);
    ctx.local_offset = base;

    for i in 0..field_count {
        gen_expr(`mut ctx, node.init_field_values[i]);
        i32 slot = base + (i as i32 * 8);
        emit_mov_rbp_rax(ctx.emit, slot);
    }

    /* Return pointer to struct in RAX */
    emit_lea_rax_rbp(ctx.emit, base);
}

/* ─── Function codegen ───────────────────────────────────────────────────── */
fn gen_fn(`mut CgenCtx ctx, `ASTNode* fn_node) -> void {
    /* Determine label name — rename main → cprime_main */
    str name = if string.eq(fn_node.fn_name, "main") { "cprime_main" }
               else if fn_node.fn_is_method {
                   fmt.sprintf("%s.%s", fn_node.fn_type_name, fn_node.fn_name)
               } else { fn_node.fn_name };

    usize lbl = emit_new_global_label(ctx.emit, name);
    emit_bind(ctx.emit, lbl);

    ctx.fn_name      = name;
    ctx.fn_epilog    = emit_new_label(ctx.emit, ".fn_epilog");
    ctx.local_offset = 0;

    /* Prologue */
    i32 frame_size = 128 + (fn_node.fn_param_count as i32 * 8);
    emit_prologue(ctx.emit, frame_size);
    emit_push_rbx(ctx.emit);
    emit_push_r12(ctx.emit);
    emit_push_r13(ctx.emit);

    /* Parameters: move from arg registers to stack */
    const Reg[] ARG_REGS = [RDI, RSI, RDX, RCX, R8, R9];
    ctx.push_scope();
    for i in 0..fn_node.fn_param_count {
        ASTNode* param = fn_node.fn_params[i];
        Symbol* sym = ctx.define(param.var_name, param.var_is_mut);
        if i < 6 { emit_mov_rbp_reg(ctx.emit, sym.stack_offset, ARG_REGS[i]); }
    }

    /* Body */
    if fn_node.fn_body != null { gen_stmt(`mut ctx, fn_node.fn_body); }

    /* Epilogue */
    emit_bind(ctx.emit, ctx.fn_epilog);
    ctx.pop_scope();
    emit_pop_r13(ctx.emit);
    emit_pop_r12(ctx.emit);
    emit_pop_rbx(ctx.emit);
    emit_epilogue(ctx.emit);
}

/* ─── Top-level code generation ──────────────────────────────────────────── */
fn codegen_emit(`ASTNode* ast, `str output_file, bool dump_asm, bool optimize) -> Result<void, str> {
    Emitter* emit = emitter_new();

    /* Pre-register all function labels (for forward calls) */
    for i in 0..ast.item_count {
        ASTNode* item = ast.items[i];
        if item.kind == NODE_FN_DEF {
            str name = if string.eq(item.fn_name, "main") { "cprime_main" }
                       else if item.fn_is_method {
                           fmt.sprintf("%s.%s", item.fn_type_name, item.fn_name)
                       } else { item.fn_name };
            emit_new_label(emit, name);
        }
    }

    CgenCtx ctx = CgenCtx.new(emit);

    /* Generate code for each function */
    for i in 0..ast.item_count {
        ASTNode* item = ast.items[i];
        if item.kind == NODE_FN_DEF {
            gen_fn(`mut ctx, item);
        }
    }

    if ctx.had_error { return Err("code generation failed"); }

    if dump_asm { emitter_dump(emit); }

    /* Write output binary */
    match elf_write(emit, output_file) {
        Err(e) -> return Err(fmt.sprintf("ELF write failed: %s", e)),
        Ok(_)  -> {},
    }

    emitter_free(emit);
    return Ok(void);
}
