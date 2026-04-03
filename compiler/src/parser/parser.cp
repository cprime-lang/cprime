/*
 * cpc — Parser (statements + top-level)
 * compiler/src/parser/parser.cp
 * ================================
 * Recursive descent parser for C-Prime.
 * Converts a TokenStream into an AST.
 *
 * This file handles:
 *   - Top-level items: fn, struct, enum, import, const
 *   - Statements: var decl, assign, if, while, for, match, return, block
 * Expression parsing is in parser/expr.cp (Pratt parser).
 */

import core;
import "lexer/token";
import "ast/ast";
import "utils/error";

struct Parser {
    TokenStream ts;
    Reporter*   reporter;
    bool        had_error;
}

fn Parser.new(TokenStream ts, Reporter* rep) -> Parser {
    return Parser { ts: ts, reporter: rep, had_error: false };
}

fn Parser.peek(`Parser self)         -> `Token { return self.ts.peek();  }
fn Parser.peek2(`Parser self)        -> `Token { return self.ts.peek2(); }
fn Parser.advance(`mut Parser self)  -> `Token { return self.ts.advance(); }
fn Parser.check(`Parser self, TokenKind k) -> bool { return self.ts.check(k); }

fn Parser.match_tok(`mut Parser self, TokenKind k) -> bool {
    if self.check(k) { self.advance(); return true; }
    return false;
}

fn Parser.expect(`mut Parser self, TokenKind k, `str ctx) -> `Token {
    if self.check(k) { return self.advance(); }
    `Token got = self.peek();
    if self.reporter != null {
        self.reporter.errorf(`got.span,
            "expected %s in %s, got '%s'",
            token_kind_name(k), ctx, got.text);
    }
    self.had_error = true;
    return got;
}

fn Parser.skip_to(`mut Parser self, TokenKind k) -> void {
    while !self.check(k) && !self.check(TK_EOF) { self.advance(); }
}

/* ─── Type parsing ────────────────────────────────────────────────────────── */
fn Parser.parse_type(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    ASTNode* n = ast_node(NODE_TYPE_PRIM, loc);

    /* Borrow type: `T or `mut T */
    if self.match_tok(TK_BACKTICK) {
        bool is_mut = self.match_tok(TK_MUT);
        n.kind         = if is_mut { NODE_TYPE_REF_MUT } else { NODE_TYPE_REF };
        n.type_is_mut  = is_mut;
        n.type_inner   = self.parse_type();
        return n;
    }

    /* Primitive types */
    if self.peek().is_type_keyword() {
        `Token t = self.advance();
        n.kind      = NODE_TYPE_PRIM;
        n.type_prim = t.kind;
        /* T[] — array */
        if self.match_tok(TK_LBRACKET) {
            self.expect(TK_RBRACKET, "array type");
            ASTNode* arr  = ast_node(NODE_TYPE_ARRAY, loc);
            arr.type_inner = n;
            /* T* — pointer (raw, must be in unsafe) */
            if self.match_tok(TK_STAR) {
                ASTNode* ptr  = ast_node(NODE_TYPE_PTR, loc);
                ptr.type_inner = arr;
                return ptr;
            }
            return arr;
        }
        if self.match_tok(TK_STAR) {
            ASTNode* ptr  = ast_node(NODE_TYPE_PTR, loc);
            ptr.type_inner = n;
            return ptr;
        }
        return n;
    }

    /* Named type / generic */
    if self.check(TK_IDENT) {
        `Token t = self.advance();
        n.kind       = NODE_TYPE_NAMED;
        n.type_name2 = t.text;
        /* Generics: Vec<T>, Option<T>, Result<T,E> */
        if self.match_tok(TK_LT) {
            n.kind        = NODE_TYPE_GENERIC;
            n.type_args   = mem.alloc_array<ASTNode*>(8);
            n.type_arg_count = 0;
            while !self.check(TK_GT) && !self.check(TK_EOF) {
                n.type_args[n.type_arg_count] = self.parse_type();
                n.type_arg_count = n.type_arg_count + 1;
                if !self.match_tok(TK_COMMA) { break; }
            }
            self.expect(TK_GT, "generic type");
        }
        if self.match_tok(TK_LBRACKET) {
            self.expect(TK_RBRACKET, "array type");
            ASTNode* arr  = ast_node(NODE_TYPE_ARRAY, loc);
            arr.type_inner = n;
            return arr;
        }
        if self.match_tok(TK_STAR) {
            ASTNode* ptr  = ast_node(NODE_TYPE_PTR, loc);
            ptr.type_inner = n;
            return ptr;
        }
        return n;
    }

    /* fn type: fn(T, U) -> V */
    if self.match_tok(TK_FN) {
        n.kind = NODE_TYPE_FN;
        self.expect(TK_LPAREN, "fn type");
        while !self.check(TK_RPAREN) && !self.check(TK_EOF) {
            self.parse_type();
            if !self.match_tok(TK_COMMA) { break; }
        }
        self.expect(TK_RPAREN, "fn type");
        if self.match_tok(TK_ARROW) { self.parse_type(); }
        return n;
    }

    if self.reporter != null {
        self.reporter.errorf(`loc, "expected a type, got '%s'", self.peek().text);
    }
    self.had_error = true;
    return n;
}

/* ─── Parameter list ──────────────────────────────────────────────────────── */
fn Parser.parse_params(`mut Parser self, `mut ASTNode* fn_node) -> void {
    fn_node.fn_params      = mem.alloc_array<ASTNode*>(16);
    fn_node.fn_param_count = 0;

    while !self.check(TK_RPAREN) && !self.check(TK_EOF) {
        Span loc = self.peek().span;
        ASTNode* p = ast_node(NODE_VAR_DECL, loc);
        p.var_is_param = true;

        /* Type comes first: `str name or i32 name */
        p.var_type = self.parse_type();

        /* Parameter name */
        if self.check(TK_IDENT) {
            `Token name_tok = self.advance();
            p.var_name = name_tok.text;
        }
        fn_node.fn_params[fn_node.fn_param_count] = p;
        fn_node.fn_param_count = fn_node.fn_param_count + 1;

        if !self.match_tok(TK_COMMA) { break; }
    }
}

/* ─── Function definition ─────────────────────────────────────────────────── */
fn Parser.parse_fn(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* consume 'fn' */

    ASTNode* n = ast_node(NODE_FN_DEF, loc);

    /* Parse name — may be "Type.method" */
    `Token name_tok = self.expect(TK_IDENT, "fn name");
    n.fn_name = name_tok.text;

    if self.match_tok(TK_DOT) {
        `Token method_tok = self.expect(TK_IDENT, "method name");
        n.fn_type_name = name_tok.text;
        n.fn_name      = method_tok.text;
        n.fn_is_method = true;
    }

    /* Generic params <T, U> */
    if self.match_tok(TK_LT) {
        n.fn_is_generic = true;
        while !self.check(TK_GT) && !self.check(TK_EOF) {
            self.advance(); /* skip generic type param names for now */
            if !self.match_tok(TK_COMMA) { break; }
        }
        self.expect(TK_GT, "generic params");
    }

    /* Parameters */
    self.expect(TK_LPAREN, "fn params");
    self.parse_params(`mut n);
    self.expect(TK_RPAREN, "fn params");

    /* Return type */
    n.fn_ret_type = null;
    if self.match_tok(TK_ARROW) {
        n.fn_ret_type = self.parse_type();
    }

    /* Body */
    n.fn_body = self.parse_block();
    return n;
}

/* ─── Struct definition ───────────────────────────────────────────────────── */
fn Parser.parse_struct(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* consume 'struct' */

    ASTNode* n = ast_node(NODE_STRUCT_DEF, loc);
    `Token name_tok = self.expect(TK_IDENT, "struct name");
    n.type_name = name_tok.text;

    /* Optional generic params */
    if self.match_tok(TK_LT) {
        while !self.check(TK_GT) && !self.check(TK_EOF) { self.advance(); if !self.match_tok(TK_COMMA) { break; } }
        self.expect(TK_GT, "struct generics");
    }

    self.expect(TK_LBRACE, "struct body");
    n.type_fields      = mem.alloc_array<ASTNode*>(64);
    n.type_field_count = 0;

    while !self.check(TK_RBRACE) && !self.check(TK_EOF) {
        Span fspan = self.peek().span;
        ASTNode* field = ast_node(NODE_VAR_DECL, fspan);
        field.var_type = self.parse_type();
        `Token fname   = self.expect(TK_IDENT, "field name");
        field.var_name = fname.text;
        self.match_tok(TK_SEMI);
        n.type_fields[n.type_field_count] = field;
        n.type_field_count = n.type_field_count + 1;
    }
    self.expect(TK_RBRACE, "struct body");
    return n;
}

/* ─── Enum definition ─────────────────────────────────────────────────────── */
fn Parser.parse_enum(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* consume 'enum' */

    ASTNode* n = ast_node(NODE_ENUM_DEF, loc);
    `Token name_tok = self.expect(TK_IDENT, "enum name");
    n.type_name = name_tok.text;

    /* Optional generic */
    if self.match_tok(TK_LT) {
        while !self.check(TK_GT) && !self.check(TK_EOF) { self.advance(); if !self.match_tok(TK_COMMA) { break; } }
        self.expect(TK_GT, "enum generics");
    }

    self.expect(TK_LBRACE, "enum body");
    n.type_fields      = mem.alloc_array<ASTNode*>(64);
    n.type_field_count = 0;

    while !self.check(TK_RBRACE) && !self.check(TK_EOF) {
        Span vspan = self.peek().span;
        `Token vname = self.expect(TK_IDENT, "variant name");
        ASTNode* variant = ast_node(NODE_VAR_DECL, vspan);
        variant.var_name = vname.text;
        /* Payload: Variant(Type) */
        if self.match_tok(TK_LPAREN) {
            variant.var_type = self.parse_type();
            self.expect(TK_RPAREN, "enum variant payload");
        }
        self.match_tok(TK_COMMA);
        n.type_fields[n.type_field_count] = variant;
        n.type_field_count = n.type_field_count + 1;
    }
    self.expect(TK_RBRACE, "enum body");
    return n;
}

/* ─── Import ──────────────────────────────────────────────────────────────── */
fn Parser.parse_import(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* 'import' */

    ASTNode* n = ast_node(NODE_IMPORT, loc);

    /* Collect "a.b.c" or "path/to/file" */
    if self.check(TK_STRING) {
        `Token s    = self.advance();
        n.import_path = s.text;
    } else {
        str path = "";
        while self.check(TK_IDENT) || self.check(TK_DOT) {
            `Token t = self.advance();
            path = string.concat(path, t.text);
        }
        n.import_path = path;
    }
    self.match_tok(TK_SEMI);
    return n;
}

/* ─── Const definition ────────────────────────────────────────────────────── */
fn Parser.parse_const(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* 'const' */

    ASTNode* n = ast_node(NODE_CONST_DEF, loc);
    n.const_type  = self.parse_type();
    `Token name   = self.expect(TK_IDENT, "const name");
    n.const_name  = name.text;
    self.expect(TK_ASSIGN, "const value");
    n.const_value = self.parse_expr();
    self.match_tok(TK_SEMI);
    return n;
}

/* ─── Block ───────────────────────────────────────────────────────────────── */
fn Parser.parse_block(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.expect(TK_LBRACE, "block");
    ASTNode* blk = ast_node(NODE_BLOCK, loc);

    while !self.check(TK_RBRACE) && !self.check(TK_EOF) {
        ASTNode* stmt = self.parse_stmt();
        if stmt != null { ast_list_push(`mut blk, stmt); }
    }
    self.expect(TK_RBRACE, "block");
    return blk;
}

/* ─── Statements ──────────────────────────────────────────────────────────── */
fn Parser.parse_stmt(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    TokenKind k = self.peek().kind;

    /* return */
    if k == TK_RETURN {
        self.advance();
        ASTNode* n    = ast_node(NODE_RETURN, loc);
        if !self.check(TK_SEMI) && !self.check(TK_RBRACE) {
            n.ret_value = self.parse_expr();
        }
        self.match_tok(TK_SEMI);
        return n;
    }

    /* if */
    if k == TK_IF { return self.parse_if(); }

    /* while */
    if k == TK_WHILE {
        self.advance();
        ASTNode* n      = ast_node(NODE_WHILE, loc);
        n.while_cond    = self.parse_expr();
        n.while_body    = self.parse_block();
        return n;
    }

    /* for i in lo..hi */
    if k == TK_FOR { return self.parse_for(); }

    /* match */
    if k == TK_MATCH { return self.parse_match(); }

    /* unsafe block */
    if k == TK_UNSAFE {
        self.advance();
        ASTNode* n = ast_node(NODE_UNSAFE_BLOCK, loc);
        ASTNode* inner = self.parse_block();
        n.items      = inner.items;
        n.item_count = inner.item_count;
        return n;
    }

    /* break / continue */
    if k == TK_BREAK    { self.advance(); self.match_tok(TK_SEMI); return ast_node(NODE_BREAK,    loc); }
    if k == TK_CONTINUE { self.advance(); self.match_tok(TK_SEMI); return ast_node(NODE_CONTINUE, loc); }

    /* nested block */
    if k == TK_LBRACE { return self.parse_block(); }

    /* Variable declaration: TYPE name = expr; or auto name = expr; */
    if self.is_var_decl_start() { return self.parse_var_decl(); }

    /* Expression statement */
    ASTNode* expr = self.parse_expr();
    ASTNode* stmt = ast_node(NODE_EXPR_STMT, loc);
    stmt.operand  = expr;

    /* Assignment: lhs = rhs or lhs += rhs */
    if is_assign_op(self.peek().kind) {
        TokenKind op = self.peek().kind;
        self.advance();
        ASTNode* a   = ast_node(NODE_ASSIGN, loc);
        a.lhs        = expr;
        a.assign_op  = op;
        a.rhs        = self.parse_expr();
        self.match_tok(TK_SEMI);
        return a;
    }

    self.match_tok(TK_SEMI);
    return stmt;
}

fn Parser.parse_if(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* 'if' */
    ASTNode* n   = ast_node(NODE_IF, loc);
    n.if_cond    = self.parse_expr();
    n.if_then    = self.parse_block();
    n.if_else    = null;
    if self.match_tok(TK_ELSE) {
        if self.check(TK_IF) {
            n.if_else = self.parse_if(); /* else if chain */
        } else {
            n.if_else = self.parse_block();
        }
    }
    return n;
}

fn Parser.parse_for(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* 'for' */

    /* C-style: for (init; cond; step) { } */
    if self.match_tok(TK_LPAREN) {
        ASTNode* n   = ast_node(NODE_FOR_C, loc);
        ast_list_push(`mut n, self.parse_stmt());
        self.expect(TK_SEMI, "for init");
        ast_list_push(`mut n, self.parse_expr());
        self.expect(TK_SEMI, "for cond");
        ast_list_push(`mut n, self.parse_expr());
        self.expect(TK_RPAREN, "for step");
        ast_list_push(`mut n, self.parse_block());
        return n;
    }

    /* Range: for i in lo..hi { } */
    ASTNode* n = ast_node(NODE_FOR_RANGE, loc);
    `Token var_tok = self.expect(TK_IDENT, "for variable");
    n.for_var = var_tok.text;
    self.expect(TK_IN, "for..in");
    n.for_start = self.parse_expr();
    bool inclusive = false;
    if self.match_tok(TK_RANGE_EQ) { inclusive = true; }
    else { self.expect(TK_RANGE, "for range .."); }
    n.for_inclusive = inclusive;
    n.for_end       = self.parse_expr();
    n.for_body      = self.parse_block();
    return n;
}

fn Parser.parse_match(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    self.advance(); /* 'match' */
    ASTNode* n        = ast_node(NODE_MATCH, loc);
    n.match_subject   = self.parse_expr();
    n.match_arms      = mem.alloc_array<ASTNode*>(32);
    n.match_arm_count = 0;

    self.expect(TK_LBRACE, "match body");
    while !self.check(TK_RBRACE) && !self.check(TK_EOF) {
        Span aspan = self.peek().span;
        ASTNode* arm = ast_node(NODE_MATCH_ARM, aspan);

        /* Pattern */
        if self.check(TK_IDENT) && self.peek().text == "_" {
            self.advance();
            arm.arm_pattern = ast_node(NODE_WILDCARD, aspan);
        } else {
            arm.arm_pattern = self.parse_expr();
            /* Destructuring bind: Some(x), Ok(v), Err(e) */
            if self.match_tok(TK_LPAREN) {
                `Token bind = self.expect(TK_IDENT, "match binding");
                arm.arm_bind = bind.text;
                self.expect(TK_RPAREN, "match binding");
            }
        }

        /* Multiple patterns: pat1 | pat2 -> body */
        while self.match_tok(TK_BOR) {
            self.parse_expr(); /* additional patterns — simplified */
        }

        self.expect(TK_ARROW, "match arm");

        /* Arm body: either a block or an expression */
        if self.check(TK_LBRACE) {
            arm.arm_body = self.parse_block();
        } else {
            ASTNode* body_expr = self.parse_expr();
            arm.arm_body       = ast_node(NODE_EXPR_STMT, aspan);
            arm.arm_body.operand = body_expr;
        }
        self.match_tok(TK_COMMA);

        n.match_arms[n.match_arm_count] = arm;
        n.match_arm_count = n.match_arm_count + 1;
    }
    self.expect(TK_RBRACE, "match body");
    return n;
}

