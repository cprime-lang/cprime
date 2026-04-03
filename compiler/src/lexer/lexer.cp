/*
 * cpc — Lexer
 * compiler/src/lexer/lexer.cp
 * ============================
 * Converts raw C-Prime source text into a flat TokenStream.
 *
 * Key responsibilities:
 *   - Recognise all 70+ token types including the ` borrow operator
 *   - Handle string/char escape sequences
 *   - Parse hex (0x), binary (0b), decimal integer literals
 *   - Parse float literals with optional f32/f64 suffix
 *   - Skip line (//) and block (/* */) comments
 *   - Attach accurate Span (file, line, col, len) to every token
 *   - Accumulate and report multiple errors before aborting
 */

import core;
import mem;
import string;
import "lexer/token";

/* ─── Keyword map ─────────────────────────────────────────────────────────── */
struct KwEntry { str word; TokenKind kind; }

const KwEntry[] KEYWORDS = [
    { word: "fn",       kind: TK_FN       },
    { word: "return",   kind: TK_RETURN   },
    { word: "if",       kind: TK_IF       },
    { word: "else",     kind: TK_ELSE     },
    { word: "while",    kind: TK_WHILE    },
    { word: "for",      kind: TK_FOR      },
    { word: "in",       kind: TK_IN       },
    { word: "match",    kind: TK_MATCH    },
    { word: "struct",   kind: TK_STRUCT   },
    { word: "enum",     kind: TK_ENUM     },
    { word: "import",   kind: TK_IMPORT   },
    { word: "const",    kind: TK_CONST    },
    { word: "auto",     kind: TK_AUTO     },
    { word: "mut",      kind: TK_MUT      },
    { word: "unsafe",   kind: TK_UNSAFE   },
    { word: "asm",      kind: TK_ASM      },
    { word: "as",       kind: TK_AS       },
    { word: "break",    kind: TK_BREAK    },
    { word: "continue", kind: TK_CONTINUE },
    { word: "true",     kind: TK_TRUE     },
    { word: "false",    kind: TK_FALSE    },
    { word: "None",     kind: TK_NONE     },
    { word: "Some",     kind: TK_SOME     },
    { word: "Ok",       kind: TK_OK       },
    { word: "Err",      kind: TK_ERR      },
    /* Types */
    { word: "i8",       kind: TK_I8       },
    { word: "i16",      kind: TK_I16      },
    { word: "i32",      kind: TK_I32      },
    { word: "i64",      kind: TK_I64      },
    { word: "u8",       kind: TK_U8       },
    { word: "u16",      kind: TK_U16      },
    { word: "u32",      kind: TK_U32      },
    { word: "u64",      kind: TK_U64      },
    { word: "f32",      kind: TK_F32      },
    { word: "f64",      kind: TK_F64      },
    { word: "bool",     kind: TK_BOOL_T   },
    { word: "char",     kind: TK_CHAR_T   },
    { word: "str",      kind: TK_STR      },
    { word: "byte",     kind: TK_BYTE     },
    { word: "usize",    kind: TK_USIZE    },
    { word: "isize",    kind: TK_ISIZE    },
    { word: "void",     kind: TK_VOID     },
];

/* ─── Lexer state ─────────────────────────────────────────────────────────── */
struct Lexer {
    str    src;           /* full source text */
    usize  pos;           /* current byte position */
    u32    line;
    u32    col;
    str    filename;
    bool   had_error;
    u32    error_count;
}

fn Lexer.new(src `str, filename `str) -> Lexer {
    return Lexer {
        src: src, pos: 0, line: 1, col: 1,
        filename: filename, had_error: false, error_count: 0,
    };
}

fn Lexer.current(`Lexer self) -> char {
    if self.pos < string.len(self.src) {
        return string.char_at(self.src, self.pos);
    }
    return '\0';
}

fn Lexer.peek_at(`Lexer self, usize offset) -> char {
    usize idx = self.pos + offset;
    if idx < string.len(self.src) {
        return string.char_at(self.src, idx);
    }
    return '\0';
}

fn Lexer.advance(`mut Lexer self) -> char {
    char c = self.current();
    self.pos = self.pos + 1;
    if c == '\n' { self.line = self.line + 1; self.col = 1; }
    else         { self.col  = self.col  + 1; }
    return c;
}

fn Lexer.match_char(`mut Lexer self, char expected) -> bool {
    if self.current() == expected { self.advance(); return true; }
    return false;
}

