/*
 * C-Prime Bootstrap Lexer — lexer.c
 * ===================================
 * Tokenizes C-Prime source code (.cp files) into a flat token stream.
 * This is the first stage of the bootstrap compiler pipeline.
 *
 * Supports all C-Prime syntax needed to compile compiler/src/main.cp:
 *   - All keywords (fn, struct, import, const, if, else, while, for,
 *     in, match, return, auto, mut, unsafe, asm, true, false)
 *   - All primitive types (i8..i64, u8..u64, f32, f64, bool, str, etc.)
 *   - The backtick borrow operator
 *   - String/char/integer/float/hex/binary literals
 *   - All operators and punctuation
 *   - Line/block comments
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../include/lexer.h"

/* ─── Keyword Table ───────────────────────────────────────────────────────── */
typedef struct { const char* word; TokenType type; } Keyword;

static const Keyword KEYWORDS[] = {
    /* Control flow */
    { "if",       TOK_KW_IF       },
    { "else",     TOK_KW_ELSE     },
    { "while",    TOK_KW_WHILE    },
    { "for",      TOK_KW_FOR      },
    { "in",       TOK_KW_IN       },
    { "return",   TOK_KW_RETURN   },
    { "match",    TOK_KW_MATCH    },
    { "break",    TOK_IDENT       }, /* reserved, treated as ident for now */
    { "continue", TOK_IDENT       },
    /* Declarations */
    { "fn",       TOK_KW_FN       },
    { "struct",   TOK_KW_STRUCT   },
    { "enum",     TOK_IDENT       }, /* partial support */
    { "import",   TOK_KW_IMPORT   },
    { "const",    TOK_KW_CONST    },
    { "auto",     TOK_KW_AUTO     },
    { "mut",      TOK_KW_MUT      },
    { "unsafe",   TOK_KW_UNSAFE   },
    { "asm",      TOK_KW_ASM      },
    /* Literals */
    { "true",     TOK_KW_TRUE     },
    { "false",    TOK_KW_FALSE    },
    { "None",     TOK_KW_NONE     },
    { "Some",     TOK_KW_SOME     },
    { "Ok",       TOK_KW_OK       },
    { "Err",      TOK_KW_ERR      },
    /* Types */
    { "i8",       TOK_TYPE_I8     },
    { "i16",      TOK_TYPE_I16    },
    { "i32",      TOK_TYPE_I32    },
    { "i64",      TOK_TYPE_I64    },
    { "u8",       TOK_TYPE_U8     },
    { "u16",      TOK_TYPE_U16    },
    { "u32",      TOK_TYPE_U32    },
    { "u64",      TOK_TYPE_U64    },
    { "f32",      TOK_TYPE_F32    },
    { "f64",      TOK_TYPE_F64    },
    { "bool",     TOK_TYPE_BOOL   },
    { "char",     TOK_TYPE_CHAR   },
    { "str",      TOK_TYPE_STR    },
    { "byte",     TOK_TYPE_BYTE   },
    { "usize",    TOK_TYPE_USIZE  },
    { "isize",    TOK_TYPE_ISIZE  },
    { "void",     TOK_TYPE_VOID   },
    { NULL,       TOK_INVALID     }
};

/* ─── Lexer State ─────────────────────────────────────────────────────────── */
typedef struct {
    const char* src;       /* Source buffer */
    size_t      len;       /* Source length */
    size_t      pos;       /* Current position */
    int         line;      /* Current line (1-based) */
    int         col;       /* Current column (1-based) */
    const char* filename;  /* For error messages */
} Lexer;

/* ─── Lexer Helpers ───────────────────────────────────────────────────────── */
static char lex_current(Lexer* l) {
    return (l->pos < l->len) ? l->src[l->pos] : '\0';
}

static char lex_peek(Lexer* l) {
    return (l->pos + 1 < l->len) ? l->src[l->pos + 1] : '\0';
}

static char lex_advance(Lexer* l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++; }
    return c;
}