fn Parser.parse_var_decl(`mut Parser self) -> ASTNode* {
    Span loc = self.peek().span;
    ASTNode* n = ast_node(NODE_VAR_DECL, loc);

    if self.match_tok(TK_AUTO) {
        n.var_type = null; /* inferred */
    } else if self.match_tok(TK_CONST) {
        n.var_type = self.parse_type();
        /* treat like var decl */
    } else {
        n.var_type = self.parse_type();
    }

    /* mut modifier */
    n.var_is_mut = false;

    `Token name_tok = self.expect(TK_IDENT, "variable name");
    n.var_name = name_tok.text;

    n.var_init = null;
    if self.match_tok(TK_ASSIGN) {
        n.var_init = self.parse_expr();
    }
    self.match_tok(TK_SEMI);
    return n;
}

/* ─── Helpers ─────────────────────────────────────────────────────────────── */
fn Parser.is_var_decl_start(`Parser self) -> bool {
    TokenKind k = self.peek().kind;
    if k == TK_AUTO || k == TK_CONST { return true; }
    if k.is_type_keyword() {
        /* TYPE IDENT = ... pattern — look ahead 1 */
        return self.peek2().kind == TK_IDENT ||
               self.peek2().kind == TK_STAR  ||
               self.peek2().kind == TK_LBRACKET;
    }
    return false;
}

fn is_assign_op(TokenKind k) -> bool {
    match k {
        TK_ASSIGN    -> return true,
        TK_PLUS_EQ   -> return true,
        TK_MINUS_EQ  -> return true,
        TK_STAR_EQ   -> return true,
        TK_SLASH_EQ  -> return true,
        TK_PERCENT_EQ-> return true,
        TK_AND_EQ    -> return true,
        TK_OR_EQ     -> return true,
        TK_XOR_EQ    -> return true,
        TK_SHL_EQ    -> return true,
        TK_SHR_EQ    -> return true,
        _            -> return false,
    }
}

/* ─── Top-level program ───────────────────────────────────────────────────── */
fn parse(TokenStream ts, Reporter* rep) -> Result<ASTNode*, str> {
    Parser p = Parser.new(ts, rep);
    Span root_span;
    root_span.file = ts.tokens[0].span.file;
    root_span.line = 1;
    root_span.col  = 1;
    ASTNode* program = ast_node(NODE_PROGRAM, root_span);

    while !p.check(TK_EOF) {
        TokenKind k = p.peek().kind;
        ASTNode* item = null;

        if k == TK_KW_FN || k == TK_FN { item = p.parse_fn(); }
        else if k == TK_STRUCT          { item = p.parse_struct(); }
        else if k == TK_ENUM            { item = p.parse_enum(); }
        else if k == TK_IMPORT          { item = p.parse_import(); }
        else if k == TK_CONST           { item = p.parse_const(); }
        else {
            if rep != null {
                rep.errorf(`p.peek().span,
                    "unexpected top-level token '%s'", p.peek().text);
            }
            p.had_error = true;
            p.advance();
            continue;
        }

        if item != null { ast_list_push(`mut program, item); }
    }

    if p.had_error {
        return Err("parse failed");
    }
    return Ok(program);
}
