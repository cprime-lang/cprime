/*
 * cpc — ELF Writer + Linker
 * compiler/src/codegen/x86_64/elf.cp
 * =====================================
 * Takes the filled EmitCtx and produces a native Linux ELF64 binary.
 *
 * Strategy (same as bootstrap/src/codegen/elf_writer.c):
 *   1. Resolve all internal relocations (jump patches)
 *   2. Write an Intel-syntax .s assembly file, substituting TextPatches
 *      for NOP placeholders (so GNU `as` resolves extern/string addresses)
 *   3. Assemble: as --64 -o file.o file.s
 *   4. Write C wrapper providing real ELF entry point (calls cprime_main)
 *   5. Link:    gcc -no-pie -o output wrapper.c file.o -lm
 *   6. Clean up temp files
 */

import core;
import io;
import os;
import string;
import fmt;
import "codegen/x86_64/emit";

/* ─── ELF writer ──────────────────────────────────────────────────────────── */
fn elf_write(`EmitCtx e, `str output_path) -> Result<void, str> {
    /* Step 1: resolve internal relocations */
    resolve_relocs(`mut e);

    /* Step 2: sort patches by offset */
    sort_patches(`mut e);

    /* Step 3: write assembly file */
    str asm_file  = fmt.sprintf("/tmp/cprime_%d.s",   os.get_pid());
    str obj_file  = fmt.sprintf("/tmp/cprime_%d.o",   os.get_pid());
    str wrap_file = fmt.sprintf("/tmp/cprime_%d.c",   os.get_pid());

    match write_asm(`e, asm_file) {
        Err(e) -> return Err(fmt.sprintf("asm write failed: %s", e)),
        Ok(_)  -> {},
    }

    /* Step 4: assemble */
    str as_cmd = fmt.sprintf("as --64 -o %s %s 2>&1", obj_file, asm_file);
    i32 rc = os.shell(as_cmd);
    if rc != 0 {
        os.remove(asm_file);
        return Err(fmt.sprintf("assembly failed (see %s)", asm_file));
    }

    /* Step 5: write C wrapper */
    match write_c_wrapper(wrap_file) {
        Err(e) -> { os.remove(asm_file); os.remove(obj_file); return Err(e); },
        Ok(_)  -> {},
    }

    /* Step 6: link */
    str link_cmd = fmt.sprintf("gcc -no-pie -o %s %s %s -lm 2>&1",
                                output_path, wrap_file, obj_file);
    rc = os.shell(link_cmd);

    os.remove(asm_file);
    os.remove(obj_file);
    os.remove(wrap_file);

    if rc != 0 { return Err("linking failed"); }

    /* Make output executable */
    os.chmod(output_path, 0o755);
    return Ok(void);
}

/* ─── Write .s file ───────────────────────────────────────────────────────── */
fn write_asm(`EmitCtx e, `str path) -> Result<void, str> {
    str out = "";

    out = string.concat(out, "    .intel_syntax noprefix\n\n");

    /* Declare globals */
    usize i = 0;
    while i < e.label_count {
        Label* l = `e.labels[i];
        if l.is_global && l.code_offset != UNRESOLVED {
            out = string.concat(out, fmt.sprintf("    .global %s\n", l.name));
        }
        i = i + 1;
    }
    out = string.concat(out, "\n");

    /* .rodata: string literals */
    if e.string_count > 0 {
        out = string.concat(out, "    .section .rodata\n");
        i = 0;
        while i < e.string_count {
            StringEntry* s = `e.strings[i];
            out = string.concat(out,
                fmt.sprintf("__cprime_str_%d:\n    .asciz %s\n",
                             i, asm_string_literal(s.text)));
            i = i + 1;
        }
        out = string.concat(out, "\n");
    }

    /* .text: emit bytes, substituting patches and interspersing labels */
    out = string.concat(out, "    .text\n\n");

    usize byte_pos  = 0;
    usize patch_idx = 0;

    while byte_pos <= e.text.len {
        /* Emit any labels at this position */
        i = 0;
        while i < e.label_count {
            if e.labels[i].code_offset == byte_pos {
                out = string.concat(out,
                    fmt.sprintf("%s:\n", e.labels[i].name));
            }
            i = i + 1;
        }

        if byte_pos == e.text.len { break; }

        /* Check for a text patch at this position */
        if patch_idx < e.patch_count &&
           e.patches[patch_idx].offset == byte_pos {
            TextPatch* tp = `e.patches[patch_idx];
            out = string.concat(out,
                fmt.sprintf("    %s\n", tp.asm_text));
            byte_pos  = byte_pos + tp.nop_count;
            patch_idx = patch_idx + 1;
            continue;
        }

        /* Find next boundary */
        usize next_boundary = e.text.len;
        i = 0;
        while i < e.label_count {
            usize co = e.labels[i].code_offset;
            if co > byte_pos && co < next_boundary { next_boundary = co; }
            i = i + 1;
        }
        if patch_idx < e.patch_count {
            usize po = e.patches[patch_idx].offset;
            if po > byte_pos && po < next_boundary { next_boundary = po; }
        }

        /* Emit up to 16 bytes as .byte directive */
        usize end = byte_pos + 16;
        if end > next_boundary { end = next_boundary; }

        out = string.concat(out, "    .byte ");
        usize j = byte_pos;
        while j < end {
            if j != byte_pos { out = string.concat(out, ","); }
            out = string.concat(out, fmt.sprintf("0x%02x", e.text.data[j] as u32));
            j = j + 1;
        }
        out = string.concat(out, "\n");
        byte_pos = end;
    }

    match io.write_file(path, out) {
        Err(e) -> return Err(e),
        Ok(_)  -> return Ok(void),
    }
}

/* ─── Write C wrapper ────────────────────────────────────────────────────── */
fn write_c_wrapper(`str path) -> Result<void, str> {
    str content =
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "typedef long long ll;\n"
        "/* C-Prime entry point — renamed from 'main' to avoid symbol clash */\n"
        "extern ll cprime_main(void);\n"
        "int main(int argc, char** argv) {\n"
        "    (void)argc; (void)argv;\n"
        "    return (int)cprime_main();\n"
        "}\n";
    return io.write_file(path, content);
}

/* ─── Helpers ─────────────────────────────────────────────────────────────── */
fn asm_string_literal(`str s) -> str {
    str result = "\"";
    usize i = 0;
    while i < string.len(s) {
        char c = string.char_at(s, i);
        if      c == '"'  { result = string.concat(result, "\\\""); }
        else if c == '\\' { result = string.concat(result, "\\\\"); }
        else if c == '\n' { result = string.concat(result, "\\n");  }
        else if c == '\t' { result = string.concat(result, "\\t");  }
        else if c as u8 >= 32 && c as u8 < 127 {
            result = string.push_char(result, c);
        } else {
            result = string.concat(result, fmt.sprintf("\\x%02x", c as u32));
        }
        i = i + 1;
    }
    return string.concat(result, "\"");
}

fn sort_patches(`mut EmitCtx e) -> void {
    /* Insertion sort (patch count is small) */
    usize i = 1;
    while i < e.patch_count {
        TextPatch tmp = e.patches[i];
        i64 j = i as i64 - 1;
        while j >= 0 && e.patches[j as usize].offset > tmp.offset {
            e.patches[(j + 1) as usize] = e.patches[j as usize];
            j = j - 1;
        }
        e.patches[(j + 1) as usize] = tmp;
        i = i + 1;
    }
}
