/*
 * cpc — Token Definitions
 * compiler/src/lexer/token.cp
 * =============================
 * All token types, the Token struct, and the TokenStream
 * used throughout the compiler pipeline.
 */

import core;
import mem;

/* ─── Token Types ─────────────────────────────────────────────────────────── */
enum TokenKind {
    /* Literals */
    TK_INT, TK_FLOAT, TK_STRING, TK_CHAR, TK_BOOL,

    /* Identifiers & Keywords */
    TK_IDENT,
    TK_FN, TK_RETURN, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_IN,
    TK_MATCH, TK_STRUCT, TK_ENUM, TK_IMPORT, TK_CONST, TK_AUTO,
    TK_MUT, TK_UNSAFE, TK_ASM, TK_TRUE, TK_FALSE, TK_NONE,
    TK_SOME, TK_OK, TK_ERR, TK_AS, TK_BREAK, TK_CONTINUE,

    /* Primitive Types */
    TK_I8, TK_I16, TK_I32, TK_I64,
    TK_U8, TK_U16, TK_U32, TK_U64,
    TK_F32, TK_F64, TK_BOOL_T, TK_CHAR_T,
    TK_STR, TK_BYTE, TK_USIZE, TK_ISIZE, TK_VOID,

    /* Operators */
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LTE, TK_GTE,
    TK_AND, TK_OR, TK_NOT, TK_BAND, TK_BOR, TK_BXOR, TK_BNOT,
    TK_SHL, TK_SHR,
    TK_ASSIGN, TK_PLUS_EQ, TK_MINUS_EQ, TK_STAR_EQ, TK_SLASH_EQ,
    TK_PERCENT_EQ, TK_AND_EQ, TK_OR_EQ, TK_XOR_EQ, TK_SHL_EQ, TK_SHR_EQ,
    TK_INC, TK_DEC,
    TK_ARROW, TK_FAT_ARROW, TK_RANGE, TK_RANGE_EQ,
    TK_DOT, TK_DOTDOT, TK_COMMA, TK_COLON, TK_DCOLON, TK_SEMI,

    /* THE BORROW OPERATOR */
    TK_BACKTICK,

    /* Delimiters */
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET, TK_HASH, TK_AT,

    /* Special */
    TK_EOF, TK_INVALID,
}

/* ─── Source Location ─────────────────────────────────────────────────────── */
struct Span {
    str   file;
    u32   line;
    u32   col;
    u32   len;    /* byte length of token in source */
}

/* ─── Token ───────────────────────────────────────────────────────────────── */
struct Token {
    TokenKind kind;
    Span      span;
    str       text;       /* raw source text of token */
    /* Parsed literal values */
    i64       int_val;
    f64       float_val;
    bool      bool_val;
}

/* ─── Token Stream ────────────────────────────────────────────────────────── */
struct TokenStream {
    Token[]  tokens;
    usize    len;
    usize    cap;
    usize    pos;         /* current read cursor */
}

fn TokenStream.new() -> TokenStream {
    return TokenStream {
        tokens: null,
        len: 0, cap: 0, pos: 0,
    };
}

fn TokenStream.peek(`TokenStream self) -> `Token {
    if self.pos < self.len { return `self.tokens[self.pos]; }
    return `self.tokens[self.len - 1]; /* EOF */
}

fn TokenStream.peek2(`TokenStream self) -> `Token {
    if self.pos + 1 < self.len { return `self.tokens[self.pos + 1]; }
    return `self.tokens[self.len - 1];
}

fn TokenStream.advance(`mut TokenStream self) -> `Token {
    `Token t = self.peek();
    if self.pos < self.len { self.pos = self.pos + 1; }
    return t;
}

fn TokenStream.check(`TokenStream self, TokenKind kind) -> bool {
    return self.peek().kind == kind;
}

fn TokenStream.match_kind(`mut TokenStream self, TokenKind kind) -> bool {
    if self.check(kind) { self.advance(); return true; }
    return false;
}

fn TokenStream.expect(`mut TokenStream self, TokenKind kind, `str ctx) -> Result<`Token, str> {
    if self.check(kind) { return Ok(self.advance()); }
    `Token got = self.peek();
    return Err(fmt.sprintf("[%s:%d] expected token kind, got '%s'",
                            got.span.file, got.span.line, got.text));
}

fn Token.is_type_keyword(`Token self) -> bool {
    match self.kind {
        TK_I8    -> return true,
        TK_I16   -> return true,
        TK_I32   -> return true,
        TK_I64   -> return true,
        TK_U8    -> return true,
        TK_U16   -> return true,
        TK_U32   -> return true,
        TK_U64   -> return true,
        TK_F32   -> return true,
        TK_F64   -> return true,
        TK_BOOL_T-> return true,
        TK_CHAR_T-> return true,
        TK_STR   -> return true,
        TK_BYTE  -> return true,
        TK_USIZE -> return true,
        TK_ISIZE -> return true,
        TK_VOID  -> return true,
        _        -> return false,
    }
}

fn Token.is_literal(`Token self) -> bool {
    match self.kind {
        TK_INT    -> return true,
        TK_FLOAT  -> return true,
        TK_STRING -> return true,
        TK_CHAR   -> return true,
        TK_TRUE   -> return true,
        TK_FALSE  -> return true,
        TK_NONE   -> return true,
        _         -> return false,
    }
}

fn token_kind_name(TokenKind k) -> str {
    match k {
        TK_INT      -> return "int_literal",
        TK_FLOAT    -> return "float_literal",
        TK_STRING   -> return "string_literal",
        TK_IDENT    -> return "identifier",
        TK_BACKTICK -> return "` (borrow)",
        TK_ARROW    -> return "->",
        TK_EOF      -> return "EOF",
        _           -> return "token",
    }
}
