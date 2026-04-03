/*
 * C-Prime Bootstrap Parser — parser.c
 * =====================================
 * Recursive-descent parser. Consumes a TokenStream and produces an AST.
 *
 * Grammar (abbreviated):
 *   program       := item*
 *   item          := fn_def | struct_def | const_def | import_stmt
 *   fn_def        := 'fn' ident '(' params ')' '->' type block
 *   block         := '{' stmt* '}'
 *   stmt          := var_decl | assign | if | while | for | match
 *                  | return | expr_stmt
 *   expr          := binary | unary | call | borrow | literal | ident
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "../../include/lexer.h"
#include "../../include/parser.h"

/* ─── Parser State ────────────────────────────────────────────────────────── */
typedef struct {
    TokenStream* ts;
    bool         had_error;
} Parser;

/* ─── Helpers ─────────────────────────────────────────────────────────────── */
static Token* p_peek(Parser* p) {
    return token_stream_peek(p->ts);
}

static Token* p_advance(Parser* p) {
    return token_stream_consume(p->ts);
}

static bool p_check(Parser* p, TokenType t) {
    return p_peek(p)->type == t;
}

static bool p_match(Parser* p, TokenType t) {
    if (p_check(p, t)) { p_advance(p); return true; }
    return false;
}

static Token* p_expect(Parser* p, TokenType t, const char* msg) {
    if (p_check(p, t)) return p_advance(p);
    Token* got = p_peek(p);
    fprintf(stderr, "\033[31m[parser] %s:%d:%d: error: %s (got '%.*s' type=%s)\033[0m\n",
            got->loc.filename, got->loc.line, got->loc.column, msg,
            (int)got->length, got->start, token_type_name(got->type));
    p->had_error = true;
    return got;
}

static SourceLoc p_loc(Parser* p) {
    return p_peek(p)->loc;
}

/* Check if current token is a type keyword */
static bool p_is_type(Parser* p) {
    TokenType t = p_peek(p)->type;
    return (t >= TOK_TYPE_I8 && t <= TOK_TYPE_VOID) ||
           t == TOK_IDENT || t == TOK_BACKTICK;
}

/* ─── AST Node Constructors ───────────────────────────────────────────────── */
ASTNode* ast_new(ASTKind kind, SourceLoc loc) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    if (!n) { fprintf(stderr, "[parser] OOM\n"); exit(1); }
    n->kind = kind;
    n->loc  = loc;
    return n;
}

void ast_list_push(ASTNode* list_node, ASTNode* child) {
    if (list_node->as.list.count >= list_node->as.list.cap) {
        size_t new_cap = list_node->as.list.cap == 0 ? 8 : list_node->as.list.cap * 2;
        list_node->as.list.items = realloc(list_node->as.list.items,
                                           new_cap * sizeof(ASTNode*));
        list_node->as.list.cap = new_cap;
    }
    list_node->as.list.items[list_node->as.list.count++] = child;
}

/* ─── Forward declarations ────────────────────────────────────────────────── */
static ASTNode* parse_expr(Parser* p);
static ASTNode* parse_stmt(Parser* p);
static ASTNode* parse_block(Parser* p);
static ASTNode* parse_type(Parser* p);
static ASTNode* parse_postfix(Parser* p);

/* ─── Token text copy helper ──────────────────────────────────────────────── */
static void tok_copy(char* dst, size_t dsz, Token* t) {
    size_t n = t->length < dsz - 1 ? t->length : dsz - 1;
    memcpy(dst, t->start, n);
    dst[n] = '\0';
}

/* ─── Type Parsing ────────────────────────────────────────────────────────── */
static ASTNode* parse_type(Parser* p) {
    SourceLoc loc = p_loc(p);

    /* Borrow type: `T or `mut T */
    if (p_check(p, TOK_BACKTICK)) {
        p_advance(p);
        ASTKind kind = AST_TYPE_REF;
        if (p_match(p, TOK_KW_MUT)) kind = AST_TYPE_REF_MUT;
        ASTNode* n = ast_new(kind, loc);
        n->as.type.inner = parse_type(p);
        return n;
    }

    /* Primitive types */
    if (p_peek(p)->type >= TOK_TYPE_I8 && p_peek(p)->type <= TOK_TYPE_VOID) {
        Token* t = p_advance(p);
        ASTNode* n = ast_new(AST_TYPE_PRIM, loc);
        n->as.type.prim = t->type;

        /* Array type: T[] */
        if (p_match(p, TOK_LBRACKET)) {
            p_expect(p, TOK_RBRACKET, "expected ']' after array type");
            ASTNode* arr = ast_new(AST_TYPE_ARRAY, loc);
            arr->as.type.inner = n;
            return arr;
        }
        /* Pointer: T* */
        if (p_match(p, TOK_STAR)) {
            ASTNode* ptr = ast_new(AST_TYPE_PTR, loc);
            ptr->as.type.inner = n;
            return ptr;
        }
        return n;
    }

    /* Named type (includes generics) */
    if (p_check(p, TOK_IDENT)) {
        Token* t = p_advance(p);
        ASTNode* n = ast_new(AST_TYPE_NAMED, loc);
        tok_copy(n->as.type.name, sizeof(n->as.type.name), t);

        /* Generics: Vec<T> */
        if (p_match(p, TOK_LT)) {
            /* Just consume until matching > for now (simplified) */
            int depth = 1;
            while (!p_check(p, TOK_EOF) && depth > 0) {
                if (p_check(p, TOK_LT)) depth++;
                if (p_check(p, TOK_GT)) depth--;
                if (depth > 0) p_advance(p);
            }
            p_expect(p, TOK_GT, "expected '>' to close generic");
        }

        /* Array: Name[] */
        if (p_match(p, TOK_LBRACKET)) {
            p_expect(p, TOK_RBRACKET, "expected ']'");
            ASTNode* arr = ast_new(AST_TYPE_ARRAY, loc);
            arr->as.type.inner = n;
            return arr;
        }
        /* Pointer: Name* */
        if (p_match(p, TOK_STAR)) {
            ASTNode* ptr = ast_new(AST_TYPE_PTR, loc);
            ptr->as.type.inner = n;
            return ptr;
        }
        return n;
    }

    /* fn() -> T type */
    if (p_match(p, TOK_KW_FN)) {
        ASTNode* n = ast_new(AST_TYPE_FN, loc);
        p_expect(p, TOK_LPAREN, "expected '(' in fn type");
        while (!p_check(p, TOK_RPAREN) && !p_check(p, TOK_EOF)) {
            parse_type(p);
            p_match(p, TOK_COMMA);
        }
        p_expect(p, TOK_RPAREN, "expected ')'");
        if (p_match(p, TOK_ARROW)) parse_type(p);
        return n;
    }

    fprintf(stderr, "\033[31m[parser] %s:%d: expected type\033[0m\n",
            loc.filename, loc.line);
    p->had_error = true;
    ASTNode* err = ast_new(AST_TYPE_PRIM, loc);
    err->as.type.prim = TOK_TYPE_VOID;
    return err;
}

