/*
 * C-Prime Bootstrap Parser — parser.h
 * =====================================
 * Abstract Syntax Tree node definitions and parser API.
 */

#ifndef CPRIME_PARSER_H
#define CPRIME_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include "lexer.h"

/* ─── AST Node Types ──────────────────────────────────────────────────────── */
typedef enum {
    /* Top-level */
    AST_PROGRAM,           /* Root node — list of top-level items */
    AST_IMPORT,            /* import io; */
    AST_FN_DEF,            /* fn name(params) -> ret { body } */
    AST_STRUCT_DEF,        /* struct Name { fields } */
    AST_CONST_DEF,         /* const TYPE NAME = expr; */

    /* Statements */
    AST_BLOCK,             /* { stmt; stmt; ... } */
    AST_RETURN,            /* return expr; */
    AST_VAR_DECL,          /* TYPE name = expr; / auto name = expr; */
    AST_ASSIGN,            /* lhs = rhs */
    AST_COMPOUND_ASSIGN,   /* lhs += rhs etc. */
    AST_IF,                /* if cond { } else { } */
    AST_WHILE,             /* while cond { } */
    AST_FOR_RANGE,         /* for i in lo..hi { } */
    AST_FOR_C,             /* for (init; cond; step) { } */
    AST_MATCH,             /* match expr { arm, arm, ... } */
    AST_MATCH_ARM,         /* pattern -> expr */
    AST_EXPR_STMT,         /* expr; */
    AST_UNSAFE_BLOCK,      /* unsafe { } */

    /* Expressions */
    AST_INT_LIT,           /* 42 */
    AST_FLOAT_LIT,         /* 3.14 */
    AST_STRING_LIT,        /* "hello" */
    AST_BOOL_LIT,          /* true / false */
    AST_NONE_LIT,          /* None */
    AST_IDENT,             /* foo */
    AST_BORROW,            /* `expr */
    AST_BORROW_MUT,        /* `mut expr */
    AST_DEREF,             /* *expr */
    AST_ADDR_OF,           /* &expr */
    AST_BINARY,            /* lhs op rhs */
    AST_UNARY,             /* op expr */
    AST_CALL,              /* expr(args...) */
    AST_METHOD_CALL,       /* expr.method(args...) */
    AST_FIELD_ACCESS,      /* expr.field */
    AST_INDEX,             /* expr[idx] */
    AST_CAST,              /* expr as Type */
    AST_STRUCT_INIT,       /* Name { field: val, ... } */
    AST_ARRAY_LIT,         /* [a, b, c] */
    AST_SOME,              /* Some(expr) */
    AST_OK,                /* Ok(expr) */
    AST_ERR,               /* Err(expr) */
    AST_RANGE,             /* lo..hi */
    AST_SIZEOF,            /* sizeof(Type) */
    AST_CLOSURE,           /* fn() -> T { } — anonymous function */
    AST_IF_EXPR,           /* if cond { } else { } used as expression */

    /* Type nodes */
    AST_TYPE_PRIM,         /* i32, f64, str, bool, void, ... */
    AST_TYPE_REF,          /* `T (borrow type) */
    AST_TYPE_REF_MUT,      /* `mut T */
    AST_TYPE_PTR,          /* T* */
    AST_TYPE_ARRAY,        /* T[] */
    AST_TYPE_NAMED,        /* SomeStruct */
    AST_TYPE_GENERIC,      /* Vec<T> */
    AST_TYPE_FN,           /* fn(T) -> U */
    AST_TYPE_OPTION,       /* Option<T> */
    AST_TYPE_RESULT,       /* Result<T, E> */

    /* Wildcard / special */
    AST_WILDCARD,          /* _ (in match arms) */
    AST_VOID_EXPR,         /* () — unit value */

    AST_NODE_COUNT
} ASTKind;

