# Contributing to cppm (C-Prime Package Manager)

## Adding a new command

1. Write the implementation in `src/cli/commands.cp`

```c
fn cmd_mycommand(`str arg, bool verbose) -> i32 {
    io.printf("[cppm] Running mycommand: %s\n", arg);
    /* implementation */
    return 0;
}
```

2. Register it in `src/main.cp` in the command dispatch:

```c
} else if string.eq(command, "mycommand") {
    if argc < 3 { io.eprintln("usage: cppm mycommand <arg>"); return 1; }
    return cmd_mycommand(`argv[2], opts.verbose);
}
```

3. Add it to the help text in `print_help()` in `src/main.cp`

4. Document it in `pkgman/docs/COMMANDS.md`

## Adding a new UI component

UI components live in `src/ui/`. Follow the pattern in `progress.cp`:
- Check `os.is_tty(1)` before emitting ANSI codes
- Always provide a non-TTY fallback
- Use the constants `COLOR_OK`, `COLOR_ERR`, `COLOR_INFO`, `RESET`

## Working with the registry

The registry client is in `src/registry/client.cp`.
All registry API calls should:
- Set the `User-Agent: cppm/VERSION` header
- Handle network errors gracefully (return `Err(...)` not `panic`)
- Respect the 24-hour update cache TTL

## Testing

```bash
# Run all cppm tests
../../scripts/test.sh

# Test a specific command manually
./build/pkgman/cppm version
./build/pkgman/cppm help
./build/pkgman/cppm init test_project
```
