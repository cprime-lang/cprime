/*
 * cppm — Progress Bar & Spinner
 * pkgman/src/ui/progress.cp
 * ===========================
 * Terminal UI components for cppm:
 *   - Animated spinner for operations with unknown duration
 *   - Progress bar for operations with known progress (0–100%)
 *   - Status line updates using ANSI escape codes
 */

import core;
import io;
import os;
import thread;

const str SPIN_FRAMES = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
const str COLOR_OK    = "\033[32m";
const str COLOR_ERR   = "\033[31m";
const str COLOR_INFO  = "\033[36m";
const str RESET       = "\033[0m";
const str CLEAR_LINE  = "\033[2K\r";

struct Progress {
    str    label;
    u32    percent;
    bool   done;
    bool   failed;
    u32    spin_frame;
    bool   is_tty;
}

fn Progress.new(`str label) -> Progress {
    return Progress {
        label:      label,
        percent:    0,
        done:       false,
        failed:     false,
        spin_frame: 0,
        is_tty:     os.is_tty(1),
    };
}

fn Progress.start(`mut Progress self) -> void {
    if !self.is_tty { io.printf("[cppm] %s...\n", self.label); return; }
    self.render();
}

fn Progress.update(`mut Progress self, `str msg, u32 pct) -> void {
    self.label   = msg;
    self.percent = if pct > 100 { 100 } else { pct };
    self.spin_frame = (self.spin_frame + 1) % 10;
    if self.is_tty { self.render(); }
}

fn Progress.done(`mut Progress self) -> void {
    self.done    = true;
    self.percent = 100;
    if self.is_tty {
        io.printf("%s%s✓%s %s\n", CLEAR_LINE, COLOR_OK, RESET, self.label);
    } else {
        io.printf("[cppm] ✓ %s\n", self.label);
    }
}

fn Progress.fail(`mut Progress self) -> void {
    self.failed = true;
    if self.is_tty {
        io.printf("%s%s✗%s %s\n", CLEAR_LINE, COLOR_ERR, RESET, self.label);
    } else {
        io.eprintf("[cppm] ✗ %s\n", self.label);
    }
}

fn Progress.render(`Progress self) -> void {
    /* spinner char */
    char spin = string.char_at(SPIN_FRAMES, (self.spin_frame * 3) as usize);

    /* progress bar: [████░░░░░░] 40% */
    u32 bar_width = 20;
    u32 filled    = (self.percent * bar_width) / 100;
    str bar       = "";
    u32 i = 0;
    while i < bar_width {
        bar = string.push_char(bar, if i < filled { '█' } else { '░' });
        i = i + 1;
    }

    io.printf("%s%s%c%s [%s] %d%% %s",
              CLEAR_LINE, COLOR_INFO, spin, RESET,
              bar, self.percent, self.label);
    /* flush stdout */
    io.flush_stdout();
}

/* ─── Simple line status (no animation) ─────────────────────────────────── */
fn status_ok(`str msg) -> void {
    io.printf("  %s✓%s  %s\n", COLOR_OK, RESET, msg);
}

fn status_fail(`str msg) -> void {
    io.eprintf("  %s✗%s  %s\n", COLOR_ERR, RESET, msg);
}

fn status_info(`str msg) -> void {
    io.printf("  %s→%s  %s\n", COLOR_INFO, RESET, msg);
}