/* ─── AST Node ────────────────────────────────────────────────────────────── */
typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTKind  kind;
    SourceLoc loc;

    union {
        /* Program / Block */
        struct {
            ASTNode** items;
            size_t    count;
            size_t    cap;
        } list;

        /* Import */
        struct {
            char path[256];   /* "io" or "collections.vec" */
        } import;

        /* Function definition */
        struct {
            char      name[128];
            ASTNode*  params;    /* AST_BLOCK of AST_VAR_DECL nodes */
            size_t    param_count;
            ASTNode** param_list;
            ASTNode*  ret_type;
            ASTNode*  body;      /* AST_BLOCK */
            bool      is_method; /* SomeType.method */
            char      type_name[128];
        } fn_def;

        /* Struct definition */
        struct {
            char      name[128];
            ASTNode** fields;    /* list of AST_VAR_DECL (no initializer) */
            size_t    field_count;
        } struct_def;

        /* Constant definition */
        struct {
            char     name[128];
            ASTNode* type;
            ASTNode* value;
        } const_def;

        /* Variable declaration */
        struct {
            char     name[128];
            ASTNode* type;       /* NULL if auto-inferred */
            ASTNode* init;       /* NULL if uninitialized */
            bool     is_param;   /* true when used as function parameter */
            bool     is_mut;     /* mut keyword present */
        } var_decl;

        /* Assignment */
        struct {
            ASTNode* lhs;
            ASTNode* rhs;
            TokenType op;    /* TOK_ASSIGN, TOK_PLUS_EQ, etc. */
        } assign;

        /* Binary expression */
        struct {
            ASTNode*  left;
            ASTNode*  right;
            TokenType op;
        } binary;

        /* Unary expression */
        struct {
            ASTNode*  operand;
            TokenType op;
        } unary;

        /* Function / method call */
        struct {
            ASTNode*  callee;   /* AST_IDENT or AST_FIELD_ACCESS */
            ASTNode** args;
            size_t    arg_count;
        } call;

        /* Field access */
        struct {
            ASTNode* object;
            char     field[128];
        } field;

        /* If statement / expression */
        struct {
            ASTNode* cond;
            ASTNode* then_block;
            ASTNode* else_block;  /* NULL if no else */
        } if_stmt;

        /* While loop */
        struct {
            ASTNode* cond;
            ASTNode* body;
        } while_loop;

        /* For range loop */
        struct {
            char     var[64];
            ASTNode* start;
            ASTNode* end;
            ASTNode* body;
        } for_range;

        /* Match statement */
        struct {
            ASTNode*  subject;
            ASTNode** arms;
            size_t    arm_count;
        } match;

        /* Match arm */
        struct {
            ASTNode* pattern;  /* AST_IDENT, AST_SOME, AST_OK, AST_ERR, AST_WILDCARD */
            char     bind[64]; /* binding variable inside Some(x) */
            ASTNode* body;     /* expr or block */
        } arm;

        /* Return */
        struct {
            ASTNode* value;  /* NULL for bare return */
        } ret;

        /* Literals */
        struct {
            long long  int_val;
            double     float_val;
            bool       bool_val;
            char       str_val[1024]; /* unescaped string content */
        } lit;

        /* Identifier */
        struct {
            char name[128];
        } ident;

        /* Borrow */
        struct {
            ASTNode* expr;
            bool     is_mut;
        } borrow;

        /* Cast */
        struct {
            ASTNode* expr;
            ASTNode* type;
        } cast;

        /* Struct initializer */
        struct {
            char      type_name[128];
            char**    field_names;
            ASTNode** field_values;
            size_t    field_count;
        } struct_init;

        /* Type nodes */
        struct {
            TokenType prim;       /* for AST_TYPE_PRIM */
            char      name[128];  /* for AST_TYPE_NAMED */
            ASTNode*  inner;      /* for AST_TYPE_REF, AST_TYPE_PTR, etc. */
            ASTNode*  inner2;     /* for AST_TYPE_RESULT (error type) */
        } type;

        /* Index expr: arr[i] */
        struct {
            ASTNode* array;
            ASTNode* index;
        } index;

        /* Range expr: lo..hi */
        struct {
            ASTNode* lo;
            ASTNode* hi;
        } range;

        /* Wrapping constructors: Some(x), Ok(x), Err(x) */
        struct {
            ASTNode* inner;
        } wrap;

    } as;
};

/* ─── Parser API ──────────────────────────────────────────────────────────── */
ASTNode* parse(TokenStream* ts);
void     ast_dump(const ASTNode* node, int indent);
void     ast_free(ASTNode* node);

/* ─── AST construction helpers ────────────────────────────────────────────── */
ASTNode* ast_new(ASTKind kind, SourceLoc loc);
void     ast_list_push(ASTNode* list_node, ASTNode* child);

#endif /* CPRIME_PARSER_H */
