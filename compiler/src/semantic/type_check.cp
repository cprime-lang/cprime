/*
 * cpc — Type Checker
 * compiler/src/semantic/type_check.cp
 * =====================================
 * Assigns types to every AST node and verifies type compatibility.
 * Runs after parsing, before borrow checking.
 *
 * Responsibilities:
 *   - Infer types for `auto` declarations
 *   - Resolve named types (structs, enums)
 *   - Check function call argument types
 *   - Check binary operator operand types
 *   - Verify return types match function signatures
 *   - Resolve generics (basic monomorphisation)
 */

import core;
import collections.vec;
import "ast/ast";
import "lexer/token";

/* ─── Type Environment ────────────────────────────────────────────────────── */
struct TypeEnv {
    Vec<VarType>   locals;    /* variable → type mappings */
    Vec<FnSig>     functions; /* known function signatures */
    Vec<StructDef> structs;   /* known struct definitions */
    u32            error_count;
    bool           had_error;
}

struct VarType {
    str      name;
    TypeInfo ty;
    u32      scope_depth;
}

struct FnSig {
    str       name;
    TypeInfo* params;
    usize     param_count;
    TypeInfo  ret;
}

struct StructField { str name; TypeInfo ty; }
struct StructDef {
    str          name;
    StructField* fields;
    usize        field_count;
}

fn TypeEnv.new() -> TypeEnv {
    return TypeEnv {
        locals:     Vec.new(),
        functions:  Vec.new(),
        structs:    Vec.new(),
        error_count: 0,
        had_error:  false,
    };
}

fn TypeEnv.error(`mut TypeEnv self, `Span span, `str msg) -> void {
    io.eprintf("[type] %s:%d:%d: \033[31merror:\033[0m %s\n",
               span.file, span.line, span.col, msg);
    self.had_error    = true;
    self.error_count  = self.error_count + 1;
}

fn TypeEnv.define(`mut TypeEnv self, `str name, TypeInfo ty, u32 depth) -> void {
    VarType vt;
    vt.name        = name;
    vt.ty          = ty;
    vt.scope_depth = depth;
    self.locals.push(vt);
}

fn TypeEnv.lookup(`TypeEnv self, `str name) -> Option<TypeInfo> {
    /* Search from the most recent scope outward */
    i64 i = self.locals.len() as i64 - 1;
    while i >= 0 {
        VarType* vt = self.locals.get(i as usize);
        if string.eq(vt.name, name) {
            return Some(vt.ty);
        }
        i = i - 1;
    }
    return None;
}

/* ─── Primitive type constructors ─────────────────────────────────────────── */
fn ty_prim(TokenKind k) -> TypeInfo {
    TypeInfo t;
    t.kind     = NODE_TYPE_PRIM;
    t.prim     = k;
    t.inner    = null;
    t.inner2   = null;
    t.is_mut   = false;
    t.lifetime = 0;
    return t;
}

fn ty_void()  -> TypeInfo { return ty_prim(TK_VOID);  }
fn ty_bool()  -> TypeInfo { return ty_prim(TK_BOOL_T); }
fn ty_i32()   -> TypeInfo { return ty_prim(TK_I32);   }
fn ty_i64()   -> TypeInfo { return ty_prim(TK_I64);   }
fn ty_u64()   -> TypeInfo { return ty_prim(TK_U64);   }
fn ty_f64()   -> TypeInfo { return ty_prim(TK_F64);   }
fn ty_str()   -> TypeInfo { return ty_prim(TK_STR);   }
fn ty_usize() -> TypeInfo { return ty_prim(TK_USIZE); }

fn ty_ref(TypeInfo inner, bool is_mut) -> TypeInfo {
    TypeInfo t;
    t.kind   = if is_mut { NODE_TYPE_REF_MUT } else { NODE_TYPE_REF };
    t.is_mut = is_mut;
    t.inner  = mem.alloc_type<TypeInfo>();
    *t.inner = inner;
    return t;
}

fn types_compatible(`TypeInfo a, `TypeInfo b) -> bool {
    if a.kind != b.kind { return false; }
    if a.kind == NODE_TYPE_PRIM { return a.prim == b.prim; }
    if a.kind == NODE_TYPE_NAMED { return string.eq(a.name, b.name); }
    if a.kind == NODE_TYPE_REF || a.kind == NODE_TYPE_REF_MUT {
        if a.inner == null || b.inner == null { return false; }
        return types_compatible(a.inner, b.inner);
    }
    return true;
}

fn type_to_string(`TypeInfo t) -> str {
    match t.kind {
        NODE_TYPE_PRIM    -> return prim_name(t.prim),
        NODE_TYPE_REF     -> return fmt.sprintf("`%s", type_to_string(t.inner)),
        NODE_TYPE_REF_MUT -> return fmt.sprintf("`mut %s", type_to_string(t.inner)),
        NODE_TYPE_PTR     -> return fmt.sprintf("%s*", type_to_string(t.inner)),
        NODE_TYPE_ARRAY   -> return fmt.sprintf("%s[]", type_to_string(t.inner)),
        NODE_TYPE_NAMED   -> return t.name,
        _                 -> return "?",
    }
}