fn Lexer.error(`mut Lexer self, `str msg) -> void {
    io.eprintf("[cpc] %s:%d:%d: \033[31merror:\033[0m %s\n",
               self.filename, self.line, self.col, msg);
    self.had_error   = true;
    self.error_count = self.error_count + 1;
}

fn Lexer.skip_whitespace(`mut Lexer self) -> void {
    while self.pos < string.len(self.src) {
        char c = self.current();
        if c == ' ' || c == '\t' || c == '\r' || c == '\n' {
            self.advance();
        } else if c == '/' && self.peek_at(1) == '/' {
            /* line comment */
            while self.pos < string.len(self.src) && self.current() != '\n' {
                self.advance();
            }
        } else if c == '/' && self.peek_at(1) == '*' {
            /* block comment */
            self.advance(); self.advance();
            while self.pos + 1 < string.len(self.src) {
                if self.current() == '*' && self.peek_at(1) == '/' {
                    self.advance(); self.advance();
                    break;
                }
                self.advance();
            }
        } else {
            break;
        }
    }
}

fn Lexer.lex_string(`mut Lexer self) -> Token {
    u32 start_line = self.line;
    u32 start_col  = self.col - 1; /* includes opening " */
    str buf = "";
    while self.pos < string.len(self.src) && self.current() != '"' {
        if self.current() == '\\' {
            self.advance();
            char esc = self.advance();
            match esc {
                'n'  -> buf = string.concat(buf, "\n"),
                't'  -> buf = string.concat(buf, "\t"),
                'r'  -> buf = string.concat(buf, "\r"),
                '0'  -> buf = string.concat(buf, "\0"),
                '\\' -> buf = string.concat(buf, "\\"),
                '"'  -> buf = string.concat(buf, "\""),
                '\'' -> buf = string.concat(buf, "'"),
                _    -> {
                    self.error("unknown escape sequence");
                    buf = string.concat(buf, "?");
                },
            }
        } else {
            buf = string.push_char(buf, self.advance());
        }
    }
    if self.current() == '"' { self.advance(); }
    else { self.error("unterminated string literal"); }
    Token t;
    t.kind      = TK_STRING;
    t.text      = buf;
    t.span.file = self.filename;
    t.span.line = start_line;
    t.span.col  = start_col;
    return t;
}

fn Lexer.lex_number(`mut Lexer self, usize start_pos, u32 start_line, u32 start_col) -> Token {
    Token t;
    t.span.file = self.filename;
    t.span.line = start_line;
    t.span.col  = start_col;

    /* Hex */
    if self.current() == 'x' || self.current() == 'X' {
        self.advance();
        usize hex_start = self.pos;
        while is_hex_digit(self.current()) || self.current() == '_' {
            self.advance();
        }
        str hex_str = string.slice(self.src, hex_start, self.pos);
        t.kind    = TK_INT;
        t.int_val = parse_hex(hex_str);
        t.text    = string.slice(self.src, start_pos, self.pos);
        return t;
    }

    /* Binary */
    if self.current() == 'b' || self.current() == 'B' {
        self.advance();
        usize bin_start = self.pos;
        while self.current() == '0' || self.current() == '1' || self.current() == '_' {
            self.advance();
        }
        str bin_str = string.slice(self.src, bin_start, self.pos);
        t.kind    = TK_INT;
        t.int_val = parse_binary(bin_str);
        t.text    = string.slice(self.src, start_pos, self.pos);
        return t;
    }

    /* Decimal / Float */
    while is_digit(self.current()) || self.current() == '_' {
        self.advance();
    }
    bool is_float = false;
    if self.current() == '.' && is_digit(self.peek_at(1)) {
        is_float = true;
        self.advance();
        while is_digit(self.current()) || self.current() == '_' {
            self.advance();
        }
    }
    if self.current() == 'e' || self.current() == 'E' {
        is_float = true;
        self.advance();
        if self.current() == '+' || self.current() == '-' { self.advance(); }
        while is_digit(self.current()) { self.advance(); }
    }
    str suffix = "";
    if self.current() == 'f' {
        self.advance();
        if self.current() == '3' && self.peek_at(1) == '2' {
            suffix = "f32"; self.advance(); self.advance();
        } else if self.current() == '6' && self.peek_at(1) == '4' {
            suffix = "f64"; self.advance(); self.advance();
        }
        is_float = true;
    }
    t.text = string.slice(self.src, start_pos, self.pos);
    if is_float {
        t.kind      = TK_FLOAT;
        t.float_val = string.parse_f64(t.text);
    } else {
        t.kind    = TK_INT;
        t.int_val = string.parse_i64(t.text);
    }
    return t;
}

fn Lexer.lex_ident(`mut Lexer self, usize start_pos, u32 start_line, u32 start_col) -> Token {
    while is_ident_continue(self.current()) { self.advance(); }
    str word = string.slice(self.src, start_pos, self.pos);
    Token t;
    t.text      = word;
    t.span.file = self.filename;
    t.span.line = start_line;
    t.span.col  = start_col;
    /* Keyword lookup */
    for kw in KEYWORDS {
        if string.eq(kw.word, word) {
            t.kind = kw.kind;
            if kw.kind == TK_TRUE  { t.bool_val = true;  }
            if kw.kind == TK_FALSE { t.bool_val = false; }
            return t;
        }
    }
    t.kind = TK_IDENT;
    return t;
}

