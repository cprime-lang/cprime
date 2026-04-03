/**
 * C-Prime Hover Provider — hover.ts
 * ====================================
 * Shows inline documentation when hovering over:
 *   - Primitive type keywords (i32, f64, str…)
 *   - The backtick borrow operator
 *   - Built-in functions (io.println, mem.alloc…)
 *   - Keywords (fn, match, struct…)
 */

import * as vscode from 'vscode';

// ─── Type Documentation ───────────────────────────────────────────────────────

const TYPE_DOCS: Record<string, string> = {
    'i8':    '**i8** — Signed 8-bit integer. Range: -128 to 127.',
    'i16':   '**i16** — Signed 16-bit integer. Range: -32,768 to 32,767.',
    'i32':   '**i32** — Signed 32-bit integer. Range: -2,147,483,648 to 2,147,483,647.',
    'i64':   '**i64** — Signed 64-bit integer. Range: ±9.2 × 10¹⁸.',
    'u8':    '**u8** — Unsigned 8-bit integer (byte). Range: 0 to 255.',
    'u16':   '**u16** — Unsigned 16-bit integer. Range: 0 to 65,535.',
    'u32':   '**u32** — Unsigned 32-bit integer. Range: 0 to 4,294,967,295.',
    'u64':   '**u64** — Unsigned 64-bit integer. Range: 0 to 1.8 × 10¹⁹.',
    'f32':   '**f32** — 32-bit IEEE 754 floating point. ~7 decimal digits of precision.',
    'f64':   '**f64** — 64-bit IEEE 754 floating point. ~15 decimal digits of precision.',
    'bool':  '**bool** — Boolean value: `true` or `false`. Size: 1 byte.',
    'char':  '**char** — Unicode scalar value (UTF-32). Size: 4 bytes.',
    'str':   '**str** — String slice: a fat pointer (ptr + length). Not null-terminated.',
    'byte':  '**byte** — Raw byte, alias for `u8`. Range: 0–255.',
    'usize': '**usize** — Unsigned pointer-sized integer. 8 bytes on 64-bit.',
    'isize': '**isize** — Signed pointer-sized integer. 8 bytes on 64-bit.',
    'void':  '**void** — No value. Used as return type for functions that return nothing.',
};

// ─── Keyword Documentation ────────────────────────────────────────────────────

const KEYWORD_DOCS: Record<string, string> = {
    'fn':     '**fn** — Define a function.\n\n```c\nfn add(i32 a, i32 b) -> i32 {\n    return a + b;\n}\n```',
    'struct': '**struct** — Define a composite type.\n\n```c\nstruct Point { f64 x; f64 y; }\n```',
    'import': '**import** — Import a standard library module.\n\n```c\nimport io;\nimport collections.vec;\n```',
    'match':  '**match** — Exhaustive pattern matching (like switch, but complete).\n\n```c\nmatch val {\n    Some(x) -> io.println(x),\n    None    -> io.println("empty"),\n}\n```',
    'return': '**return** — Return a value from a function.',
    'const':  '**const** — Define a compile-time constant.',
    'auto':   '**auto** — Type inference. The compiler deduces the type from the initializer.',
    'mut':    '**mut** — Mark a borrow as mutable: `` `mut variable ``',
    'unsafe': '**unsafe** — Block that allows raw pointer operations. Use sparingly.',
    'for':    '**for** — Range-based loop.\n\n```c\nfor i in 0..10 {\n    io.println(i);\n}\n```',
    'while':  '**while** — Loop while condition is true.',
    'if':     '**if** — Conditional branch.',
    'else':   '**else** — Alternative branch for an if statement.',
    'true':   '**true** — Boolean literal true.',
    'false':  '**false** — Boolean literal false.',
    'None':   '**None** — The absent value of `Option<T>`. There are no nulls in C-Prime.',
    'Some':   '**Some(v)** — The present value of `Option<T>`.',
    'Ok':     '**Ok(v)** — The success variant of `Result<T, E>`.',
    'Err':    '**Err(e)** — The failure variant of `Result<T, E>`.',
    'in':     '**in** — Used in `for i in range` loop syntax.',
};

// ─── Stdlib Function Documentation ───────────────────────────────────────────