static bool lex_match(Lexer* l, char expected) {
    if (l->pos < l->len && l->src[l->pos] == expected) {
        lex_advance(l);
        return true;
    }
    return false;
}

static void lex_skip_whitespace(Lexer* l) {
    while (l->pos < l->len) {
        char c = lex_current(l);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lex_advance(l);
        } else if (c == '/' && lex_peek(l) == '/') {
            /* Line comment — skip to end of line */
            while (l->pos < l->len && lex_current(l) != '\n')
                lex_advance(l);
        } else if (c == '/' && lex_peek(l) == '*') {
            /* Block comment */
            lex_advance(l); lex_advance(l); /* consume '/' '*' */
            while (l->pos + 1 < l->len) {
                if (lex_current(l) == '*' && lex_peek(l) == '/') {
                    lex_advance(l); lex_advance(l);
                    break;
                }
                lex_advance(l);
            }
        } else {
            break;
        }
    }
}

static void lex_error(Lexer* l, const char* msg) {
    fprintf(stderr, "\033[31m[lexer] %s:%d:%d: error: %s\033[0m\n",
            l->filename, l->line, l->col, msg);
}

/* ─── Token Stream Growth ─────────────────────────────────────────────────── */
static void ts_push(TokenStream* ts, Token tok) {
    if (ts->count >= ts->capacity) {
        size_t new_cap = ts->capacity == 0 ? 256 : ts->capacity * 2;
        ts->tokens = realloc(ts->tokens, new_cap * sizeof(Token));
        if (!ts->tokens) {
            fprintf(stderr, "[lexer] out of memory\n");
            exit(1);
        }
        ts->capacity = new_cap;
    }
    ts->tokens[ts->count++] = tok;
}

static Token make_tok(Lexer* l, TokenType type, const char* start, size_t len) {
    Token t = {0};
    t.type   = type;
    t.start  = start;
    t.length = len;
    t.loc.filename = l->filename;
    t.loc.line   = l->line;
    t.loc.column = l->col;
    return t;
}

/* ─── String Literal Parsing ──────────────────────────────────────────────── */
static Token lex_string(Lexer* l) {
    const char* start = &l->src[l->pos - 1]; /* includes the opening " */
    while (l->pos < l->len && lex_current(l) != '"') {
        if (lex_current(l) == '\\') lex_advance(l); /* skip escape */
        lex_advance(l);
    }
    if (l->pos >= l->len) {
        lex_error(l, "unterminated string literal");
    } else {
        lex_advance(l); /* closing " */
    }
    size_t len = (size_t)(&l->src[l->pos] - start);
    return make_tok(l, TOK_STRING_LIT, start, len);
}

/* ─── Char Literal Parsing ────────────────────────────────────────────────── */
static Token lex_char(Lexer* l) {
    const char* start = &l->src[l->pos - 1];
    if (lex_current(l) == '\\') lex_advance(l);
    lex_advance(l); /* the char itself */
    if (lex_current(l) != '\'') {
        lex_error(l, "unterminated char literal");
    } else {
        lex_advance(l); /* closing ' */
    }
    return make_tok(l, TOK_CHAR_LIT, start, (size_t)(&l->src[l->pos] - start));
}