/* ─── Main lex function ───────────────────────────────────────────────────── */
fn lex(src `str, filename `str) -> Result<TokenStream, str> {
    Lexer l = Lexer.new(src, filename);
    TokenStream ts = TokenStream.new();

    while true {
        l.skip_whitespace();
        if l.pos >= string.len(l.src) { break; }

        u32   tok_line = l.line;
        u32   tok_col  = l.col;
        usize tok_pos  = l.pos;
        char  c        = l.advance();
        Token tok;
        tok.span.file = filename;
        tok.span.line = tok_line;
        tok.span.col  = tok_col;

        match c {
            '`' -> { tok.kind = TK_BACKTICK; tok.text = "`"; },
            '(' -> { tok.kind = TK_LPAREN;   tok.text = "("; },
            ')' -> { tok.kind = TK_RPAREN;   tok.text = ")"; },
            '{' -> { tok.kind = TK_LBRACE;   tok.text = "{"; },
            '}' -> { tok.kind = TK_RBRACE;   tok.text = "}"; },
            '[' -> { tok.kind = TK_LBRACKET; tok.text = "["; },
            ']' -> { tok.kind = TK_RBRACKET; tok.text = "]"; },
            ';' -> { tok.kind = TK_SEMI;     tok.text = ";"; },
            ',' -> { tok.kind = TK_COMMA;    tok.text = ","; },
            '#' -> { tok.kind = TK_HASH;     tok.text = "#"; },
            '@' -> { tok.kind = TK_AT;       tok.text = "@"; },
            '~' -> { tok.kind = TK_BNOT;     tok.text = "~"; },
            ':' -> {
                if l.match_char(':') { tok.kind = TK_DCOLON; tok.text = "::"; }
                else                 { tok.kind = TK_COLON;  tok.text = ":";  }
            },
            '.' -> {
                if l.match_char('.') {
                    if l.match_char('=') { tok.kind = TK_RANGE_EQ; tok.text = "..="; }
                    else                 { tok.kind = TK_RANGE;    tok.text = "..";  }
                } else { tok.kind = TK_DOT; tok.text = "."; }
            },
            '-' -> {
                if      l.match_char('>') { tok.kind = TK_ARROW;    tok.text = "->"; }
                else if l.match_char('=') { tok.kind = TK_MINUS_EQ; tok.text = "-="; }
                else if l.match_char('-') { tok.kind = TK_DEC;      tok.text = "--"; }
                else                      { tok.kind = TK_MINUS;    tok.text = "-";  }
            },
            '+' -> {
                if      l.match_char('=') { tok.kind = TK_PLUS_EQ; tok.text = "+="; }
                else if l.match_char('+') { tok.kind = TK_INC;     tok.text = "++"; }
                else                      { tok.kind = TK_PLUS;    tok.text = "+";  }
            },
            '*' -> {
                if l.match_char('=') { tok.kind = TK_STAR_EQ; tok.text = "*="; }
                else                 { tok.kind = TK_STAR;    tok.text = "*";  }
            },
            '/' -> {
                if l.match_char('=') { tok.kind = TK_SLASH_EQ; tok.text = "/="; }
                else                 { tok.kind = TK_SLASH;    tok.text = "/";  }
            },
            '%' -> {
                if l.match_char('=') { tok.kind = TK_PERCENT_EQ; tok.text = "%="; }
                else                 { tok.kind = TK_PERCENT;    tok.text = "%";  }
            },
            '=' -> {
                if      l.match_char('=') { tok.kind = TK_EQ;       tok.text = "=="; }
                else if l.match_char('>') { tok.kind = TK_FAT_ARROW;tok.text = "=>"; }
                else                      { tok.kind = TK_ASSIGN;   tok.text = "=";  }
            },
            '!' -> {
                if l.match_char('=') { tok.kind = TK_NEQ; tok.text = "!="; }
                else                 { tok.kind = TK_NOT; tok.text = "!";  }
            },
            '<' -> {
                if      l.match_char('=') { tok.kind = TK_LTE; tok.text = "<="; }
                else if l.match_char('<') {
                    if l.match_char('=') { tok.kind = TK_SHL_EQ; tok.text = "<<="; }
                    else                 { tok.kind = TK_SHL;    tok.text = "<<";  }
                } else { tok.kind = TK_LT; tok.text = "<"; }
            },
            '>' -> {
                if      l.match_char('=') { tok.kind = TK_GTE; tok.text = ">="; }
                else if l.match_char('>') {
                    if l.match_char('=') { tok.kind = TK_SHR_EQ; tok.text = ">>="; }
                    else                 { tok.kind = TK_SHR;    tok.text = ">>";  }
                } else { tok.kind = TK_GT; tok.text = ">"; }
            },
            '&' -> {
                if      l.match_char('&') { tok.kind = TK_AND;   tok.text = "&&"; }
                else if l.match_char('=') { tok.kind = TK_AND_EQ;tok.text = "&="; }
                else                      { tok.kind = TK_BAND;  tok.text = "&";  }
            },
            '|' -> {
                if      l.match_char('|') { tok.kind = TK_OR;   tok.text = "||"; }
                else if l.match_char('=') { tok.kind = TK_OR_EQ;tok.text = "|="; }
                else                      { tok.kind = TK_BOR;  tok.text = "|";  }
            },
            '^' -> {
                if l.match_char('=') { tok.kind = TK_XOR_EQ; tok.text = "^="; }
                else                 { tok.kind = TK_BXOR;   tok.text = "^";  }
            },
            '"' -> { tok = l.lex_string(); },
            _ -> {
                if is_digit(c) {
                    tok = l.lex_number(tok_pos, tok_line, tok_col);
                } else if is_ident_start(c) {
                    tok = l.lex_ident(tok_pos, tok_line, tok_col);
                } else {
                    l.error(fmt.sprintf("unexpected character '%c'", c));
                    tok.kind = TK_INVALID;
                    tok.text = string.from_char(c);
                }
            },
        }

        ts_push(`mut ts, tok);
    }

    /* EOF sentinel */
    Token eof;
    eof.kind      = TK_EOF;
    eof.text      = "";
    eof.span.file = filename;
    eof.span.line = l.line;
    eof.span.col  = l.col;
    ts_push(`mut ts, eof);

    if l.had_error {
        return Err(fmt.sprintf("lexer: %d error(s) in %s", l.error_count, filename));
    }
    return Ok(ts);
}

/* ─── Helpers ─────────────────────────────────────────────────────────────── */
fn is_digit(char c) -> bool {
    return c >= '0' && c <= '9';
}

fn is_hex_digit(char c) -> bool {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

fn is_ident_start(char c) -> bool {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

fn is_ident_continue(char c) -> bool {
    return is_ident_start(c) || is_digit(c);
}

fn ts_push(`mut TokenStream ts, Token tok) -> void {
    if ts.len >= ts.cap {
        ts.cap = if ts.cap == 0 { 256 } else { ts.cap * 2 };
        ts.tokens = mem.realloc_array<Token>(ts.tokens, ts.cap);
    }
    ts.tokens[ts.len] = tok;
    ts.len = ts.len + 1;
}

fn parse_hex(`str s) -> i64 {
    i64 val = 0;
    for ch in s {
        if ch == '_' { continue; }
        val = val * 16;
        if ch >= '0' && ch <= '9' { val = val + (ch as i64 - '0' as i64); }
        else if ch >= 'a' && ch <= 'f' { val = val + (ch as i64 - 'a' as i64 + 10); }
        else if ch >= 'A' && ch <= 'F' { val = val + (ch as i64 - 'A' as i64 + 10); }
    }
    return val;
}

fn parse_binary(`str s) -> i64 {
    i64 val = 0;
    for ch in s {
        if ch == '_' { continue; }
        val = val * 2 + (ch as i64 - '0' as i64);
    }
    return val;
}