/* ─── Argument list parser ────────────────────────────────────────────────── */
static size_t parse_args(Parser* p, ASTNode*** out_args) {
    /* Expects to be called after '(' has been consumed */
    size_t cap = 4, count = 0;
    ASTNode** args = malloc(cap * sizeof(ASTNode*));

    while (!p_check(p, TOK_RPAREN) && !p_check(p, TOK_EOF)) {
        if (count >= cap) { cap *= 2; args = realloc(args, cap * sizeof(ASTNode*)); }
        args[count++] = parse_expr(p);
        if (!p_match(p, TOK_COMMA)) break;
    }
    p_expect(p, TOK_RPAREN, "expected ')' to close argument list");
    *out_args = args;
    return count;
}

/* ─── Primary Expression ──────────────────────────────────────────────────── */
static ASTNode* parse_primary(Parser* p) {
    SourceLoc loc = p_loc(p);

    /* Integer literal */
    if (p_check(p, TOK_INT_LIT)) {
        Token* t = p_advance(p);
        ASTNode* n = ast_new(AST_INT_LIT, loc);
        n->as.lit.int_val = t->value.int_val;
        /* re-parse since we strtoll'd the raw text */
        n->as.lit.int_val = strtoll(t->start, NULL, 0);
        return n;
    }

    /* Float literal */
    if (p_check(p, TOK_FLOAT_LIT)) {
        Token* t = p_advance(p);
        ASTNode* n = ast_new(AST_FLOAT_LIT, loc);
        n->as.lit.float_val = strtod(t->start, NULL);
        return n;
    }

    /* String literal */
    if (p_check(p, TOK_STRING_LIT)) {
        Token* t = p_advance(p);
        ASTNode* n = ast_new(AST_STRING_LIT, loc);
        /* Strip surrounding quotes and copy, handle escapes */
        size_t src_len = t->length >= 2 ? t->length - 2 : 0;
        const char* src = t->start + 1;
        size_t di = 0;
        for (size_t si = 0; si < src_len && di < sizeof(n->as.lit.str_val) - 1; si++) {
            if (src[si] == '\\' && si + 1 < src_len) {
                si++;
                switch (src[si]) {
                    case 'n':  n->as.lit.str_val[di++] = '\n'; break;
                    case 't':  n->as.lit.str_val[di++] = '\t'; break;
                    case 'r':  n->as.lit.str_val[di++] = '\r'; break;
                    case '0':  n->as.lit.str_val[di++] = '\0'; break;
                    case '\\': n->as.lit.str_val[di++] = '\\'; break;
                    case '"':  n->as.lit.str_val[di++] = '"';  break;
                    default:   n->as.lit.str_val[di++] = src[si]; break;
                }
            } else {
                n->as.lit.str_val[di++] = src[si];
            }
        }
        n->as.lit.str_val[di] = '\0';
        return n;
    }

    /* bool */
    if (p_check(p, TOK_KW_TRUE) || p_check(p, TOK_KW_FALSE)) {
        Token* t = p_advance(p);
        ASTNode* n = ast_new(AST_BOOL_LIT, loc);
        n->as.lit.bool_val = (t->type == TOK_KW_TRUE);
        return n;
    }

    /* None */
    if (p_match(p, TOK_KW_NONE)) {
        return ast_new(AST_NONE_LIT, loc);
    }

    /* Some(x) */
    if (p_match(p, TOK_KW_SOME)) {
        p_expect(p, TOK_LPAREN, "expected '(' after Some");
        ASTNode* inner = parse_expr(p);
        p_expect(p, TOK_RPAREN, "expected ')'");
        ASTNode* n = ast_new(AST_SOME, loc);
        n->as.wrap.inner = inner;
        return n;
    }

    /* Ok(x) */
    if (p_match(p, TOK_KW_OK)) {
        p_expect(p, TOK_LPAREN, "expected '(' after Ok");
        ASTNode* inner = parse_expr(p);
        p_expect(p, TOK_RPAREN, "expected ')'");
        ASTNode* n = ast_new(AST_OK, loc);
        n->as.wrap.inner = inner;
        return n;
    }

    /* Err(x) */
    if (p_match(p, TOK_KW_ERR)) {
        p_expect(p, TOK_LPAREN, "expected '(' after Err");
        ASTNode* inner = parse_expr(p);
        p_expect(p, TOK_RPAREN, "expected ')'");
        ASTNode* n = ast_new(AST_ERR, loc);
        n->as.wrap.inner = inner;
        return n;
    }

    /* Backtick borrow: `expr or `mut expr */
    if (p_match(p, TOK_BACKTICK)) {
        bool is_mut = p_match(p, TOK_KW_MUT);
        ASTNode* inner = parse_primary(p);
        ASTNode* n = ast_new(is_mut ? AST_BORROW_MUT : AST_BORROW, loc);
        n->as.borrow.expr   = inner;
        n->as.borrow.is_mut = is_mut;
        return n;
    }

    /* Deref: *expr */
    if (p_match(p, TOK_STAR)) {
        ASTNode* inner = parse_primary(p);
        ASTNode* n = ast_new(AST_DEREF, loc);
        n->as.unary.operand = inner;
        n->as.unary.op = TOK_STAR;
        return n;
    }

    /* Unary minus / not / borrow */
    if (p_check(p, TOK_MINUS) || p_check(p, TOK_NOT)) {
        Token* op = p_advance(p);
        /* Use parse_postfix so !obj.method(x) works correctly */
        ASTNode* operand = parse_postfix(p);
        ASTNode* n = ast_new(AST_UNARY, loc);
        n->as.unary.operand = operand;
        n->as.unary.op = op->type;
        return n;
    }

    /* Grouping: (expr) */
    if (p_match(p, TOK_LPAREN)) {
        ASTNode* inner = parse_expr(p);
        p_expect(p, TOK_RPAREN, "expected ')'");
        return inner;
    }

    /* Identifier / function call / struct init */
    if (p_check(p, TOK_IDENT)) {
        Token* t = p_advance(p);
        ASTNode* ident = ast_new(AST_IDENT, loc);
        tok_copy(ident->as.ident.name, sizeof(ident->as.ident.name), t);

        /* Struct initializer: Name { field: val, ... } */
        if (p_check(p, TOK_LBRACE)) {
            /* Look ahead to see if this is a struct init or just a block.
               If the next token after { is IDENT : then it's struct init. */
            size_t saved_pos = p->ts->position;
            p_advance(p); /* { */
            bool is_struct_init = false;
            if (p_check(p, TOK_IDENT)) {
                /* Could be struct init */
                p_advance(p);
                if (p_check(p, TOK_COLON)) is_struct_init = true;
            } else if (p_check(p, TOK_RBRACE)) {
                is_struct_init = true; /* empty struct */
            }
            p->ts->position = saved_pos; /* rewind */

            if (is_struct_init) {
                p_advance(p); /* consume { */
                ASTNode* n = ast_new(AST_STRUCT_INIT, loc);
                tok_copy(n->as.struct_init.type_name,
                         sizeof(n->as.struct_init.type_name), t);

                size_t cap = 8, count = 0;
                char**    names  = malloc(cap * sizeof(char*));
                ASTNode** values = malloc(cap * sizeof(ASTNode*));

                while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
                    if (count >= cap) {
                        cap *= 2;
                        names  = realloc(names,  cap * sizeof(char*));
                        values = realloc(values, cap * sizeof(ASTNode*));
                    }
                    Token* field_tok = p_expect(p, TOK_IDENT, "expected field name");
                    names[count] = malloc(field_tok->length + 1);
                    tok_copy(names[count], field_tok->length + 1, field_tok);
                    p_expect(p, TOK_COLON, "expected ':'");
                    values[count] = parse_expr(p);
                    count++;
                    if (!p_match(p, TOK_COMMA)) break;
                }
                p_expect(p, TOK_RBRACE, "expected '}' to close struct initializer");
                n->as.struct_init.field_names   = names;
                n->as.struct_init.field_values  = values;
                n->as.struct_init.field_count   = count;
                return n;
            }
        }

        /* Call: ident(args) */
        if (p_match(p, TOK_LPAREN)) {
            ASTNode* n = ast_new(AST_CALL, loc);
            n->as.call.callee = ident;
            n->as.call.arg_count = parse_args(p, &n->as.call.args);
            return n;
        }

        return ident;
    }

    /* Wildcard _ */
    if (p_check(p, TOK_IDENT)) {
        Token* t = p_peek(p);
        if (t->length == 1 && t->start[0] == '_') {
            p_advance(p);
            return ast_new(AST_WILDCARD, loc);
        }
    }

    /* If expression (used in value position) */
    if (p_check(p, TOK_KW_IF)) {
        return parse_stmt(p);
    }

    fprintf(stderr, "\033[31m[parser] %s:%d:%d: unexpected token '%.*s'\033[0m\n",
            loc.filename, loc.line, loc.column,
            (int)p_peek(p)->length, p_peek(p)->start);
    p->had_error = true;
    p_advance(p); /* skip bad token */
    return ast_new(AST_VOID_EXPR, loc);
}