/* ─── Number Literal Parsing ──────────────────────────────────────────────── */
static Token lex_number(Lexer* l, const char* start) {
    TokenType type = TOK_INT_LIT;
    long long int_val = 0;

    /* Hex: 0x... */
    if (lex_current(l) == 'x' || lex_current(l) == 'X') {
        lex_advance(l);
        while (isxdigit((unsigned char)lex_current(l)) || lex_current(l) == '_')
            lex_advance(l);
        int_val = strtoll(start, NULL, 16);
    }
    /* Binary: 0b... */
    else if (lex_current(l) == 'b' || lex_current(l) == 'B') {
        lex_advance(l);
        while (lex_current(l) == '0' || lex_current(l) == '1' || lex_current(l) == '_')
            lex_advance(l);
        /* parse binary */
        const char* p = start + 2;
        while (*p) { int_val = int_val * 2 + (*p == '1'); p++; }
    }
    /* Decimal / Float */
    else {
        while (isdigit((unsigned char)lex_current(l)) || lex_current(l) == '_')
            lex_advance(l);
        /* Float? */
        if (lex_current(l) == '.' && isdigit((unsigned char)lex_peek(l))) {
            type = TOK_FLOAT_LIT;
            lex_advance(l); /* . */
            while (isdigit((unsigned char)lex_current(l))) lex_advance(l);
        }
        int_val = strtoll(start, NULL, 10);
    }

    /* Optional suffix: i32, u64, f32, etc. */
    if (isalpha((unsigned char)lex_current(l))) {
        while (isalnum((unsigned char)lex_current(l))) lex_advance(l);
    }

    size_t len = (size_t)(&l->src[l->pos] - start);
    Token t = make_tok(l, type, start, len);
    t.value.int_val = int_val;
    return t;
}

/* ─── Identifier / Keyword ────────────────────────────────────────────────── */
static Token lex_ident(Lexer* l, const char* start) {
    while (isalnum((unsigned char)lex_current(l)) || lex_current(l) == '_')
        lex_advance(l);

    size_t len = (size_t)(&l->src[l->pos] - start);

    /* Check keyword table */
    for (int i = 0; KEYWORDS[i].word != NULL; i++) {
        if (strlen(KEYWORDS[i].word) == len &&
            strncmp(KEYWORDS[i].word, start, len) == 0) {
            Token t = make_tok(l, KEYWORDS[i].type, start, len);
            if (KEYWORDS[i].type == TOK_KW_TRUE)  t.value.bool_val = true;
            if (KEYWORDS[i].type == TOK_KW_FALSE) t.value.bool_val = false;
            return t;
        }
    }

    return make_tok(l, TOK_IDENT, start, len);
}

