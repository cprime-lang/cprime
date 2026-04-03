/*
 * cpc — Expression Parser (Pratt Parser)
 * compiler/src/parser/expr.cp
 * ===================================
 * Parses C-Prime expressions using a Pratt (top-down operator precedence)
 * parser. This gives clean, correct precedence handling without grammar
 * ambiguity.
 *
 * Precedence levels (highest to lowest):
 *   14  Unary: -, !, ~, *, &, `
 *   13  Cast: as
 *   12  Multiplicative: *, /, %
 *   11  Additive: +, -
 *   10  Shifts: <<, >>
 *    9  Bitwise AND: &
 *    8  Bitwise XOR: ^
 *    7  Bitwise OR: |
 *    6  Comparison: ==, !=, <, >, <=, >=
 *    5  Logical AND: &&
 *    4  Logical OR: ||
 *    3  Range: ..  ..=
 *    1  Lowest
 */

import core;
import "lexer/token";
import "ast/ast";
import "utils/error";

/* ─── Precedence values ───────────────────────────────────────────────────── */
fn infix_precedence(TokenKind k) -> i32 {
    match k {
        TK_STAR    | TK_SLASH | TK_PERCENT     -> return 12,
        TK_PLUS    | TK_MINUS                  -> return 11,
        TK_SHL     | TK_SHR                    -> return 10,
        TK_BAND                                -> return 9,
        TK_BXOR                                -> return 8,
        TK_BOR                                 -> return 7,
        TK_EQ | TK_NEQ | TK_LT | TK_GT
            | TK_LTE | TK_GTE                  -> return 6,
        TK_AND                                 -> return 5,
        TK_OR                                  -> return 4,
        TK_RANGE   | TK_RANGE_EQ               -> return 3,
        TK_AS                                  -> return 13,
        TK_DOT     | TK_DCOLON                 -> return 15,
        TK_LPAREN  | TK_LBRACKET               -> return 15,
        _                                      -> return 0,
    }
}