/* ─── Postfix: call, method call, field, index ────────────────────────────── */
static ASTNode* parse_postfix(Parser* p) {
    ASTNode* left = parse_primary(p);
    SourceLoc loc = left->loc;

    while (true) {
        /* Dot: field access or method call */
        if (p_match(p, TOK_DOT)) {
            Token* field_tok = p_expect(p, TOK_IDENT, "expected field/method name");

            if (p_match(p, TOK_LPAREN)) {
                /* Method call: obj.method(args) */
                ASTNode* n = ast_new(AST_METHOD_CALL, loc);
                n->as.call.callee = left; /* object is stored as callee */
                /* Store method name in callee's ident field (reuse) */
                ASTNode* method_ident = ast_new(AST_IDENT, field_tok->loc);
                tok_copy(method_ident->as.ident.name,
                         sizeof(method_ident->as.ident.name), field_tok);
                /* args: first arg is object, then actual args */
                ASTNode** args = NULL;
                size_t argc = parse_args(p, &args);
                n->as.call.args = args;
                n->as.call.arg_count = argc;
                /* Attach method name to field node */
                ASTNode* fa = ast_new(AST_FIELD_ACCESS, loc);
                fa->as.field.object = left;
                tok_copy(fa->as.field.field, sizeof(fa->as.field.field), field_tok);
                n->as.call.callee = fa;
                left = n;
            } else {
                /* Field access: obj.field */
                ASTNode* n = ast_new(AST_FIELD_ACCESS, loc);
                n->as.field.object = left;
                tok_copy(n->as.field.field, sizeof(n->as.field.field), field_tok);
                left = n;
            }
        }
        /* Index: arr[i] */
        else if (p_match(p, TOK_LBRACKET)) {
            ASTNode* idx = parse_expr(p);
            p_expect(p, TOK_RBRACKET, "expected ']'");
            ASTNode* n = ast_new(AST_INDEX, loc);
            n->as.index.array = left;
            n->as.index.index = idx;
            left = n;
        }
        else {
            break;
        }
    }
    return left;
}

/* ─── Cast: expr as Type ──────────────────────────────────────────────────── */
static ASTNode* parse_cast(Parser* p) {
    ASTNode* left = parse_postfix(p);
    while (p_check(p, TOK_IDENT) &&
           p_peek(p)->length == 2 &&
           strncmp(p_peek(p)->start, "as", 2) == 0) {
        SourceLoc loc = p_loc(p);
        p_advance(p); /* 'as' */
        ASTNode* type = parse_type(p);
        ASTNode* n = ast_new(AST_CAST, loc);
        n->as.cast.expr = left;
        n->as.cast.type = type;
        left = n;
    }
    return left;
}