/* ─── Main Lex Function ───────────────────────────────────────────────────── */
TokenStream* lex(const char* source, size_t len, const char* filename) {
    Lexer l = {
        .src      = source,
        .len      = len,
        .pos      = 0,
        .line     = 1,
        .col      = 1,
        .filename = filename,
    };

    TokenStream* ts = calloc(1, sizeof(TokenStream));
    if (!ts) return NULL;

    bool had_error = false;

    while (true) {
        lex_skip_whitespace(&l);
        if (l.pos >= l.len) break;

        int tok_line = l.line;
        int tok_col  = l.col;
        const char* start = &l.src[l.pos];
        char c = lex_advance(&l);

        Token tok = {0};
        tok.loc.filename = filename;
        tok.loc.line     = tok_line;
        tok.loc.column   = tok_col;
        tok.start        = start;

#define SIMPLE(ch, ty) case ch: tok = make_tok(&l, ty, start, 1); break

        switch (c) {
            /* ── The Borrow Operator ── */
            case '`': tok = make_tok(&l, TOK_BACKTICK, start, 1); break;

            /* ── Single-char tokens ── */
            SIMPLE('(', TOK_LPAREN);
            SIMPLE(')', TOK_RPAREN);
            SIMPLE('{', TOK_LBRACE);
            SIMPLE('}', TOK_RBRACE);
            SIMPLE('[', TOK_LBRACKET);
            SIMPLE(']', TOK_RBRACKET);
            SIMPLE(';', TOK_SEMICOLON);
            SIMPLE(',', TOK_COMMA);
            SIMPLE('@', TOK_AT);
            SIMPLE('#', TOK_HASH);

            /* ── Colon ── */
            case ':':
                tok = make_tok(&l, TOK_COLON, start,
                               lex_match(&l, ':') ? 2 : 1);
                break;

            /* ── Dot / Range ── */
            case '.':
                if (lex_match(&l, '.')) tok = make_tok(&l, TOK_RANGE, start, 2);
                else                    tok = make_tok(&l, TOK_DOT,   start, 1);
                break;

            /* ── Arrow / Minus / Minus-assign / Decrement ── */
            case '-':
                if      (lex_match(&l, '>')) tok = make_tok(&l, TOK_ARROW,     start, 2);
                else if (lex_match(&l, '=')) tok = make_tok(&l, TOK_MINUS_EQ,  start, 2);
                else if (lex_match(&l, '-')) tok = make_tok(&l, TOK_DECREMENT, start, 2);
                else                         tok = make_tok(&l, TOK_MINUS,      start, 1);
                break;

            /* ── Plus ── */
            case '+':
                if      (lex_match(&l, '=')) tok = make_tok(&l, TOK_PLUS_EQ,   start, 2);
                else if (lex_match(&l, '+')) tok = make_tok(&l, TOK_INCREMENT, start, 2);
                else                         tok = make_tok(&l, TOK_PLUS,       start, 1);
                break;

            /* ── Star ── */
            case '*':
                if (lex_match(&l, '=')) tok = make_tok(&l, TOK_STAR_EQ, start, 2);
                else                    tok = make_tok(&l, TOK_STAR,    start, 1);
                break;

            /* ── Slash (comments already consumed above) ── */
            case '/':
                if (lex_match(&l, '=')) tok = make_tok(&l, TOK_SLASH_EQ, start, 2);
                else                    tok = make_tok(&l, TOK_SLASH,    start, 1);
                break;

            /* ── Percent ── */
            SIMPLE('%', TOK_PERCENT);

            /* ── Comparison ── */
            case '=':
                if      (lex_match(&l, '=')) tok = make_tok(&l, TOK_EQ,           start, 2);
                else if (lex_match(&l, '>')) tok = make_tok(&l, TOK_DOUBLE_ARROW, start, 2);
                else                         tok = make_tok(&l, TOK_ASSIGN,        start, 1);
                break;
            case '!':
                if (lex_match(&l, '=')) tok = make_tok(&l, TOK_NEQ, start, 2);
                else                    tok = make_tok(&l, TOK_NOT, start, 1);
                break;
            case '<':
                if (lex_match(&l, '=')) tok = make_tok(&l, TOK_LTE,    start, 2);
                else                    tok = make_tok(&l, TOK_LT,     start, 1);
                break;
            case '>':
                if (lex_match(&l, '=')) tok = make_tok(&l, TOK_GTE,    start, 2);
                else                    tok = make_tok(&l, TOK_GT,     start, 1);
                break;

            /* ── Logical ── */
            case '&':
                if (lex_match(&l, '&')) tok = make_tok(&l, TOK_AND, start, 2);
                else {
                    lex_error(&l, "unexpected '&' — did you mean '&&'?");
                    tok = make_tok(&l, TOK_INVALID, start, 1);
                    had_error = true;
                }
                break;
            case '|':
                if (lex_match(&l, '|')) tok = make_tok(&l, TOK_OR, start, 2);
                else {
                    /* Single '|' used in match arms — treat as INVALID for now */
                    tok = make_tok(&l, TOK_INVALID, start, 1);
                }
                break;

            /* ── Strings ── */
            case '"':  tok = lex_string(&l); break;
            case '\'': tok = lex_char(&l);   break;

            /* ── Numbers ── */
            default:
                if (isdigit((unsigned char)c)) {
                    tok = lex_number(&l, start);
                } else if (isalpha((unsigned char)c) || c == '_') {
                    tok = lex_ident(&l, start);
                } else {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "unexpected character: '%c' (0x%02x)", c, (unsigned char)c);
                    lex_error(&l, msg);
                    tok = make_tok(&l, TOK_INVALID, start, 1);
                    had_error = true;
                }
                break;
        }

        ts_push(ts, tok);
    }

    /* Emit EOF */
    Token eof = {0};
    eof.type = TOK_EOF;
    eof.loc.filename = filename;
    eof.loc.line     = l.line;
    eof.loc.column   = l.col;
    eof.start        = &l.src[l.pos];
    eof.length       = 0;
    ts_push(ts, eof);

    if (had_error) {
        token_stream_free(ts);
        return NULL;
    }

    return ts;
}