/* ─── Prefix parse (nud) ──────────────────────────────────────────────────── */
fn parse_prefix(Parser* p) -> ASTNode* {
    Span span = p.peek().span;

    /* Borrow: `expr  or  `mut expr */
    if p.check(TK_BACKTICK) {
        p.advance();
        if p.match_kind(TK_MUT) {
            ASTNode* n = ast_node(NODE_BORROW_MUT, span);
            n.borrow_expr = parse_expr_bp(p, 13);
            return n;
        }
        ASTNode* n = ast_node(NODE_BORROW, span);
        n.borrow_expr = parse_expr_bp(p, 13);
        return n;
    }

    /* Unary minus */
    if p.match_kind(TK_MINUS) {
        ASTNode* n = ast_node(NODE_UNARY, span);
        n.op      = TK_MINUS;
        n.operand = parse_expr_bp(p, 13);
        return n;
    }

    /* Logical not */
    if p.match_kind(TK_NOT) {
        ASTNode* n = ast_node(NODE_UNARY, span);
        n.op      = TK_NOT;
        n.operand = parse_expr_bp(p, 13);
        return n;
    }

    /* Bitwise not */
    if p.match_kind(TK_BNOT) {
        ASTNode* n = ast_node(NODE_UNARY, span);
        n.op      = TK_BNOT;
        n.operand = parse_expr_bp(p, 13);
        return n;
    }

    /* Deref: *expr */
    if p.match_kind(TK_STAR) {
        ASTNode* n = ast_node(NODE_DEREF, span);
        n.borrow_expr = parse_expr_bp(p, 13);
        return n;
    }

    /* Address of: &expr */
    if p.match_kind(TK_BAND) {
        ASTNode* n = ast_node(NODE_ADDR_OF, span);
        n.borrow_expr = parse_expr_bp(p, 13);
        return n;
    }

    /* Grouped: (expr) */
    if p.match_kind(TK_LPAREN) {
        ASTNode* inner = parse_expr(p);
        p.expect(TK_RPAREN, ")");
        return inner;
    }

    /* Array literal: [a, b, c] */
    if p.match_kind(TK_LBRACKET) {
        ASTNode* n = ast_node(NODE_ARRAY_LIT, span);
        while !p.check(TK_RBRACKET) && !p.at_end() {
            ast_list_push(n, parse_expr(p));
            if !p.match_kind(TK_COMMA) { break; }
        }
        p.expect(TK_RBRACKET, "]");
        return n;
    }

    /* Wildcard */
    if p.match_kind(TK_IDENT) {
        `Token tok = p.ts.tokens[p.ts.pos - 1];
        if string.eq(tok.text, "_") {
            return ast_node(NODE_WILDCARD, span);
        }

        /* Some(expr), Ok(expr), Err(expr) */
        ASTKind wrap_kind = NODE_COUNT;
        if string.eq(tok.text, "Some") { wrap_kind = NODE_SOME; }
        if string.eq(tok.text, "Ok")   { wrap_kind = NODE_OK;   }
        if string.eq(tok.text, "Err")  { wrap_kind = NODE_ERR;  }
        if wrap_kind != NODE_COUNT && p.check(TK_LPAREN) {
            p.advance();
            ASTNode* n = ast_node(wrap_kind, span);
            n.wrap_inner = parse_expr(p);
            p.expect(TK_RPAREN, ")");
            return n;
        }

        /* Struct initializer: Name { field: val, ... } */
        if p.check(TK_LBRACE) && !is_block_context(p) {
            return parse_struct_init(p, tok.text, span);
        }

        /* Plain identifier */
        ASTNode* id = ast_node(NODE_IDENT, span);
        id.ident_name = tok.text;
        return id;
    }

    /* Literals */
    if p.check(TK_INT_LIT) {
        `Token tok = p.advance();
        ASTNode* n = ast_node(NODE_INT_LIT, span);
        n.lit_int   = tok.int_val;
        return n;
    }
    if p.check(TK_FLOAT_LIT) {
        `Token tok = p.advance();
        ASTNode* n = ast_node(NODE_FLOAT_LIT, span);
        n.lit_float = tok.float_val;
        return n;
    }
    if p.check(TK_STRING_LIT) {
        `Token tok = p.advance();
        ASTNode* n = ast_node(NODE_STRING_LIT, span);
        n.lit_str   = tok.text;
        return n;
    }
    if p.check(TK_CHAR_LIT) {
        `Token tok = p.advance();
        ASTNode* n = ast_node(NODE_CHAR_LIT, span);
        n.lit_char  = tok.text[0] as char;
        return n;
    }
    if p.match_kind(TK_TRUE) {
        ASTNode* n = ast_node(NODE_BOOL_LIT, span);
        n.lit_bool = true;
        return n;
    }
    if p.match_kind(TK_FALSE) {
        ASTNode* n = ast_node(NODE_BOOL_LIT, span);
        n.lit_bool = false;
        return n;
    }
    if p.match_kind(TK_NONE) {
        return ast_node(NODE_NONE_LIT, span);
    }

    /* if expression */
    if p.check(TK_IF) {
        return p.parse_if();
    }

    p.rep.errorf(`span, "unexpected token in expression: '%s'", p.peek().text);
    p.advance();
    return ast_node(NODE_VOID_EXPR, span);
}