/* ─── Binary expression (Pratt-style precedence climbing) ─────────────────── */
static int bin_precedence(TokenType t) {
    switch (t) {
        case TOK_OR:      return 1;
        case TOK_AND:     return 2;
        case TOK_EQ:
        case TOK_NEQ:     return 3;
        case TOK_LT:
        case TOK_GT:
        case TOK_LTE:
        case TOK_GTE:     return 4;
        case TOK_RANGE:   return 5;
        case TOK_PLUS:
        case TOK_MINUS:   return 6;
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT: return 7;
        default:          return -1;
    }
}

static ASTNode* parse_binary(Parser* p, int min_prec) {
    ASTNode* left = parse_cast(p);

    while (true) {
        TokenType op = p_peek(p)->type;
        int prec = bin_precedence(op);
        if (prec < min_prec) break;
        SourceLoc loc = p_loc(p);
        p_advance(p);
        ASTNode* right = parse_binary(p, prec + 1);
        ASTNode* n = ast_new(AST_BINARY, loc);
        n->as.binary.left  = left;
        n->as.binary.right = right;
        n->as.binary.op    = op;
        left = n;
    }
    return left;
}

static ASTNode* parse_expr(Parser* p) {
    return parse_binary(p, 0);
}

/* ─── Match Statement ─────────────────────────────────────────────────────── */
static ASTNode* parse_match(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_expect(p, TOK_KW_MATCH, "expected 'match'");
    ASTNode* subject = parse_expr(p);
    p_expect(p, TOK_LBRACE, "expected '{'");

    ASTNode* n = ast_new(AST_MATCH, loc);
    n->as.match.subject = subject;

    size_t arm_cap = 8, arm_count = 0;
    ASTNode** arms = malloc(arm_cap * sizeof(ASTNode*));

    while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        if (arm_count >= arm_cap) {
            arm_cap *= 2;
            arms = realloc(arms, arm_cap * sizeof(ASTNode*));
        }

        SourceLoc arm_loc = p_loc(p);
        ASTNode* arm = ast_new(AST_MATCH_ARM, arm_loc);

        /* Pattern: Ok(x), Err(x), Some(x), None, _, ident, integer, bool */
        if (p_check(p, TOK_KW_OK) || p_check(p, TOK_KW_ERR) || p_check(p, TOK_KW_SOME)) {
            TokenType kind = p_advance(p)->type;
            arm->as.arm.pattern = ast_new(
                kind == TOK_KW_OK ? AST_OK : kind == TOK_KW_ERR ? AST_ERR : AST_SOME,
                arm_loc);
            if (p_match(p, TOK_LPAREN)) {
                Token* bind = p_expect(p, TOK_IDENT, "expected binding variable");
                tok_copy(arm->as.arm.bind, sizeof(arm->as.arm.bind), bind);
                p_expect(p, TOK_RPAREN, "expected ')'");
            }
        } else if (p_check(p, TOK_KW_NONE)) {
            p_advance(p);
            arm->as.arm.pattern = ast_new(AST_NONE_LIT, arm_loc);
        } else if (p_check(p, TOK_IDENT) &&
                   p_peek(p)->length == 1 && p_peek(p)->start[0] == '_') {
            p_advance(p);
            arm->as.arm.pattern = ast_new(AST_WILDCARD, arm_loc);
        } else {
            arm->as.arm.pattern = parse_expr(p);
        }

        /* -> body */
        p_expect(p, TOK_ARROW, "expected '->' in match arm");

        /* Body: block or single expr */
        if (p_check(p, TOK_LBRACE)) {
            arm->as.arm.body = parse_block(p);
        } else {
            arm->as.arm.body = parse_expr(p);
        }

        arms[arm_count++] = arm;
        p_match(p, TOK_COMMA);
    }

    p_expect(p, TOK_RBRACE, "expected '}' to close match");
    n->as.match.arms      = arms;
    n->as.match.arm_count = arm_count;
    return n;
}

/* ─── Block ───────────────────────────────────────────────────────────────── */
static ASTNode* parse_block(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_expect(p, TOK_LBRACE, "expected '{'");

    ASTNode* block = ast_new(AST_BLOCK, loc);

    while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        ASTNode* stmt = parse_stmt(p);
        if (stmt) ast_list_push(block, stmt);
    }

    p_expect(p, TOK_RBRACE, "expected '}'");
    return block;
}

