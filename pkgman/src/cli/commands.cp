/*
 * cppm — CLI Commands
 * pkgman/src/cli/commands.cp
 * ===========================
 * Implements all cppm subcommands.
 * Each command is a standalone function called from main().
 *
 * Commands:
 *   install   — download and install a package from the registry
 *   uninstall — remove an installed package
 *   search    — search the registry
 *   list      — list installed packages
 *   update    — check for compiler/tool updates
 *   upgrade   — upgrade all installed packages to latest
 *   run       — compile and run a .cp file
 *   check     — run cpg on a file
 *   init      — scaffold a new C-Prime project
 *   build     — build the current project using cprime.json
 *   clean     — remove build artifacts
 *   info      — show details about a package
 *   version   — show cppm version and all tool versions
 */

import core;
import io;
import os;
import net;
import string;
import fmt;
import json;
import collections.vec;
import "pkgman/src/package/installer";
import "pkgman/src/package/manifest";
import "pkgman/src/registry/client";
import "pkgman/src/updater/check";
import "pkgman/src/ui/progress";
import "pkgman/src/ui/table";

const str CPPM_VERSION = "0.1.0-alpha";
const str PACKAGES_DIR = "~/.cppm/packages";
const str CACHE_DIR    = "~/.cppm/cache";

/* ─── cmd_install ─────────────────────────────────────────────────────────── */
fn cmd_install(`str pkg_spec, bool verbose) -> i32 {
    /* Parse pkg_spec — may be "name" or "name@version" */
    str pkg_name = pkg_spec;
    str pkg_ver  = "latest";
    i32 at = string.find_char(pkg_spec, '@');
    if at >= 0 {
        pkg_name = string.slice(pkg_spec, 0, at as usize);
        pkg_ver  = string.slice(pkg_spec, at as usize + 1, string.len(pkg_spec));
    }

    io.printf("[cppm] Installing %s@%s...\n", pkg_name, pkg_ver);

    Progress bar = Progress.new(fmt.sprintf("Fetching %s", pkg_name));
    bar.start();

    match registry_fetch_package(pkg_name, pkg_ver) {
        Err(e) -> {
            bar.fail();
            io.eprintf("[cppm] error: could not fetch package '%s': %s\n", pkg_name, e);
            return 1;
        },
        Ok(pkg_info) -> {
            bar.update("Downloading...", 25);
            match installer_install(`pkg_info, PACKAGES_DIR, CACHE_DIR) {
                Err(e) -> {
                    bar.fail();
                    io.eprintf("[cppm] error: install failed: %s\n", e);
                    return 1;
                },
                Ok(_) -> {
                    bar.done();
                    io.printf("[cppm] \033[32m✓\033[0m Installed %s@%s\n",
                              pkg_name, pkg_info.version);
                    return 0;
                },
            }
        },
    }
}

/* ─── cmd_uninstall ───────────────────────────────────────────────────────── */
fn cmd_uninstall(`str pkg_name) -> i32 {
    str pkg_dir = fmt.sprintf("%s/%s", PACKAGES_DIR, pkg_name);

    if !os.dir_exists(pkg_dir) {
        io.eprintf("[cppm] '%s' is not installed\n", pkg_name);
        return 1;
    }

    match os.remove_dir_all(pkg_dir) {
        Err(e) -> {
            io.eprintf("[cppm] error: could not remove '%s': %s\n", pkg_name, e);
            return 1;
        },
        Ok(_) -> {
            io.printf("[cppm] \033[32m✓\033[0m Uninstalled %s\n", pkg_name);
            return 0;
        },
    }
}

/* ─── cmd_search ──────────────────────────────────────────────────────────── */
fn cmd_search(`str query) -> i32 {
    io.printf("[cppm] Searching registry for '%s'...\n\n", query);

    match registry_search(query) {
        Err(e) -> {
            io.eprintf("[cppm] error: registry search failed: %s\n", e);
            return 1;
        },
        Ok(results) -> {
            if results.len() == 0 {
                io.printf("No packages found matching '%s'\n", query);
                return 0;
            }

            /* Print results as a table */
            Table t = Table.new();
            t.add_header("Name");
            t.add_header("Version");
            t.add_header("Description");
            t.add_header("Downloads");

            for pkg in results {
                t.add_row([pkg.name, pkg.version, pkg.description, pkg.download_count]);
            }
            t.print();
            return 0;
        },
    }
}

