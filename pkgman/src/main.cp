/*
 * cppm — C-Prime Package Manager
 * main.cp
 * =======
 * The cppm tool manages:
 *   - Installing C-Prime libraries from the registry
 *   - Checking for compiler/tool updates
 *   - Running cpc and cpg as sub-commands
 *   - Managing the local package cache
 *
 * Usage:
 *   cppm install <package>
 *   cppm search <query>
 *   cppm update
 *   cppm run <file.cp>
 *   cppm check <file.cp>       (runs cpg)
 *   cppm list
 *   cppm version
 */

import io;
import os;
import net;
import string;
import fmt;
import json;
import collections.vec;

import "cli/commands";
import "cli/banner";
import "package/installer";
import "package/manifest";
import "registry/client";
import "updater/check";
import "ui/progress";

const str CPPM_VERSION       = "0.1.0-alpha";
const str REGISTRY_URL       = "https://registry.cprime-lang.org";
const str RELEASES_URL       = "https://api.github.com/repos/cprime-lang/cprime/releases/latest";
const str CACHE_DIR          = "~/.cppm/cache";
const str PACKAGES_DIR       = "~/.cppm/packages";
const str CONFIG_FILE        = "~/.cppm/config.json";
const str MANIFEST_FILE      = "cprime.json";    /* project manifest */

/* ─── Package Manifest ────────────────────────────────────────────────────── */
struct Dependency {
    str name;
    str version;
}

struct Manifest {
    str           name;
    str           version;
    str           description;
    str           author;
    str           license;
    Dependency[]  dependencies;
    str[]         build_flags;
}

/* ─── CLI Commands ────────────────────────────────────────────────────────── */
struct Command {
    str  name;
    str  description;
    bool requires_network;
}

fn print_banner() -> void {
    io.println("\033[36m");
    io.println("  ██████╗██████╗ ██████╗ ███╗   ███╗");
    io.println(" ██╔════╝██╔══██╗██╔══██╗████╗ ████║");
    io.println(" ██║     ██████╔╝██████╔╝██╔████╔██║");
    io.println(" ██║     ██╔═══╝ ██╔═══╝ ██║╚██╔╝██║");
    io.println(" ╚██████╗██║     ██║     ██║ ╚═╝ ██║");
    io.println("  ╚═════╝╚═╝     ╚═╝     ╚═╝     ╚═╝");
    io.println("\033[0m");
    io.printf("  C-Prime Package Manager v%s\n", CPPM_VERSION);
    io.println("  Safe as Rust, Simple as C, Sharp as a backtick.");
    io.println("");
}

fn print_help() -> void {
    print_banner();
    io.println("Commands:");
    io.println("  install <package[@version]>  Install a package");
    io.println("  uninstall <package>          Remove a package");
    io.println("  search <query>               Search the registry");
    io.println("  list                         List installed packages");
    io.println("  update                       Check for cppm/cpc/cpg updates");
    io.println("  upgrade                      Upgrade all installed packages");
    io.println("  run <file.cp>                Compile and run a C-Prime file");
    io.println("  check <file.cp>              Run cpg (C-Prime Guard) on a file");
    io.println("  init [name]                  Create a new C-Prime project");
    io.println("  build                        Build the current project");
    io.println("  clean                        Clean build artifacts");
    io.println("  info <package>               Show package details");
    io.println("  version                      Show cppm version info");
    io.println("");
    io.println("Options:");
    io.println("  --no-color    Disable colored output");
    io.println("  --verbose     Verbose output");
    io.println("  --offline     Use cached packages only");
    io.println("  --help        Show this message");
    io.println("");
}