/* ─── Statements ──────────────────────────────────────────────────────────── */
static ASTNode* parse_stmt(Parser* p) {
    SourceLoc loc = p_loc(p);

    /* return */
    if (p_match(p, TOK_KW_RETURN)) {
        ASTNode* n = ast_new(AST_RETURN, loc);
        if (!p_check(p, TOK_SEMICOLON) && !p_check(p, TOK_RBRACE))
            n->as.ret.value = parse_expr(p);
        p_match(p, TOK_SEMICOLON);
        return n;
    }

    /* if */
    if (p_match(p, TOK_KW_IF)) {
        ASTNode* n = ast_new(AST_IF, loc);
        n->as.if_stmt.cond = parse_expr(p);
        n->as.if_stmt.then_block = parse_block(p);
        if (p_match(p, TOK_KW_ELSE)) {
            if (p_check(p, TOK_KW_IF))
                n->as.if_stmt.else_block = parse_stmt(p);
            else
                n->as.if_stmt.else_block = parse_block(p);
        }
        return n;
    }

    /* while */
    if (p_match(p, TOK_KW_WHILE)) {
        ASTNode* n = ast_new(AST_WHILE, loc);
        n->as.while_loop.cond = parse_expr(p);
        n->as.while_loop.body = parse_block(p);
        return n;
    }

    /* for */
    if (p_match(p, TOK_KW_FOR)) {
        if (p_match(p, TOK_LPAREN)) {
            /* C-style: for (init; cond; step) */
            ASTNode* n = ast_new(AST_FOR_C, loc);
            /* Parse as 3 stmts in a block — simplified */
            ASTNode* block = ast_new(AST_BLOCK, loc);
            ast_list_push(block, parse_stmt(p)); /* init */
            ast_list_push(block, parse_expr(p));  /* cond */
            p_match(p, TOK_SEMICOLON);
            ast_list_push(block, parse_expr(p));  /* step */
            p_expect(p, TOK_RPAREN, "expected ')'");
            n->as.while_loop.cond = block;
            n->as.while_loop.body = parse_block(p);
            return n;
        } else {
            /* Range: for var in lo..hi */
            ASTNode* n = ast_new(AST_FOR_RANGE, loc);
            Token* var = p_expect(p, TOK_IDENT, "expected loop variable");
            tok_copy(n->as.for_range.var, sizeof(n->as.for_range.var), var);
            p_expect(p, TOK_KW_IN, "expected 'in'");
            n->as.for_range.start = parse_expr(p);
            if (p_match(p, TOK_RANGE)) {
                n->as.for_range.end = parse_expr(p);
            }
            n->as.for_range.body = parse_block(p);
            return n;
        }
    }

    /* match */
    if (p_check(p, TOK_KW_MATCH)) {
        return parse_match(p);
    }

    /* unsafe block */
    if (p_match(p, TOK_KW_UNSAFE)) {
        ASTNode* n = ast_new(AST_UNSAFE_BLOCK, loc);
        n->as.list.items = NULL;
        ASTNode* body = parse_block(p);
        /* Re-use block contents */
        *n = *body;
        n->kind = AST_UNSAFE_BLOCK;
        free(body);
        return n;
    }

    /* Variable declaration: type name = expr; or auto name = expr; */
    if (p_is_type(p)) {
        /* Save position — might be type start or expression start */
        size_t saved = p->ts->position;
        bool is_decl = false;

        /* ── Heuristic lookahead: skip the type tokens then check for IDENT ── */
        /* This handles: i32, f64, str, u8*, str[], Option<T>, Person, etc. */
        {
            size_t tmp = p->ts->position;

            /* auto / const are always decls */
            if (p->ts->tokens[tmp].type == TOK_KW_AUTO ||
                p->ts->tokens[tmp].type == TOK_KW_CONST) {
                is_decl = true;
            } else {
                /* Skip the type: primitive, named (IDENT), or backtick-borrow */

                /* Skip leading backtick(s) for borrow types like `Options */
                while (tmp < p->ts->count && p->ts->tokens[tmp].type == TOK_BACKTICK)
                    tmp++;
                /* skip 'mut' after backtick */
                if (tmp < p->ts->count && p->ts->tokens[tmp].type == TOK_KW_MUT)
                    tmp++;

                /* Now at the base type: primitive keyword or named IDENT */
                if (tmp < p->ts->count &&
                    (p->ts->tokens[tmp].type >= TOK_TYPE_I8 &&
                     p->ts->tokens[tmp].type <= TOK_TYPE_VOID)) {
                    tmp++;  /* consume primitive type */
                } else if (tmp < p->ts->count &&
                           p->ts->tokens[tmp].type == TOK_IDENT) {
                    tmp++;  /* consume named type (e.g. Person, Options) */
                    /* Skip generic type params: <T, U> */
                    if (tmp < p->ts->count && p->ts->tokens[tmp].type == TOK_LT) {
                        int depth = 1; tmp++;
                        while (tmp < p->ts->count && depth > 0) {
                            if (p->ts->tokens[tmp].type == TOK_LT)  depth++;
                            if (p->ts->tokens[tmp].type == TOK_GT)  depth--;
                            tmp++;
                        }
                    }
                } else {
                    /* Not a recognized type start */
                    goto not_decl;
                }

                /* Skip pointer/array suffixes: *, [], []  */
                while (tmp < p->ts->count &&
                       (p->ts->tokens[tmp].type == TOK_STAR ||
                        p->ts->tokens[tmp].type == TOK_LBRACKET)) {
                    if (p->ts->tokens[tmp].type == TOK_LBRACKET) {
                        tmp++; /* skip [ */
                        if (tmp < p->ts->count &&
                            p->ts->tokens[tmp].type == TOK_RBRACKET) tmp++; /* skip ] */
                    } else {
                        tmp++; /* skip * */
                    }
                }

                /* Now we should be at the variable name */
                if (tmp < p->ts->count && p->ts->tokens[tmp].type == TOK_IDENT) {
                    is_decl = true;
                }
            }
            not_decl:;
        }

        if (is_decl) {
            ASTNode* n = ast_new(AST_VAR_DECL, loc);
            bool is_const = p_match(p, TOK_KW_CONST);
            bool is_auto  = p_match(p, TOK_KW_AUTO);
            (void)is_const;

            if (!is_auto) n->as.var_decl.type = parse_type(p);

            Token* name_tok = p_expect(p, TOK_IDENT, "expected variable name");
            tok_copy(n->as.var_decl.name, sizeof(n->as.var_decl.name), name_tok);

            if (p_match(p, TOK_ASSIGN)) {
                /* Fix: if initializer is '{', check if it's a struct init.
                   Pattern: NamedType varname = { field: val, ... }
                   The struct type is the declared type (if it's a named type). */
                if (p_check(p, TOK_LBRACE)) {
                    /* Peek inside the { to see if it's field: val or just statements */
                    size_t peek_pos = p->ts->position + 1; /* skip { */
                    bool looks_like_struct = false;
                    if (peek_pos < p->ts->count) {
                        TokenType first = p->ts->tokens[peek_pos].type;
                        if (first == TOK_IDENT && peek_pos + 1 < p->ts->count &&
                            p->ts->tokens[peek_pos + 1].type == TOK_COLON) {
                            looks_like_struct = true;
                        }
                        if (first == TOK_RBRACE) {
                            looks_like_struct = true; /* empty struct init */
                        }
                    }

                    if (looks_like_struct && n->as.var_decl.type != NULL &&
                        n->as.var_decl.type->kind == AST_TYPE_NAMED) {
                        /* Parse as struct init: use the declared type name */
                        p_advance(p); /* consume '{' */
                        ASTNode* si = ast_new(AST_STRUCT_INIT, loc);
                        strncpy(si->as.struct_init.type_name,
                                n->as.var_decl.type->as.type.name,
                                sizeof(si->as.struct_init.type_name) - 1);
                        si->as.struct_init.type_name[sizeof(si->as.struct_init.type_name)-1] = '\0';

                        size_t si_cap = 8, si_count = 0;
                        char** names  = malloc(si_cap * sizeof(char*));
                        ASTNode** vals = malloc(si_cap * sizeof(ASTNode*));

                        while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
                            if (si_count >= si_cap) {
                                si_cap *= 2;
                                names = realloc(names, si_cap * sizeof(char*));
                                vals  = realloc(vals,  si_cap * sizeof(ASTNode*));
                            }
                            Token* fn = p_expect(p, TOK_IDENT, "expected field name");
                            char* fname = malloc(fn->length + 1);
                            memcpy(fname, fn->start, fn->length);
                            fname[fn->length] = '\0';
                            p_expect(p, TOK_COLON, "expected ':' after field name");
                            ASTNode* fval = parse_expr(p);
                            names[si_count]  = fname;
                            vals[si_count]   = fval;
                            si_count++;
                            if (!p_match(p, TOK_COMMA)) break;
                        }
                        p_expect(p, TOK_RBRACE, "expected '}' to close struct init");
                        si->as.struct_init.field_names   = names;
                        si->as.struct_init.field_values  = vals;
                        si->as.struct_init.field_count   = si_count;
                        n->as.var_decl.init = si;
                    } else {
                        /* Regular block expression */
                        n->as.var_decl.init = parse_expr(p);
                    }
                } else {
                    n->as.var_decl.init = parse_expr(p);
                }
            }
            p_match(p, TOK_SEMICOLON);
            return n;
        } else {
            p->ts->position = saved;
        }
    }

    /* Expression statement or assignment */
    ASTNode* expr = parse_expr(p);

    /* Assignment: lhs = rhs or lhs += rhs etc. */
    TokenType t = p_peek(p)->type;
    if (t == TOK_ASSIGN || t == TOK_PLUS_EQ || t == TOK_MINUS_EQ ||
        t == TOK_STAR_EQ || t == TOK_SLASH_EQ) {
        SourceLoc assign_loc = p_loc(p);
        TokenType op = p_advance(p)->type;
        ASTNode* rhs = parse_expr(p);
        p_match(p, TOK_SEMICOLON);
        ASTNode* n = ast_new(AST_ASSIGN, assign_loc);
        n->as.assign.lhs = expr;
        n->as.assign.rhs = rhs;
        n->as.assign.op  = op;
        return n;
    }

    p_match(p, TOK_SEMICOLON);
    ASTNode* stmt = ast_new(AST_EXPR_STMT, loc);
    stmt->as.unary.operand = expr;
    return stmt;
}