/* ─── cmd_list ────────────────────────────────────────────────────────────── */
fn cmd_list() -> i32 {
    Vec<str> installed = os.list_dir(PACKAGES_DIR);

    if installed.len() == 0 {
        io.println("No packages installed.");
        io.println("Run 'cppm install <package>' to install one.");
        return 0;
    }

    io.printf("Installed packages (%d):\n\n", installed.len());

    Table t = Table.new();
    t.add_header("Package");
    t.add_header("Version");
    t.add_header("Location");

    for pkg_name in installed {
        str manifest_path = fmt.sprintf("%s/%s/cprime.json", PACKAGES_DIR, pkg_name);
        Manifest m = manifest_load(manifest_path);
        t.add_row([m.name, m.version, fmt.sprintf("%s/%s", PACKAGES_DIR, pkg_name)]);
    }
    t.print();
    return 0;
}

/* ─── cmd_update ──────────────────────────────────────────────────────────── */
fn cmd_update() -> i32 {
    io.println("[cppm] Checking for updates...\n");
    return check_for_updates(true);
}

/* ─── cmd_upgrade ─────────────────────────────────────────────────────────── */
fn cmd_upgrade() -> i32 {
    io.println("[cppm] Upgrading all packages...\n");
    Vec<str> installed = os.list_dir(PACKAGES_DIR);
    u32 upgraded = 0;

    for pkg_name in installed {
        match registry_fetch_latest(pkg_name) {
            Err(_)       -> {},
            Ok(pkg_info) -> {
                str manifest_path = fmt.sprintf("%s/%s/cprime.json", PACKAGES_DIR, pkg_name);
                Manifest m = manifest_load(manifest_path);

                if !string.eq(m.version, pkg_info.version) {
                    io.printf("  Upgrading %s: %s → %s\n",
                              pkg_name, m.version, pkg_info.version);
                    cmd_install(`pkg_name, false);
                    upgraded = upgraded + 1;
                }
            },
        }
    }

    if upgraded == 0 {
        io.println("\033[32mAll packages are up to date.\033[0m");
    } else {
        io.printf("\n\033[32mUpgraded %d package(s).\033[0m\n", upgraded);
    }
    return 0;
}

/* ─── cmd_run ─────────────────────────────────────────────────────────────── */
fn cmd_run(`str file, bool optimize) -> i32 {
    if !os.file_exists(file) {
        io.eprintf("[cppm] error: file not found: %s\n", file);
        return 1;
    }

    str tmp_bin = fmt.sprintf("/tmp/cprime_run_%d", os.get_pid());
    str cpc_args = fmt.sprintf("%s -o %s%s",
                               file, tmp_bin,
                               if optimize { " -O" } else { "" });

    io.printf("[cppm] Compiling %s...\n", file);
    i32 rc = os.exec("cpc", [file, "-o", tmp_bin]);
    if rc != 0 {
        os.remove(tmp_bin);
        return rc;
    }

    rc = os.exec_replace(tmp_bin, []);
    os.remove(tmp_bin);
    return rc;
}

/* ─── cmd_check ───────────────────────────────────────────────────────────── */
fn cmd_check(`str file, bool strict, bool json_out) -> i32 {
    if !os.file_exists(file) {
        io.eprintf("[cppm] error: file not found: %s\n", file);
        return 1;
    }

    Vec<str> args = [file];
    if strict   { args.push("--strict");       }
    if json_out { args.push("--report=json");  }

    return os.exec("cpg", args.as_slice());
}

/* ─── cmd_init ────────────────────────────────────────────────────────────── */
fn cmd_init(`str project_name) -> i32 {
    if os.dir_exists(project_name) {
        io.eprintf("[cppm] error: directory '%s' already exists\n", project_name);
        return 1;
    }

    os.mkdir_p(fmt.sprintf("%s/src",   project_name));
    os.mkdir_p(fmt.sprintf("%s/tests", project_name));
    os.mkdir_p(fmt.sprintf("%s/docs",  project_name));

    /* cprime.json manifest */
    str manifest_content = fmt.sprintf(
        "{\n  \"name\": \"%s\",\n  \"version\": \"0.1.0\",\n"
        "  \"description\": \"\",\n  \"author\": \"\",\n"
        "  \"license\": \"MIT\",\n  \"dependencies\": {}\n}\n",
        project_name);
    os.write_file(fmt.sprintf("%s/cprime.json", project_name), manifest_content);

    /* src/main.cp */
    str main_content =
        "import io;\n\nfn main() -> i32 {\n"
        "    io.println(\"Hello from " + project_name + "!\");\n"
        "    return 0;\n}\n";
    os.write_file(fmt.sprintf("%s/src/main.cp", project_name), main_content);

    /* README.md */
    str readme = fmt.sprintf("# %s\n\nA C-Prime project.\n\n## Build\n\n```bash\ncppm build\n```\n\n## Run\n\n```bash\ncppm run src/main.cp\n```\n", project_name);
    os.write_file(fmt.sprintf("%s/README.md", project_name), readme);

    /* .gitignore */
    os.write_file(fmt.sprintf("%s/.gitignore", project_name),
                  "build/\ndist/\n*.cpobj\n.cprime_cache/\n");

    io.printf("\033[32m[cppm] Project '%s' created!\033[0m\n\n", project_name);
    io.printf("  cd %s\n", project_name);
    io.printf("  cppm run src/main.cp\n\n");
    return 0;
}

/* ─── cmd_build ───────────────────────────────────────────────────────────── */
fn cmd_build(bool optimize) -> i32 {
    if !os.file_exists("cprime.json") {
        io.eprintln("[cppm] error: no cprime.json found — are you in a C-Prime project?");
        return 1;
    }

    Manifest m = manifest_load("cprime.json");
    io.printf("[cppm] Building %s v%s...\n", m.name, m.version);

    os.mkdir_p("build");
    str out = fmt.sprintf("build/%s", m.name);
    str main = if os.file_exists("src/main.cp") { "src/main.cp" } else { "main.cp" };

    Vec<str> args = [main, "-o", out];
    if optimize { args.push("-O"); }

    i32 rc = os.exec("cpc", args.as_slice());
    if rc == 0 {
        io.printf("[cppm] \033[32m✓\033[0m Built: %s\n", out);
    }
    return rc;
}

/* ─── cmd_clean ───────────────────────────────────────────────────────────── */
fn cmd_clean() -> i32 {
    Vec<str> to_remove = ["build", "dist", ".cprime_cache"];
    for dir in to_remove {
        if os.dir_exists(dir) {
            os.remove_dir_all(dir);
            io.printf("[cppm] Removed: %s/\n", dir);
        }
    }
    io.println("[cppm] Clean done.");
    return 0;
}

/* ─── cmd_info ────────────────────────────────────────────────────────────── */
fn cmd_info(`str pkg_name) -> i32 {
    match registry_fetch_package(pkg_name, "latest") {
        Err(e) -> {
            io.eprintf("[cppm] error: %s\n", e);
            return 1;
        },
        Ok(pkg) -> {
            io.printf("Name:        %s\n", pkg.name);
            io.printf("Version:     %s\n", pkg.version);
            io.printf("Description: %s\n", pkg.description);
            io.printf("License:     %s\n", pkg.license);
            io.printf("Author:      %s\n", pkg.author);
            io.printf("Homepage:    %s\n", pkg.homepage);
            io.printf("Downloads:   %s\n", pkg.download_count);
            if pkg.dependencies.len() > 0 {
                io.println("Dependencies:");
                for dep in pkg.dependencies {
                    io.printf("  %s@%s\n", dep.name, dep.version);
                }
            }
            return 0;
        },
    }
}

/* ─── cmd_version ─────────────────────────────────────────────────────────── */
fn cmd_version() -> i32 {
    io.printf("cppm  v%s\n", CPPM_VERSION);
    os.exec("cpc",  ["--version"]);
    os.exec("cpg",  ["--version"]);
    return 0;
}