const STDLIB_DOCS: Record<string, string> = {
    'io.println':  '```\nfn println(`str s) -> void\n```\nPrint a string followed by a newline to stdout.',
    'io.print':    '```\nfn print(`str s) -> void\n```\nPrint a string to stdout without a newline.',
    'io.printf':   '```\nfn printf(`str fmt, ...) -> void\n```\nFormatted output. Uses C-style format specifiers (`%d`, `%s`, `%f`).',
    'io.eprintln': '```\nfn eprintln(`str s) -> void\n```\nPrint a string + newline to stderr.',
    'io.read_line':'```\nfn read_line() -> Result<str, str>\n```\nRead one line from stdin. Returns `Err("EOF")` at end of input.',
    'io.prompt':   '```\nfn prompt(`str message) -> Result<str, str>\n```\nPrint a prompt, then read a line from stdin.',
    'io.open':     '```\nfn open(`str path, FileMode mode) -> Result<File, str>\n```\nOpen a file. Modes: `Read`, `Write`, `Append`, `ReadWrite`.',
    'io.read_file':'```\nfn read_file(`str path) -> Result<str, str>\n```\nRead entire file contents into a string.',
    'io.write_file':'```\nfn write_file(`str path, `str content) -> Result<void, str>\n```\nWrite a string to a file (creates/overwrites).',
    'mem.alloc':   '```\nfn alloc(usize n) -> u8*\n```\nAllocate `n` zero-initialized bytes on the heap.',
    'mem.free':    '```\nfn free(u8* ptr) -> void\n```\nFree heap memory.',
    'mem.copy':    '```\nfn copy(u8* dst, `u8* src, usize n) -> void\n```\nCopy `n` bytes. Regions must not overlap.',
    'mem.zero':    '```\nfn zero(u8* dst, usize n) -> void\n```\nZero out `n` bytes.',
    'string.eq':   '```\nfn eq(`str a, `str b) -> bool\n```\nReturn `true` if two strings are equal.',
    'string.len':  '```\nfn len(`str s) -> usize\n```\nReturn the byte length of a string.',
    'fmt.sprintf': '```\nfn sprintf(`str fmt, ...) -> str\n```\nFormat a string (like C\'s sprintf).',
    'Vec.new':     '```\nfn Vec.new<T>() -> Vec<T>\n```\nCreate an empty dynamic array.',
    'Vec.push':    '```\nfn Vec.push<T>(`mut Vec<T> self, T item) -> Result<void, str>\n```\nAppend an item to the end.',
    'Vec.pop':     '```\nfn Vec.pop<T>(`mut Vec<T> self) -> Option<T>\n```\nRemove and return the last item.',
    'Vec.get':     '```\nfn Vec.get<T>(`Vec<T> self, usize index) -> Result<`T, str>\n```\nGet an element by index.',
    'Vec.len':     '```\nfn Vec.len<T>(`Vec<T> self) -> usize\n```\nReturn the number of elements.',
};

// ─── Hover Provider ───────────────────────────────────────────────────────────

export class CprimeHoverProvider implements vscode.HoverProvider {

    provideHover(
        document: vscode.TextDocument,
        position: vscode.Position,
        _token: vscode.CancellationToken
    ): vscode.ProviderResult<vscode.Hover> {

        const range = document.getWordRangeAtPosition(position, /[a-zA-Z_][a-zA-Z0-9_.]*|`(mut\s+)?/);
        if (!range) return null;

        const word = document.getText(range);

        // Check the backtick operator specifically
        const lineText = document.lineAt(position.line).text;
        const charBefore = position.character > 0 ? lineText[position.character - 1] : '';
        if (word === '`' || charBefore === '`') {
            const md = new vscode.MarkdownString();
            md.isTrusted = true;
            md.appendMarkdown('**`` ` `` — The Borrow Operator** *(C-Prime\'s core feature)*\n\n');
            md.appendMarkdown('Borrows a value without taking ownership.\n\n');
            md.appendCodeblock(
                'fn greet(name `str) -> void {\n' +
                '    io.println(name);\n' +
                '}  // name is NOT freed here\n\n' +
                'fn main() -> i32 {\n' +
                '    str s = "hello";\n' +
                '    greet(`s);    // borrow s\n' +
                '    io.println(s); // still valid!\n' +
                '    return 0;\n' +
                '}',
                'cprime'
            );
            md.appendMarkdown('\n\n*Use `` `mut `` for a mutable borrow.*');
            return new vscode.Hover(md, range);
        }

        // Check type keywords
        if (TYPE_DOCS[word]) {
            const md = new vscode.MarkdownString(TYPE_DOCS[word]);
            md.isTrusted = true;
            return new vscode.Hover(md, range);
        }

        // Check language keywords
        if (KEYWORD_DOCS[word]) {
            const md = new vscode.MarkdownString(KEYWORD_DOCS[word]);
            md.isTrusted = true;
            return new vscode.Hover(md, range);
        }

        // Check stdlib functions (word may be "io.println" etc.)
        if (STDLIB_DOCS[word]) {
            const md = new vscode.MarkdownString();
            md.isTrusted = true;
            md.appendMarkdown(`**${word}**\n\n`);
            md.appendMarkdown(STDLIB_DOCS[word]);
            return new vscode.Hover(md, range);
        }

        // Partial match: if cursor is on "println" and line has "io.println"
        for (const [key, doc] of Object.entries(STDLIB_DOCS)) {
            if (key.endsWith('.' + word) || key === word) {
                const md = new vscode.MarkdownString();
                md.isTrusted = true;
                md.appendMarkdown(`**${key}**\n\n`);
                md.appendMarkdown(doc);
                return new vscode.Hover(md, range);
            }
        }

        return null;
    }
}