/* ─── Function Parameters ─────────────────────────────────────────────────── */
static void parse_params(Parser* p, ASTNode*** out_params, size_t* out_count) {
    size_t cap = 4, count = 0;
    ASTNode** params = malloc(cap * sizeof(ASTNode*));

    while (!p_check(p, TOK_RPAREN) && !p_check(p, TOK_EOF)) {
        if (count >= cap) { cap *= 2; params = realloc(params, cap * sizeof(ASTNode*)); }
        SourceLoc loc = p_loc(p);

        /* Variadic: ... */
        if (p_match(p, TOK_RANGE)) { p_match(p, TOK_DOT); break; }

        ASTNode* param = ast_new(AST_VAR_DECL, loc);
        param->as.var_decl.is_param = true;
        param->as.var_decl.type = parse_type(p);
        Token* name_tok = p_expect(p, TOK_IDENT, "expected parameter name");
        tok_copy(param->as.var_decl.name, sizeof(param->as.var_decl.name), name_tok);
        params[count++] = param;
        if (!p_match(p, TOK_COMMA)) break;
    }

    *out_params = params;
    *out_count  = count;
}

/* ─── Top-Level Items ─────────────────────────────────────────────────────── */
static ASTNode* parse_fn_def(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_expect(p, TOK_KW_FN, "expected 'fn'");

    ASTNode* n = ast_new(AST_FN_DEF, loc);

    Token* name_tok = p_expect(p, TOK_IDENT, "expected function name");
    tok_copy(n->as.fn_def.name, sizeof(n->as.fn_def.name), name_tok);

    /* Method: Type.method — dots in name */
    if (p_match(p, TOK_DOT)) {
        n->as.fn_def.is_method = true;
        memcpy(n->as.fn_def.type_name, n->as.fn_def.name,
               sizeof(n->as.fn_def.type_name));
        Token* method_tok = p_expect(p, TOK_IDENT, "expected method name");
        tok_copy(n->as.fn_def.name, sizeof(n->as.fn_def.name), method_tok);
    }

    /* Generic parameters: <T, U> — skip for bootstrap */
    if (p_match(p, TOK_LT)) {
        int depth = 1;
        while (!p_check(p, TOK_EOF) && depth > 0) {
            if (p_check(p, TOK_LT)) depth++;
            if (p_check(p, TOK_GT)) depth--;
            if (depth > 0) p_advance(p);
        }
        p_expect(p, TOK_GT, "expected '>'");
    }

    p_expect(p, TOK_LPAREN, "expected '('");
    parse_params(p, &n->as.fn_def.param_list, &n->as.fn_def.param_count);
    p_expect(p, TOK_RPAREN, "expected ')'");

    if (p_match(p, TOK_ARROW)) {
        n->as.fn_def.ret_type = parse_type(p);
    }

    n->as.fn_def.body = parse_block(p);
    return n;
}

