/*
 * C-Prime Bootstrap Lexer — lexer.h
 * Tokenizes C-Prime source code (.cp files)
 */

#ifndef CPRIME_LEXER_H
#define CPRIME_LEXER_H

#include <stddef.h>
#include <stdbool.h>

/* ─── Token Types ─────────────────────────────────────────────────────────── */
typedef enum {
    /* Literals */
    TOK_INT_LIT,       /* 42, 0xFF, 0b1010 */
    TOK_FLOAT_LIT,     /* 3.14, 2.0f */
    TOK_STRING_LIT,    /* "hello" */
    TOK_CHAR_LIT,      /* 'A' */
    TOK_BOOL_LIT,      /* true, false */

    /* Identifiers & Keywords */
    TOK_IDENT,         /* foo, bar, my_var */
    TOK_KW_FN,         /* fn */
    TOK_KW_RETURN,     /* return */
    TOK_KW_IF,         /* if */
    TOK_KW_ELSE,       /* else */
    TOK_KW_WHILE,      /* while */
    TOK_KW_FOR,        /* for */
    TOK_KW_IN,         /* in */
    TOK_KW_MATCH,      /* match */
    TOK_KW_STRUCT,     /* struct */
    TOK_KW_IMPORT,     /* import */
    TOK_KW_CONST,      /* const */
    TOK_KW_AUTO,       /* auto */
    TOK_KW_MUT,        /* mut */
    TOK_KW_UNSAFE,     /* unsafe */
    TOK_KW_ASM,        /* asm */
    TOK_KW_TRUE,       /* true */
    TOK_KW_FALSE,      /* false */
    TOK_KW_NONE,       /* None */
    TOK_KW_SOME,       /* Some */
    TOK_KW_OK,         /* Ok */
    TOK_KW_ERR,        /* Err */

    /* Types */
    TOK_TYPE_I8,       /* i8 */
    TOK_TYPE_I16,      /* i16 */
    TOK_TYPE_I32,      /* i32 */
    TOK_TYPE_I64,      /* i64 */
    TOK_TYPE_U8,       /* u8 */
    TOK_TYPE_U16,      /* u16 */
    TOK_TYPE_U32,      /* u32 */
    TOK_TYPE_U64,      /* u64 */
    TOK_TYPE_F32,      /* f32 */
    TOK_TYPE_F64,      /* f64 */
    TOK_TYPE_BOOL,     /* bool */
    TOK_TYPE_CHAR,     /* char */
    TOK_TYPE_STR,      /* str */
    TOK_TYPE_BYTE,     /* byte */
    TOK_TYPE_USIZE,    /* usize */
    TOK_TYPE_ISIZE,    /* isize */
    TOK_TYPE_VOID,     /* void */

    /* Operators */
    TOK_PLUS,          /* + */
    TOK_MINUS,         /* - */
    TOK_STAR,          /* * */
    TOK_SLASH,         /* / */
    TOK_PERCENT,       /* % */
    TOK_EQ,            /* == */
    TOK_NEQ,           /* != */
    TOK_LT,            /* < */
    TOK_GT,            /* > */
    TOK_LTE,           /* <= */
    TOK_GTE,           /* >= */
    TOK_AND,           /* && */
    TOK_OR,            /* || */
    TOK_NOT,           /* ! */
    TOK_ASSIGN,        /* = */
    TOK_PLUS_EQ,       /* += */
    TOK_MINUS_EQ,      /* -= */
    TOK_STAR_EQ,       /* *= */
    TOK_SLASH_EQ,      /* /= */
    TOK_INCREMENT,     /* ++ */
    TOK_DECREMENT,     /* -- */
    TOK_ARROW,         /* -> */
    TOK_DOUBLE_ARROW,  /* => (unused, reserved) */
    TOK_RANGE,         /* .. */
    TOK_DOT,           /* . */
    TOK_COMMA,         /* , */
    TOK_COLON,         /* : */
    TOK_SEMICOLON,     /* ; */

    /* *** THE BORROW OPERATOR *** */
    TOK_BACKTICK,      /* ` — the borrow operator, heart of C-Prime */

    /* Delimiters */
    TOK_LPAREN,        /* ( */
    TOK_RPAREN,        /* ) */
    TOK_LBRACE,        /* { */
    TOK_RBRACE,        /* } */
    TOK_LBRACKET,      /* [ */
    TOK_RBRACKET,      /* ] */
    TOK_LANGLE,        /* < (generic open) */
    TOK_RANGLE,        /* > (generic close) */
    TOK_HASH,          /* # (attributes) */
    TOK_AT,            /* @ (annotations) */

    /* Special */
    TOK_EOF,           /* End of file */
    TOK_INVALID,       /* Invalid/unknown token */
} TokenType;

/* ─── Source Location ─────────────────────────────────────────────────────── */
typedef struct {
    const char* filename;
    int         line;
    int         column;
} SourceLoc;

/* ─── Token ───────────────────────────────────────────────────────────────── */
typedef struct {
    TokenType   type;
    const char* start;   /* Pointer into source buffer */
    size_t      length;  /* Length of token text */
    SourceLoc   loc;
    union {
        long long  int_val;
        double     float_val;
        bool       bool_val;
    } value;
} Token;

/* ─── Token Stream ────────────────────────────────────────────────────────── */
typedef struct {
    Token*  tokens;
    size_t  count;
    size_t  capacity;
    size_t  position;    /* Current read position */
} TokenStream;

/* ─── Lexer API ───────────────────────────────────────────────────────────── */

/**
 * lex() — Tokenize a C-Prime source file.
 *
 * @param source    Null-terminated source text
 * @param len       Length of source text
 * @param filename  Filename for error messages
 * @return          Heap-allocated TokenStream, or NULL on error
 */
TokenStream* lex(const char* source, size_t len, const char* filename);

/** Print all tokens (for --dump-tokens) */
void token_stream_dump(const TokenStream* ts);

/** Free the token stream */
void token_stream_free(TokenStream* ts);

/** Peek at current token */
Token* token_stream_peek(TokenStream* ts);

/** Consume and return current token */
Token* token_stream_consume(TokenStream* ts);

/** Check if current token matches type */
bool token_stream_check(const TokenStream* ts, TokenType type);

/** Get human-readable name for a token type */
const char* token_type_name(TokenType type);

#endif /* CPRIME_LEXER_H */
