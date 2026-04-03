/*
 * cpc — Abstract Syntax Tree
 * compiler/src/ast/ast.cp
 * ==========================
 * All AST node types used across the parser, semantic analyser,
 * borrow checker, optimizer, and code generator.
 *
 * Design:
 *   - Every node carries a Span for error reporting
 *   - Type nodes and value nodes are separate variants
 *   - The borrow operator ` has explicit AST nodes (AST_BORROW / AST_BORROW_MUT)
 *     so the borrow checker can traverse them cleanly
 */

import core;
import mem;
import "lexer/token";

/* ─── AST Node Kinds ──────────────────────────────────────────────────────── */
enum ASTKind {
    /* ── Program ── */
    NODE_PROGRAM,          /* root: list of top-level items */

    /* ── Top-level items ── */
    NODE_FN_DEF,           /* fn name<T>(params) -> ret { body } */
    NODE_STRUCT_DEF,       /* struct Name { fields } */
    NODE_ENUM_DEF,         /* enum Name { variants } */
    NODE_IMPORT,           /* import path; */
    NODE_CONST_DEF,        /* const TYPE NAME = expr; */
    NODE_TYPE_ALIAS,       /* type Alias = Type; */

    /* ── Statements ── */
    NODE_BLOCK,            /* { stmt* } */
    NODE_VAR_DECL,         /* TYPE name = expr; / auto name = expr; */
    NODE_ASSIGN,           /* lhs = rhs */
    NODE_COMPOUND_ASSIGN,  /* lhs += rhs */
    NODE_IF,               /* if cond { } else { } */
    NODE_WHILE,            /* while cond { } */
    NODE_FOR_RANGE,        /* for i in lo..hi { } */
    NODE_FOR_C,            /* for (init; cond; step) { } */
    NODE_MATCH,            /* match expr { arms } */
    NODE_MATCH_ARM,        /* pattern -> body */
    NODE_RETURN,           /* return expr; */
    NODE_BREAK,            /* break; */
    NODE_CONTINUE,         /* continue; */
    NODE_EXPR_STMT,        /* expr; */
    NODE_UNSAFE_BLOCK,     /* unsafe { } */
    NODE_ASM_BLOCK,        /* asm { } */

    /* ── Expressions ── */
    NODE_INT_LIT,          /* 42 */
    NODE_FLOAT_LIT,        /* 3.14 */
    NODE_STRING_LIT,       /* "hello" */
    NODE_CHAR_LIT,         /* 'A' */
    NODE_BOOL_LIT,         /* true / false */
    NODE_NONE_LIT,         /* None */
    NODE_IDENT,            /* foo */
    NODE_BORROW,           /* `expr */
    NODE_BORROW_MUT,       /* `mut expr */
    NODE_DEREF,            /* *expr */
    NODE_ADDR_OF,          /* &expr */
    NODE_BINARY,           /* lhs op rhs */
    NODE_UNARY,            /* op expr */
    NODE_CALL,             /* callee(args) */
    NODE_METHOD_CALL,      /* obj.method(args) */
    NODE_FIELD_ACCESS,     /* obj.field */
    NODE_INDEX,            /* arr[idx] */
    NODE_CAST,             /* expr as Type */
    NODE_STRUCT_INIT,      /* Name { field: val } */
    NODE_ARRAY_LIT,        /* [a, b, c] */
    NODE_RANGE_EXPR,       /* lo..hi */
    NODE_RANGE_EQ_EXPR,    /* lo..=hi */
    NODE_SOME,             /* Some(expr) */
    NODE_OK,               /* Ok(expr) */
    NODE_ERR,              /* Err(expr) */
    NODE_IF_EXPR,          /* if cond { } else { } as expression */
    NODE_CLOSURE,          /* fn(params) -> T { body } */
    NODE_SIZEOF,           /* sizeof(Type) */
    NODE_ALIGNOF,          /* alignof(Type) */
    NODE_WILDCARD,         /* _ */
    NODE_VOID_EXPR,        /* () */
    NODE_BLOCK_EXPR,       /* { stmts; last_expr } */
    NODE_TUPLE,            /* (a, b, c) */

    /* ── Type nodes ── */
    NODE_TYPE_PRIM,        /* i32, f64, str, bool, void … */
    NODE_TYPE_REF,         /* `T */
    NODE_TYPE_REF_MUT,     /* `mut T */
    NODE_TYPE_PTR,         /* T* */
    NODE_TYPE_ARRAY,       /* T[] */
    NODE_TYPE_SLICE,       /* T[..] */
    NODE_TYPE_NAMED,       /* SomeStruct */
    NODE_TYPE_GENERIC,     /* Vec<T> */
    NODE_TYPE_FN,          /* fn(T) -> U */
    NODE_TYPE_OPTION,      /* Option<T> */
    NODE_TYPE_RESULT,      /* Result<T,E> */
    NODE_TYPE_TUPLE,       /* (T, U) */