/* ─── Token Stream API ────────────────────────────────────────────────────── */
Token* token_stream_peek(TokenStream* ts) {
    if (ts->position < ts->count) return &ts->tokens[ts->position];
    return &ts->tokens[ts->count - 1]; /* EOF */
}

Token* token_stream_consume(TokenStream* ts) {
    Token* t = token_stream_peek(ts);
    if (ts->position < ts->count) ts->position++;
    return t;
}

bool token_stream_check(const TokenStream* ts, TokenType type) {
    if (ts->position < ts->count)
        return ts->tokens[ts->position].type == type;
    return type == TOK_EOF;
}

void token_stream_free(TokenStream* ts) {
    if (ts) {
        free(ts->tokens);
        free(ts);
    }
}

/* ─── Debug Dump ──────────────────────────────────────────────────────────── */
const char* token_type_name(TokenType t) {
    switch (t) {
#define N(x) case x: return #x
        N(TOK_INT_LIT); N(TOK_FLOAT_LIT); N(TOK_STRING_LIT); N(TOK_CHAR_LIT);
        N(TOK_BOOL_LIT); N(TOK_IDENT);
        N(TOK_KW_FN); N(TOK_KW_RETURN); N(TOK_KW_IF); N(TOK_KW_ELSE);
        N(TOK_KW_WHILE); N(TOK_KW_FOR); N(TOK_KW_IN); N(TOK_KW_MATCH);
        N(TOK_KW_STRUCT); N(TOK_KW_IMPORT); N(TOK_KW_CONST); N(TOK_KW_AUTO);
        N(TOK_KW_MUT); N(TOK_KW_UNSAFE); N(TOK_KW_ASM);
        N(TOK_KW_TRUE); N(TOK_KW_FALSE); N(TOK_KW_NONE); N(TOK_KW_SOME);
        N(TOK_KW_OK); N(TOK_KW_ERR);
        N(TOK_TYPE_I32); N(TOK_TYPE_I64); N(TOK_TYPE_U8); N(TOK_TYPE_U64);
        N(TOK_TYPE_F64); N(TOK_TYPE_BOOL); N(TOK_TYPE_STR); N(TOK_TYPE_VOID);
        N(TOK_TYPE_USIZE); N(TOK_TYPE_ISIZE);
        N(TOK_PLUS); N(TOK_MINUS); N(TOK_STAR); N(TOK_SLASH); N(TOK_PERCENT);
        N(TOK_EQ); N(TOK_NEQ); N(TOK_LT); N(TOK_GT); N(TOK_LTE); N(TOK_GTE);
        N(TOK_AND); N(TOK_OR); N(TOK_NOT); N(TOK_ASSIGN);
        N(TOK_ARROW); N(TOK_RANGE); N(TOK_DOT); N(TOK_COMMA); N(TOK_COLON);
        N(TOK_SEMICOLON); N(TOK_BACKTICK);
        N(TOK_LPAREN); N(TOK_RPAREN); N(TOK_LBRACE); N(TOK_RBRACE);
        N(TOK_LBRACKET); N(TOK_RBRACKET);
        N(TOK_INCREMENT); N(TOK_DECREMENT);
        N(TOK_EOF); N(TOK_INVALID);
#undef N
        default: return "TOK_UNKNOWN";
    }
}

void token_stream_dump(const TokenStream* ts) {
    printf("=== Token Stream (%zu tokens) ===\n", ts->count);
    for (size_t i = 0; i < ts->count; i++) {
        Token* t = &ts->tokens[i];
        printf("[%4zu] %-20s %3d:%-3d  |%.*s|\n",
               i,
               token_type_name(t->type),
               t->loc.line,
               t->loc.column,
               (int)t->length,
               t->start);
    }
}
