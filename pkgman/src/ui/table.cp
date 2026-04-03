/*
 * cppm — Table Renderer
 * pkgman/src/ui/table.cp
 * =======================
 * Renders data as aligned ASCII tables in the terminal.
 * Used by 'cppm list', 'cppm search', and 'cppm info'.
 *
 * Example output:
 *   ┌─────────────┬─────────┬──────────────────────────────┐
 *   │ Package     │ Version │ Description                  │
 *   ├─────────────┼─────────┼──────────────────────────────┤
 *   │ collections │ 1.2.0   │ Generic data structures      │
 *   │ net         │ 0.5.1   │ HTTP/TCP networking library  │
 *   └─────────────┴─────────┴──────────────────────────────┘
 */

import core;
import io;
import string;
import collections.vec;

const char TL = '┌'; const char TR = '┐';
const char BL = '└'; const char BR = '┘';
const char HL = '─'; const char VL = '│';
const char TM = '┬'; const char BM = '┴';
const char ML = '├'; const char MR = '┤';
const char MM = '┼';

struct Table {
    Vec<str>  headers;
    Vec<str[]> rows;
    Vec<usize> col_widths;
}

fn Table.new() -> Table {
    return Table {
        headers:    Vec.new(),
        rows:       Vec.new(),
        col_widths: Vec.new(),
    };
}

fn Table.add_header(`mut Table self, `str header) -> void {
    self.headers.push(header);
    self.col_widths.push(string.len(header));
}

fn Table.add_row(`mut Table self, str[] cells) -> void {
    /* Update column widths */
    usize i = 0;
    while i < cells.len && i < self.col_widths.len() {
        usize cw = self.col_widths.get(i);
        if string.len(cells[i]) > cw {
            *self.col_widths.get_mut(i) = string.len(cells[i]);
        }
        i = i + 1;
    }
    self.rows.push(cells);
}

fn Table.print(`Table self) -> void {
    if self.headers.len() == 0 { return; }

    usize ncols = self.headers.len();
    /* usize[] widths already in self.col_widths */

    /* Top border */
    print_border(`self, TL, HL, TM, TR);
    /* Header row */
    print_row(`self, self.headers.as_slice());
    /* Header separator */
    print_border(`self, ML, HL, MM, MR);
    /* Data rows */
    for row in self.rows {
        print_row(`self, row);
    }
    /* Bottom border */
    print_border(`self, BL, HL, BM, BR);
}

fn print_border(`Table self, char left, char fill, char mid, char right) -> void {
    io.print(string.from_char(left));
    usize i = 0;
    while i < self.col_widths.len() {
        usize w = self.col_widths.get(i);
        io.print(string.repeat(string.from_char(fill), w + 2));
        if i + 1 < self.col_widths.len() {
            io.print(string.from_char(mid));
        }
        i = i + 1;
    }
    io.print(string.from_char(right));
    io.println("");
}

fn print_row(`Table self, str[] cells) -> void {
    io.print(string.from_char(VL));
    usize i = 0;
    while i < self.col_widths.len() {
        usize w   = self.col_widths.get(i);
        str   val = if i < cells.len { cells[i] } else { "" };
        usize pad = w - string.len(val);
        io.printf(" %s%s ", val, string.repeat(" ", pad));
        io.print(string.from_char(VL));
        i = i + 1;
    }
    io.println("");
}