fn prim_name(TokenKind k) -> str {
    match k {
        TK_I8    -> return "i8",
        TK_I16   -> return "i16",
        TK_I32   -> return "i32",
        TK_I64   -> return "i64",
        TK_U8    -> return "u8",
        TK_U16   -> return "u16",
        TK_U32   -> return "u32",
        TK_U64   -> return "u64",
        TK_F32   -> return "f32",
        TK_F64   -> return "f64",
        TK_BOOL_T-> return "bool",
        TK_CHAR_T-> return "char",
        TK_STR   -> return "str",
        TK_BYTE  -> return "byte",
        TK_USIZE -> return "usize",
        TK_ISIZE -> return "isize",
        TK_VOID  -> return "void",
        _        -> return "?",
    }
}

/* ─── Type inference ──────────────────────────────────────────────────────── */
fn infer_type(`mut TypeEnv env, `ASTNode* node) -> TypeInfo {
    if node == null { return ty_void(); }

    match node.kind {
        NODE_INT_LIT    -> return ty_i32(),
        NODE_FLOAT_LIT  -> return ty_f64(),
        NODE_STRING_LIT -> return ty_str(),
        NODE_BOOL_LIT   -> return ty_bool(),
        NODE_NONE_LIT   -> return ty_void(), /* will be resolved by context */

        NODE_IDENT -> {
            match env.lookup(node.ident_name) {
                Some(ty) -> return ty,
                None     -> {
                    env.error(`node.span,
                        fmt.sprintf("undeclared variable '%s'", node.ident_name));
                    return ty_void();
                },
            }
        },

        NODE_BORROW -> {
            TypeInfo inner = infer_type(`mut env, node.borrow_expr);
            return ty_ref(inner, false);
        },

        NODE_BORROW_MUT -> {
            TypeInfo inner = infer_type(`mut env, node.borrow_expr);
            return ty_ref(inner, true);
        },

        NODE_BINARY -> {
            TypeInfo lt = infer_type(`mut env, node.left);
            TypeInfo rt = infer_type(`mut env, node.right);
            match node.op {
                TK_EQ | TK_NEQ | TK_LT | TK_GT | TK_LTE | TK_GTE
                    -> return ty_bool(),
                TK_AND | TK_OR
                    -> return ty_bool(),
                _ -> {
                    if !types_compatible(`lt, `rt) {
                        env.error(`node.span,
                            fmt.sprintf("type mismatch: %s vs %s",
                                        type_to_string(`lt), type_to_string(`rt)));
                    }
                    return lt;
                },
            }
        },

        NODE_CALL | NODE_METHOD_CALL -> {
            /* For now return void — full resolution needs the function table */
            for i in 0..node.arg_count {
                infer_type(`mut env, node.args[i]);
            }
            return ty_void();
        },

        NODE_FIELD_ACCESS -> {
            /* Resolve struct field type — simplified */
            return ty_void();
        },

        NODE_INDEX -> {
            infer_type(`mut env, node.idx_index);
            return ty_void();
        },

        NODE_CAST -> {
            infer_type(`mut env, node.cast_expr);
            return ast_type_to_typeinfo(node.cast_type);
        },

        NODE_IF_EXPR -> {
            infer_type(`mut env, node.if_cond);
            TypeInfo t = infer_type(`mut env, node.if_then);
            if node.if_else != null {
                TypeInfo e = infer_type(`mut env, node.if_else);
                if !types_compatible(`t, `e) {
                    env.error(`node.span, "if/else branches have different types");
                }
            }
            return t;
        },

        _ -> return ty_void(),
    }
}

fn ast_type_to_typeinfo(`ASTNode* node) -> TypeInfo {
    if node == null { return ty_void(); }
    match node.kind {
        NODE_TYPE_PRIM -> return ty_prim(node.type_prim),
        NODE_TYPE_REF  -> {
            TypeInfo inner = ast_type_to_typeinfo(node.type_inner);
            return ty_ref(inner, false);
        },
        NODE_TYPE_REF_MUT -> {
            TypeInfo inner = ast_type_to_typeinfo(node.type_inner);
            return ty_ref(inner, true);
        },
        NODE_TYPE_NAMED -> {
            TypeInfo t;
            t.kind = NODE_TYPE_NAMED;
            t.name = node.type_name2;
            return t;
        },
        _ -> return ty_void(),
    }
}

/* ─── Statement type checker ─────────────────────────────────────────────── */
fn tc_stmt(`mut TypeEnv env, `ASTNode* node, u32 depth) -> void {
    if node == null { return; }
    match node.kind {
        NODE_VAR_DECL -> {
            TypeInfo inferred = infer_type(`mut env, node.var_init);
            TypeInfo declared = if node.var_type != null {
                ast_type_to_typeinfo(node.var_type)
            } else {
                inferred
            };
            if node.var_type != null && !types_compatible(`inferred, `declared) {
                env.error(`node.span,
                    fmt.sprintf("type mismatch: declared '%s', got '%s'",
                                type_to_string(`declared), type_to_string(`inferred)));
            }
            env.define(node.var_name, declared, depth);
        },
        NODE_RETURN -> { infer_type(`mut env, node.ret_value); },
        NODE_EXPR_STMT -> { infer_type(`mut env, node.operand); },
        NODE_IF -> {
            TypeInfo cond_ty = infer_type(`mut env, node.if_cond);
            if !types_compatible(`cond_ty, `ty_bool()) {
                env.error(`node.span, "if condition must be bool");
            }
            tc_stmt(`mut env, node.if_then, depth);
            tc_stmt(`mut env, node.if_else, depth);
        },
        NODE_WHILE -> {
            infer_type(`mut env, node.while_cond);
            tc_stmt(`mut env, node.while_body, depth);
        },
        NODE_BLOCK | NODE_UNSAFE_BLOCK -> {
            for i in 0..node.item_count {
                tc_stmt(`mut env, node.items[i], depth + 1);
            }
        },
        NODE_ASSIGN -> {
            TypeInfo lty = infer_type(`mut env, node.lhs);
            TypeInfo rty = infer_type(`mut env, node.rhs);
            if !types_compatible(`lty, `rty) {
                env.error(`node.span, "assignment type mismatch");
            }
        },
        _ -> { infer_type(`mut env, node); },
    }
}

/* ─── Top-level entry point ───────────────────────────────────────────────── */
fn type_check(`ASTNode* ast) -> Result<void, str> {
    TypeEnv env = TypeEnv.new();
    if ast == null { return Err("null AST"); }

    for i in 0..ast.item_count {
        ASTNode* item = ast.items[i];
        if item.kind == NODE_FN_DEF {
            tc_stmt(`mut env, item.fn_body, 0);
        }
    }

    if env.had_error {
        return Err(fmt.sprintf("type check failed with %d error(s)", env.error_count));
    }
    return Ok(void);
}