    NODE_COUNT,
}

/* ─── Type information attached after semantic analysis ───────────────────── */
struct TypeInfo {
    ASTKind  kind;       /* NODE_TYPE_PRIM, NODE_TYPE_REF, etc. */
    TokenKind prim;      /* for NODE_TYPE_PRIM: TK_I32, TK_STR, etc. */
    str      name;       /* for NODE_TYPE_NAMED */
    TypeInfo* inner;     /* for ref/ptr/array/option */
    TypeInfo* inner2;    /* for Result<T,E> — error type */
    bool     is_mut;     /* for mutable borrows */
    u32      lifetime;   /* borrow lifetime id (0 = unset) */
}

/* ─── AST Node ────────────────────────────────────────────────────────────── */
struct ASTNode {
    ASTKind   kind;
    Span      span;
    TypeInfo* ty;        /* filled by semantic analyser; null before that */

    /* ── Payload (tagged union by kind) ── */

    /* NODE_PROGRAM / NODE_BLOCK / NODE_BLOCK_EXPR / NODE_UNSAFE_BLOCK */
    ASTNode** items;
    usize     item_count;
    usize     item_cap;

    /* NODE_FN_DEF */
    str       fn_name;
    str       fn_type_name;   /* "Foo" if fn is "Foo.bar" */
    bool      fn_is_method;
    bool      fn_is_generic;
    ASTNode** fn_params;
    usize     fn_param_count;
    ASTNode*  fn_ret_type;
    ASTNode*  fn_body;

    /* NODE_STRUCT_DEF / NODE_ENUM_DEF */
    str       type_name;
    ASTNode** type_fields;
    usize     type_field_count;

    /* NODE_VAR_DECL */
    str       var_name;
    ASTNode*  var_type;
    ASTNode*  var_init;
    bool      var_is_mut;
    bool      var_is_param;

    /* NODE_ASSIGN / NODE_COMPOUND_ASSIGN */
    ASTNode*  lhs;
    ASTNode*  rhs;
    TokenKind assign_op;

    /* NODE_BINARY / NODE_UNARY */
    ASTNode*  left;
    ASTNode*  right;
    ASTNode*  operand;
    TokenKind op;

    /* NODE_CALL / NODE_METHOD_CALL */
    ASTNode*  callee;
    ASTNode** args;
    usize     arg_count;

    /* NODE_FIELD_ACCESS */
    ASTNode*  object;
    str       field_name;

    /* NODE_IF / NODE_IF_EXPR */
    ASTNode*  if_cond;
    ASTNode*  if_then;
    ASTNode*  if_else;

    /* NODE_WHILE */
    ASTNode*  while_cond;
    ASTNode*  while_body;

    /* NODE_FOR_RANGE */
    str       for_var;
    ASTNode*  for_start;
    ASTNode*  for_end;
    ASTNode*  for_body;
    bool      for_inclusive;

    /* NODE_MATCH */
    ASTNode*  match_subject;
    ASTNode** match_arms;
    usize     match_arm_count;

    /* NODE_MATCH_ARM */
    ASTNode*  arm_pattern;
    str       arm_bind;        /* binding in Some(x) or Ok(x) */
    ASTNode*  arm_body;
    bool      arm_is_guard;
    ASTNode*  arm_guard;

    /* NODE_RETURN */
    ASTNode*  ret_value;

    /* NODE_INDEX */
    ASTNode*  idx_array;
    ASTNode*  idx_index;

    /* NODE_CAST */
    ASTNode*  cast_expr;
    ASTNode*  cast_type;

    /* NODE_STRUCT_INIT */
    str       init_type;
    str*      init_field_names;
    ASTNode** init_field_values;
    usize     init_field_count;

    /* NODE_BORROW / NODE_BORROW_MUT / NODE_DEREF / NODE_ADDR_OF */
    ASTNode*  borrow_expr;

    /* NODE_CONST_DEF */
    str       const_name;
    ASTNode*  const_type;
    ASTNode*  const_value;

    /* NODE_IMPORT */
    str       import_path;

    /* NODE_SOME / NODE_OK / NODE_ERR */
    ASTNode*  wrap_inner;

    /* Literals */
    i64       lit_int;
    f64       lit_float;
    str       lit_str;
    char      lit_char;
    bool      lit_bool;

    /* NODE_IDENT */
    str       ident_name;

    /* NODE_TYPE_PRIM */
    TokenKind type_prim;

    /* NODE_TYPE_NAMED / NODE_TYPE_GENERIC */
    str       type_name2;
    ASTNode** type_args;
    usize     type_arg_count;

    /* NODE_TYPE_REF / NODE_TYPE_PTR / NODE_TYPE_ARRAY */
    ASTNode*  type_inner;
    bool      type_is_mut;
}