static ASTNode* parse_struct_def(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_expect(p, TOK_KW_STRUCT, "expected 'struct'");
    ASTNode* n = ast_new(AST_STRUCT_DEF, loc);

    Token* name_tok = p_expect(p, TOK_IDENT, "expected struct name");
    tok_copy(n->as.struct_def.name, sizeof(n->as.struct_def.name), name_tok);

    /* Generic params — skip */
    if (p_match(p, TOK_LT)) {
        int depth = 1;
        while (!p_check(p, TOK_EOF) && depth > 0) {
            if (p_check(p, TOK_LT)) depth++;
            if (p_check(p, TOK_GT)) depth--;
            if (depth > 0) p_advance(p);
        }
        p_expect(p, TOK_GT, "expected '>'");
    }

    p_expect(p, TOK_LBRACE, "expected '{'");

    size_t cap = 8, count = 0;
    ASTNode** fields = malloc(cap * sizeof(ASTNode*));

    while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        if (count >= cap) { cap *= 2; fields = realloc(fields, cap * sizeof(ASTNode*)); }
        SourceLoc field_loc = p_loc(p);
        ASTNode* field = ast_new(AST_VAR_DECL, field_loc);
        field->as.var_decl.type = parse_type(p);
        Token* fn_tok = p_expect(p, TOK_IDENT, "expected field name");
        tok_copy(field->as.var_decl.name, sizeof(field->as.var_decl.name), fn_tok);
        p_match(p, TOK_SEMICOLON);
        fields[count++] = field;
    }

    p_expect(p, TOK_RBRACE, "expected '}'");
    n->as.struct_def.fields      = fields;
    n->as.struct_def.field_count = count;
    return n;
}

static ASTNode* parse_import(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_expect(p, TOK_KW_IMPORT, "expected 'import'");

    ASTNode* n = ast_new(AST_IMPORT, loc);
    char* dst = n->as.import.path;
    size_t space = sizeof(n->as.import.path) - 1;
    size_t written = 0;

    /* Import path: io or collections.vec or "local/file" */
    if (p_check(p, TOK_STRING_LIT)) {
        Token* t = p_advance(p);
        size_t len = t->length >= 2 ? t->length - 2 : 0;
        if (len > space) len = space;
        memcpy(dst, t->start + 1, len);
        written = len;
    } else {
        while ((p_check(p, TOK_IDENT) || p_check(p, TOK_DOT)) && written < space) {
            Token* t = p_advance(p);
            size_t chunk = t->length < (space - written) ? t->length : (space - written);
            memcpy(dst + written, t->start, chunk);
            written += chunk;
        }
    }
    dst[written] = '\0';
    p_match(p, TOK_SEMICOLON);
    return n;
}

static ASTNode* parse_const_def(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_expect(p, TOK_KW_CONST, "expected 'const'");
    ASTNode* n = ast_new(AST_CONST_DEF, loc);
    n->as.const_def.type  = parse_type(p);
    Token* name_tok = p_expect(p, TOK_IDENT, "expected const name");
    tok_copy(n->as.const_def.name, sizeof(n->as.const_def.name), name_tok);
    p_expect(p, TOK_ASSIGN, "expected '='");
    n->as.const_def.value = parse_expr(p);
    p_match(p, TOK_SEMICOLON);
    return n;
}

static ASTNode* parse_enum_def(Parser* p) {
    SourceLoc loc = p_loc(p);
    p_advance(p); /* 'enum' keyword (stored as IDENT in our lexer) */
    ASTNode* n = ast_new(AST_STRUCT_DEF, loc); /* treat enum as struct for now */
    if (p_check(p, TOK_IDENT)) {
        Token* t = p_advance(p);
        tok_copy(n->as.struct_def.name, sizeof(n->as.struct_def.name), t);
    }
    /* Skip generic */
    if (p_match(p, TOK_LT)) {
        int d = 1;
        while (!p_check(p, TOK_EOF) && d > 0) {
            if (p_check(p, TOK_LT)) d++;
            if (p_check(p, TOK_GT)) d--;
            if (d > 0) p_advance(p);
        }
        p_expect(p, TOK_GT, ">");
    }
    /* Skip body */
    if (p_match(p, TOK_LBRACE)) {
        int d = 1;
        while (!p_check(p, TOK_EOF) && d > 0) {
            if (p_check(p, TOK_LBRACE)) d++;
            if (p_check(p, TOK_RBRACE)) d--;
            if (d > 0) p_advance(p);
        }
        p_expect(p, TOK_RBRACE, "}");
    }
    return n;
}

/* ─── Program ─────────────────────────────────────────────────────────────── */
ASTNode* parse(TokenStream* ts) {
    Parser p = { .ts = ts, .had_error = false };
    SourceLoc root_loc = { .filename = ts->tokens[0].loc.filename, .line = 1, .column = 1 };
    ASTNode* program = ast_new(AST_PROGRAM, root_loc);

    while (!p_check(&p, TOK_EOF)) {
        ASTNode* item = NULL;

        if (p_check(&p, TOK_KW_FN))     item = parse_fn_def(&p);
        else if (p_check(&p, TOK_KW_STRUCT))  item = parse_struct_def(&p);
        else if (p_check(&p, TOK_KW_IMPORT))  item = parse_import(&p);
        else if (p_check(&p, TOK_KW_CONST))   item = parse_const_def(&p);
        else if (p_check(&p, TOK_IDENT) &&
                 p.ts->tokens[p.ts->position].length == 4 &&
                 strncmp(p.ts->tokens[p.ts->position].start, "enum", 4) == 0) {
            item = parse_enum_def(&p);
        }
        else {
            fprintf(stderr, "\033[31m[parser] %s:%d: unexpected top-level token '%.*s'\033[0m\n",
                    p_peek(&p)->loc.filename, p_peek(&p)->loc.line,
                    (int)p_peek(&p)->length, p_peek(&p)->start);
            p.had_error = true;
            p_advance(&p);
            continue;
        }

        if (item) ast_list_push(program, item);
    }

    if (p.had_error) { ast_free(program); return NULL; }
    return program;
}