/* ─── Infix parse (led) ───────────────────────────────────────────────────── */
fn parse_infix(Parser* p, ASTNode* left, TokenKind op, Span span) -> ASTNode* {

    /* Method call / field access: expr.name  or  expr.name(args) */
    if op == TK_DOT {
        `Token field_tok = p.expect(TK_IDENT, "field or method name");
        if p.check(TK_LPAREN) {
            p.advance();
            ASTNode* n = ast_node(NODE_METHOD_CALL, span);
            n.callee      = left;
            n.field_name  = field_tok.text;
            Vec<ASTNode*> args = parse_args(p);
            n.args        = args.data;
            n.arg_count   = args.len;
            return n;
        }
        ASTNode* n = ast_node(NODE_FIELD_ACCESS, span);
        n.object     = left;
        n.field_name = field_tok.text;
        return n;
    }

    /* Function call: expr(args) */
    if op == TK_LPAREN {
        ASTNode* n = ast_node(NODE_CALL, span);
        n.callee    = left;
        Vec<ASTNode*> args = parse_args(p);
        n.args      = args.data;
        n.arg_count = args.len;
        return n;
    }

    /* Index: expr[idx] */
    if op == TK_LBRACKET {
        ASTNode* n = ast_node(NODE_INDEX, span);
        n.idx_array = left;
        n.idx_index = parse_expr(p);
        p.expect(TK_RBRACKET, "]");
        return n;
    }

    /* Cast: expr as Type */
    if op == TK_AS {
        ASTNode* n = ast_node(NODE_CAST, span);
        n.cast_expr = left;
        n.cast_type = p.parse_type();
        return n;
    }

    /* Range: lo..hi  or  lo..=hi */
    if op == TK_RANGE || op == TK_RANGE_EQ {
        ASTNode* n = ast_node(if op == TK_RANGE { NODE_RANGE_EXPR } else { NODE_RANGE_EQ_EXPR }, span);
        n.left  = left;
        n.right = parse_expr_bp(p, infix_precedence(op));
        return n;
    }

    /* Binary operator */
    ASTNode* n = ast_node(NODE_BINARY, span);
    n.left  = left;
    n.op    = op;
    n.right = parse_expr_bp(p, infix_precedence(op));
    return n;
}

/* ─── Pratt core ──────────────────────────────────────────────────────────── */
fn parse_expr_bp(Parser* p, i32 min_bp) -> ASTNode* {
    ASTNode* left = parse_prefix(p);
    if left == null { return null; }

    while true {
        TokenKind op  = p.peek().kind;
        i32       prec = infix_precedence(op);
        if prec <= min_bp { break; }

        Span span = p.peek().span;
        p.advance();
        left = parse_infix(p, left, op, span);
    }

    return left;
}

fn parse_expr(Parser* p) -> ASTNode* {
    return parse_expr_bp(p, 0);
}

/* ─── Argument list helper ────────────────────────────────────────────────── */
fn parse_args(Parser* p) -> Vec<ASTNode*> {
    Vec<ASTNode*> args = Vec.new();
    while !p.check(TK_RPAREN) && !p.at_end() {
        args.push(parse_expr(p));
        if !p.match_kind(TK_COMMA) { break; }
    }
    p.expect(TK_RPAREN, ")");
    return args;
}

/* ─── Struct initializer ──────────────────────────────────────────────────── */
fn parse_struct_init(Parser* p, `str type_name, Span span) -> ASTNode* {
    p.advance(); /* { */
    ASTNode* n = ast_node(NODE_STRUCT_INIT, span);
    n.init_type = type_name;

    Vec<str>      names  = Vec.new();
    Vec<ASTNode*> values = Vec.new();

    while !p.check(TK_RBRACE) && !p.at_end() {
        `Token field = p.expect(TK_IDENT, "field name");
        p.expect(TK_COLON, ":");
        ASTNode* val = parse_expr(p);
        names.push(field.text);
        values.push(val);
        if !p.match_kind(TK_COMMA) { break; }
    }
    p.expect(TK_RBRACE, "}");

    n.init_field_names  = names.data;
    n.init_field_values = values.data;
    n.init_field_count  = names.len;
    return n;
}

/* Is the current position the start of a block, not a struct init? */
fn is_block_context(Parser* p) -> bool {
    /* If the previous token was 'else', 'while', 'for', 'unsafe', or '->'
       and we see '{', it's a block not a struct init */
    /* Simplified: look for 'return', assignment operators preceding */
    return false;
}