/* ─── Node constructors ───────────────────────────────────────────────────── */
fn ast_node(ASTKind kind, Span span) -> ASTNode* {
    ASTNode* n = mem.alloc_type<ASTNode>();
    n.kind = kind;
    n.span = span;
    n.ty   = null;
    return n;
}

fn ast_list_push(`mut ASTNode* node, ASTNode* child) -> void {
    if node.item_count >= node.item_cap {
        node.item_cap = if node.item_cap == 0 { 8 } else { node.item_cap * 2 };
        node.items = mem.realloc_array<ASTNode*>(node.items, node.item_cap);
    }
    node.items[node.item_count] = child;
    node.item_count = node.item_count + 1;
}

/* ─── AST visitor interface ───────────────────────────────────────────────── */
fn ast_walk(`ASTNode* node, fn(`ASTNode*) -> void visitor) -> void {
    if node == null { return; }
    visitor(node);

    /* Recurse into children based on kind */
    if node.kind == NODE_PROGRAM || node.kind == NODE_BLOCK ||
       node.kind == NODE_UNSAFE_BLOCK {
        for i in 0..node.item_count { ast_walk(node.items[i], visitor); }
    }
    if node.kind == NODE_FN_DEF {
        for i in 0..node.fn_param_count { ast_walk(node.fn_params[i], visitor); }
        ast_walk(node.fn_ret_type, visitor);
        ast_walk(node.fn_body, visitor);
    }
    if node.kind == NODE_BINARY {
        ast_walk(node.left,  visitor);
        ast_walk(node.right, visitor);
    }
    if node.kind == NODE_CALL || node.kind == NODE_METHOD_CALL {
        ast_walk(node.callee, visitor);
        for i in 0..node.arg_count { ast_walk(node.args[i], visitor); }
    }
    if node.kind == NODE_IF || node.kind == NODE_IF_EXPR {
        ast_walk(node.if_cond, visitor);
        ast_walk(node.if_then, visitor);
        ast_walk(node.if_else, visitor);
    }
    if node.kind == NODE_WHILE {
        ast_walk(node.while_cond, visitor);
        ast_walk(node.while_body, visitor);
    }
    if node.kind == NODE_MATCH {
        ast_walk(node.match_subject, visitor);
        for i in 0..node.match_arm_count { ast_walk(node.match_arms[i], visitor); }
    }
    if node.kind == NODE_BORROW || node.kind == NODE_BORROW_MUT ||
       node.kind == NODE_DEREF  || node.kind == NODE_ADDR_OF {
        ast_walk(node.borrow_expr, visitor);
    }
    if node.kind == NODE_RETURN { ast_walk(node.ret_value, visitor); }
    if node.kind == NODE_VAR_DECL {
        ast_walk(node.var_type, visitor);
        ast_walk(node.var_init, visitor);
    }
}

/* ─── Debug dump ──────────────────────────────────────────────────────────── */
fn ast_dump(`ASTNode* node, i32 depth) -> void {
    if node == null { return; }
    str indent = string.repeat("  ", depth as usize);
    match node.kind {
        NODE_PROGRAM    -> io.printf("%s[Program]\n", indent),
        NODE_FN_DEF     -> io.printf("%s[FnDef] %s()\n",      indent, node.fn_name),
        NODE_VAR_DECL   -> io.printf("%s[VarDecl] %s\n",      indent, node.var_name),
        NODE_IDENT      -> io.printf("%s[Ident] \"%s\"\n",    indent, node.ident_name),
        NODE_INT_LIT    -> io.printf("%s[Int] %d\n",          indent, node.lit_int),
        NODE_STRING_LIT -> io.printf("%s[String] \"%s\"\n",   indent, node.lit_str),
        NODE_BINARY     -> io.printf("%s[Binary] op=%d\n",    indent, node.op),
        NODE_CALL       -> io.printf("%s[Call]\n",            indent),
        NODE_BORROW     -> io.printf("%s[Borrow]\n",          indent),
        NODE_BORROW_MUT -> io.printf("%s[BorrowMut]\n",       indent),
        NODE_RETURN     -> io.printf("%s[Return]\n",          indent),
        NODE_IF         -> io.printf("%s[If]\n",              indent),
        NODE_WHILE      -> io.printf("%s[While]\n",           indent),
        NODE_MATCH      -> io.printf("%s[Match]\n",           indent),
        NODE_BLOCK      -> io.printf("%s[Block]\n",           indent),
        NODE_STRUCT_DEF -> io.printf("%s[Struct] %s\n",       indent, node.type_name),
        NODE_IMPORT     -> io.printf("%s[Import] \"%s\"\n",   indent, node.import_path),
        _               -> io.printf("%s[Node kind=%d]\n",    indent, node.kind),
    }
}