/* ─── AST Dump ────────────────────────────────────────────────────────────── */
static const char* kind_name(ASTKind k) {
    switch (k) {
#define K(x) case x: return #x
        K(AST_PROGRAM); K(AST_FN_DEF); K(AST_STRUCT_DEF); K(AST_IMPORT);
        K(AST_BLOCK); K(AST_RETURN); K(AST_VAR_DECL); K(AST_ASSIGN);
        K(AST_IF); K(AST_WHILE); K(AST_MATCH); K(AST_MATCH_ARM);
        K(AST_INT_LIT); K(AST_FLOAT_LIT); K(AST_STRING_LIT); K(AST_BOOL_LIT);
        K(AST_NONE_LIT); K(AST_IDENT); K(AST_BORROW); K(AST_BORROW_MUT);
        K(AST_BINARY); K(AST_UNARY); K(AST_CALL); K(AST_METHOD_CALL);
        K(AST_FIELD_ACCESS); K(AST_INDEX); K(AST_CAST); K(AST_STRUCT_INIT);
        K(AST_SOME); K(AST_OK); K(AST_ERR); K(AST_WILDCARD);
        K(AST_TYPE_PRIM); K(AST_TYPE_REF); K(AST_TYPE_PTR); K(AST_TYPE_NAMED);
        K(AST_CONST_DEF); K(AST_FOR_RANGE); K(AST_EXPR_STMT); K(AST_VOID_EXPR);
#undef K
        default: return "AST_UNKNOWN";
    }
}

void ast_dump(const ASTNode* n, int indent) {
    if (!n) { printf("%*s(null)\n", indent * 2, ""); return; }
    printf("%*s[%s]", indent * 2, "", kind_name(n->kind));
    switch (n->kind) {
        case AST_IDENT:      printf(" \"%s\"", n->as.ident.name); break;
        case AST_INT_LIT:    printf(" %lld", n->as.lit.int_val); break;
        case AST_FLOAT_LIT:  printf(" %f", n->as.lit.float_val); break;
        case AST_STRING_LIT: printf(" \"%s\"", n->as.lit.str_val); break;
        case AST_BOOL_LIT:   printf(" %s", n->as.lit.bool_val ? "true" : "false"); break;
        case AST_FN_DEF:     printf(" %s()", n->as.fn_def.name); break;
        case AST_VAR_DECL:   printf(" %s", n->as.var_decl.name); break;
        case AST_IMPORT:     printf(" \"%s\"", n->as.import.path); break;
        case AST_BINARY:     printf(" op=%s", token_type_name(n->as.binary.op)); break;
        default: break;
    }
    printf("\n");
    /* Recurse into children */
    if (n->kind == AST_PROGRAM || n->kind == AST_BLOCK) {
        for (size_t i = 0; i < n->as.list.count; i++)
            ast_dump(n->as.list.items[i], indent + 1);
    }
    if (n->kind == AST_FN_DEF) {
        for (size_t i = 0; i < n->as.fn_def.param_count; i++)
            ast_dump(n->as.fn_def.param_list[i], indent + 1);
        ast_dump(n->as.fn_def.body, indent + 1);
    }
    if (n->kind == AST_BINARY) {
        ast_dump(n->as.binary.left, indent + 1);
        ast_dump(n->as.binary.right, indent + 1);
    }
}

/* ─── AST Free ────────────────────────────────────────────────────────────── */
void ast_free(ASTNode* n) {
    if (!n) return;
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
        case AST_UNSAFE_BLOCK:
            for (size_t i = 0; i < n->as.list.count; i++) ast_free(n->as.list.items[i]);
            free(n->as.list.items);
            break;
        case AST_FN_DEF:
            for (size_t i = 0; i < n->as.fn_def.param_count; i++)
                ast_free(n->as.fn_def.param_list[i]);
            free(n->as.fn_def.param_list);
            ast_free(n->as.fn_def.ret_type);
            ast_free(n->as.fn_def.body);
            break;
        case AST_STRUCT_DEF:
            for (size_t i = 0; i < n->as.struct_def.field_count; i++)
                ast_free(n->as.struct_def.fields[i]);
            free(n->as.struct_def.fields);
            break;
        case AST_VAR_DECL:
            ast_free(n->as.var_decl.type);
            ast_free(n->as.var_decl.init);
            break;
        case AST_BINARY:
            ast_free(n->as.binary.left);
            ast_free(n->as.binary.right);
            break;
        case AST_CALL:
        case AST_METHOD_CALL:
            ast_free(n->as.call.callee);
            for (size_t i = 0; i < n->as.call.arg_count; i++)
                ast_free(n->as.call.args[i]);
            free(n->as.call.args);
            break;
        case AST_IF:
            ast_free(n->as.if_stmt.cond);
            ast_free(n->as.if_stmt.then_block);
            ast_free(n->as.if_stmt.else_block);
            break;
        case AST_MATCH:
            ast_free(n->as.match.subject);
            for (size_t i = 0; i < n->as.match.arm_count; i++)
                ast_free(n->as.match.arms[i]);
            free(n->as.match.arms);
            break;
        case AST_MATCH_ARM:
            ast_free(n->as.arm.pattern);
            ast_free(n->as.arm.body);
            break;
        case AST_RETURN:       ast_free(n->as.ret.value); break;
        case AST_BORROW:
        case AST_BORROW_MUT:   ast_free(n->as.borrow.expr); break;
        case AST_UNARY:
        case AST_DEREF:
        case AST_EXPR_STMT:    ast_free(n->as.unary.operand); break;
        case AST_ASSIGN:
            ast_free(n->as.assign.lhs);
            ast_free(n->as.assign.rhs);
            break;
        case AST_FIELD_ACCESS:  ast_free(n->as.field.object); break;
        case AST_CAST:
            ast_free(n->as.cast.expr);
            ast_free(n->as.cast.type);
            break;
        case AST_STRUCT_INIT:
            for (size_t i = 0; i < n->as.struct_init.field_count; i++) {
                free(n->as.struct_init.field_names[i]);
                ast_free(n->as.struct_init.field_values[i]);
            }
            free(n->as.struct_init.field_names);
            free(n->as.struct_init.field_values);
            break;
        case AST_SOME:
        case AST_OK:
        case AST_ERR:          ast_free(n->as.wrap.inner); break;
        case AST_WHILE:
            ast_free(n->as.while_loop.cond);
            ast_free(n->as.while_loop.body);
            break;
        case AST_CONST_DEF:
            ast_free(n->as.const_def.type);
            ast_free(n->as.const_def.value);
            break;
        case AST_INDEX:
            ast_free(n->as.index.array);
            ast_free(n->as.index.index);
            break;
        case AST_TYPE_REF:
        case AST_TYPE_REF_MUT:
        case AST_TYPE_PTR:
        case AST_TYPE_ARRAY:   ast_free(n->as.type.inner); break;
        default: break;
    }
    free(n);
}