/* ─── Update Checker ─────────────────────────────────────────────────────── */
fn check_for_updates() -> void {
    io.println("[cppm] Checking for updates...");

    Result<str, str> response = net.http_get(RELEASES_URL);
    match response {
        Err(e) -> {
            io.printf("[cppm] \033[33mWarning:\033[0m could not check for updates: %s\n", e);
            return;
        },
        Ok(body) -> {
            Result<JsonValue, str> parsed = json.parse(body);
            match parsed {
                Err(_) -> return,
                Ok(data) -> {
                    str latest = json.get_str(`data, "tag_name");
                    str current = fmt.sprintf("v%s", CPPM_VERSION);
                    if !string.eq(latest, current) {
                        io.println("");
                        io.println("\033[33m╔══════════════════════════════════════════╗");
                        io.printf( "║  New version available: %-17s║\n", latest);
                        io.printf( "║  Current version:       %-17s║\n", current);
                        io.println("║  Run: cppm update to install            ║");
                        io.println("╚══════════════════════════════════════════╝\033[0m");
                        io.println("");
                    } else {
                        io.printf("[cppm] \033[32mUp to date\033[0m (v%s)\n", CPPM_VERSION);
                    }
                },
            }
        },
    }
}

/* ─── Package Install ─────────────────────────────────────────────────────── */
fn install_package(`str pkg_name, `str version) -> Result<void, str> {
    io.printf("[cppm] Installing: %s", pkg_name);
    if !string.is_empty(version) {
        io.printf("@%s", version);
    }
    io.println("");

    /* Fetch package info from registry */
    str url = fmt.sprintf("%s/packages/%s", REGISTRY_URL, pkg_name);
    Result<str, str> response = net.http_get(url);
    match response {
        Err(e) -> return Err(fmt.sprintf("registry error: %s", e)),
        Ok(body) -> {
            /* Parse and download package */
            Result<void, str> result = installer_install(body, PACKAGES_DIR, CACHE_DIR);
            return result;
        },
    }
}

/* ─── Project Init ────────────────────────────────────────────────────────── */
fn init_project(`str name) -> Result<void, str> {
    io.printf("[cppm] Creating project: %s\n", name);

    /* Create project directories */
    os.mkdir_p(fmt.sprintf("%s/src", name));
    os.mkdir_p(fmt.sprintf("%s/tests", name));

    /* Write manifest */
    str manifest = fmt.sprintf(
        "{\n  \"name\": \"%s\",\n  \"version\": \"0.1.0\",\n  \"description\": \"\",\n  \"dependencies\": {}\n}\n",
        name
    );
    os.write_file(fmt.sprintf("%s/%s", name, MANIFEST_FILE), manifest);

    /* Write hello world */
    str hello = "import io;\n\nfn main() -> i32 {\n    io.println(\"Hello, World!\");\n    return 0;\n}\n";
    os.write_file(fmt.sprintf("%s/src/main.cp", name), hello);

    /* Write .gitignore */
    str gitignore = "build/\ndist/\n*.cpobj\n.cprime_cache/\n";
    os.write_file(fmt.sprintf("%s/.gitignore", name), gitignore);

    io.printf("[cppm] \033[32mProject '%s' created!\033[0m\n", name);
    io.printf("[cppm] cd %s && cppm run src/main.cp\n", name);
    return Ok(void);
}

/* ─── Main ────────────────────────────────────────────────────────────────── */
fn main() -> i32 {
    str[] argv = os.get_args();
    i32   argc = os.get_argc();

    if argc < 2 {
        print_help();
        return 0;
    }

    str command = argv[1];

    /* Background update check (async, non-blocking) */
    os.spawn_detached(fn() -> void {
        check_for_updates();
    });

    if string.eq(command, "install") {
        if argc < 3 {
            io.println("cppm: error: 'install' requires a package name");
            return 1;
        }
        str pkg = argv[2];
        str ver = "";
        /* Parse pkg@version format */
        i32 at_pos = string.find_char(pkg, '@');
        if at_pos >= 0 {
            ver = string.slice(pkg, at_pos + 1, string.len(pkg));
            pkg = string.slice(pkg, 0, at_pos);
        }
        match install_package(`pkg, `ver) {
            Ok(_)  -> return 0,
            Err(e) -> { io.printf("cppm: error: %s\n", e); return 1; },
        }

    } else if string.eq(command, "init") {
        str name = if argc >= 3 { argv[2] } else { "my-cprime-project" };
        match init_project(`name) {
            Ok(_)  -> return 0,
            Err(e) -> { io.printf("cppm: error: %s\n", e); return 1; },
        }

    } else if string.eq(command, "run") {
        if argc < 3 { io.println("cppm: error: 'run' requires a file"); return 1; }
        str file = argv[2];
        str tmp_bin = fmt.sprintf("/tmp/cprime_run_%d", os.get_pid());
        i32 ret = os.exec("cpc", [file, "-o", tmp_bin]);
        if ret != 0 { return ret; }
        ret = os.exec(tmp_bin, []);
        os.remove(tmp_bin);
        return ret;

    } else if string.eq(command, "check") {
        if argc < 3 { io.println("cppm: error: 'check' requires a file"); return 1; }
        return os.exec("cpg", [argv[2]]);

    } else if string.eq(command, "update") {
        check_for_updates();
        return 0;

    } else if string.eq(command, "version") {
        print_banner();
        io.printf("cppm:  v%s\n", CPPM_VERSION);
        i32 cpc_ret = os.exec("cpc", ["--version"]);
        i32 cpg_ret = os.exec("cpg", ["--version"]);
        return 0;

    } else if string.eq(command, "--help") || string.eq(command, "help") {
        print_help();
        return 0;

    } else {
        io.printf("cppm: unknown command: %s\n", command);
        io.println("Run 'cppm --help' for usage.");
        return 1;
    }
}
