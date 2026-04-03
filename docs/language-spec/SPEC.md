# C-Prime Language Specification
## Version 0.1.0-alpha | Codename: Backtick

---

## 1. Introduction

C-Prime (`C\``) is a statically typed, memory-safe systems programming language. It maintains C's simplicity and low-level control while adding compile-time memory safety through a borrow checker, powered by the `` ` `` (backtick) operator.

**Design Goals:**
1. If you know C, you can read C-Prime immediately
2. Memory safety must be zero-cost — all checks happen at compile time
3. The `` ` `` symbol makes borrows explicit and visually clear
4. No hidden allocations, no garbage collector
5. Direct machine code output — no VM, no interpreter

---

## 2. File Extension

C-Prime source files use the `.cp` extension.

---

## 3. Basic Syntax

### 3.1 Comments

```c
// Single-line comment

/*
 * Multi-line comment
 */

/// Documentation comment (parsed by cpg and docs generator)
/// @param name - The name to print
fn greet(name `str) -> void { ... }
```

### 3.2 Variables

```c
// Owned variable (mutable by default in C-Prime)
i32 x = 42;

// Immutable variable
const i32 MAX = 100;

// Type inference
auto y = 3.14;    // inferred as f64

// Uninitialized (must initialize before use — compiler enforces this)
i32 z;
z = 10;           // OK
```

### 3.3 Primitive Types

| Type     | Size     | Description              |
|----------|----------|--------------------------|
| `i8`     | 1 byte   | Signed 8-bit integer     |
| `i16`    | 2 bytes  | Signed 16-bit integer    |
| `i32`    | 4 bytes  | Signed 32-bit integer    |
| `i64`    | 8 bytes  | Signed 64-bit integer    |
| `u8`     | 1 byte   | Unsigned 8-bit integer   |
| `u16`    | 2 bytes  | Unsigned 16-bit integer  |
| `u32`    | 4 bytes  | Unsigned 32-bit integer  |
| `u64`    | 8 bytes  | Unsigned 64-bit integer  |
| `f32`    | 4 bytes  | 32-bit floating point    |
| `f64`    | 8 bytes  | 64-bit floating point    |
| `bool`   | 1 byte   | Boolean (true/false)     |
| `char`   | 4 bytes  | Unicode scalar value     |
| `str`    | fat ptr  | String slice (ptr + len) |
| `byte`   | 1 byte   | Raw byte (alias for u8)  |
| `usize`  | 8 bytes  | Platform pointer size    |
| `isize`  | 8 bytes  | Platform signed ptr size |
| `void`   | 0 bytes  | No value                 |

---

## 4. The Borrow Operator `` ` ``

This is the core of C-Prime's memory model.

### 4.1 Ownership

Every value has exactly **one owner**. When the owner goes out of scope, the value is freed.

```c
fn main() -> i32 {
    str name = "Alice";     // name owns the string
    // name is freed here automatically
    return 0;
}
```

### 4.2 Move Semantics

Assigning a non-primitive type **moves** ownership:

```c
fn main() -> i32 {
    str a = "hello";
    str b = a;         // ownership MOVED to b
    // io.println(a);  // ERROR: a was moved, compiler rejects this
    io.println(b);     // OK
    return 0;
}
```

### 4.3 Borrowing with `` ` ``

Use `` ` `` to borrow a value without taking ownership:

```c
fn print_it(val `str) -> void {
    io.println(val);
}   // borrow ends here. val is NOT freed.

fn main() -> i32 {
    str message = "C-Prime";
    print_it(`message);        // borrow message
    io.println(message);       // still valid! we only borrowed
    return 0;
}
```

### 4.4 Mutable Borrowing with `` `mut ``

```c
fn double_it(val `mut i32) -> void {
    *val = *val * 2;
}

fn main() -> i32 {
    i32 x = 5;
    double_it(`mut x);
    io.println(x);    // prints 10
    return 0;
}
```

### 4.5 Borrow Rules (enforced by compiler)

1. You can have **many immutable borrows** OR **exactly one mutable borrow** — not both.
2. A borrow cannot outlive the owner.
3. You cannot mutate through an immutable borrow.

```c
fn main() -> i32 {
    i32 x = 42;
    `i32 r1 = `x;    // immutable borrow — OK
    `i32 r2 = `x;    // second immutable borrow — OK
    // `mut i32 rm = `mut x;  // ERROR: cannot borrow as mutable while immutably borrowed
    return 0;
}
```

---

## 5. Functions

```c
// Basic function
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

// Void function
fn log_message(`str msg) -> void {
    io.println(msg);
}

// Function with multiple return (via struct)
fn divide(i32 a, i32 b) -> Result<i32, str> {
    if b == 0 {
        return Err("Division by zero");
    }
    return Ok(a / b);
}
```

---

## 6. Control Flow

```c
// if / else
if x > 10 {
    io.println("big");
} else if x > 5 {
    io.println("medium");
} else {
    io.println("small");
}

// while
while i < 10 {
    i = i + 1;
}

// for (range-based)
for i in 0..10 {
    io.println(i);
}

// for (C-style)
for (i32 i = 0; i < 10; i++) {
    io.println(i);
}

// match (like switch, but exhaustive)
match x {
    1  -> io.println("one"),
    2  -> io.println("two"),
    _  -> io.println("other"),
}
```

---

## 7. Structs

```c
struct Point {
    f64 x;
    f64 y;
}

struct Person {
    str name;
    u32 age;
}

fn greet(`Person p) -> void {
    io.printf("Hello, %s! You are %d years old.\n", p.name, p.age);
}

fn main() -> i32 {
    Person alice = { name: "Alice", age: 25 };
    greet(`alice);
    return 0;
}
```

---

## 8. Pointers

C-Prime keeps raw pointers for FFI and low-level code, but they are marked `unsafe`:

```c
unsafe {
    i32* raw = malloc(sizeof(i32));
    *raw = 99;
    free(raw);
}
```

Prefer borrows (`` ` ``) over raw pointers in safe code.

---

## 9. Option and Result

```c
// Option<T> — replaces null
Option<str> find_name(i32 id) {
    if id == 1 { return Some("Alice"); }
    return None;
}

// Result<T, E> — replaces error codes
Result<i32, str> parse_int(`str s) {
    // ... parsing logic
}

// Using them
fn main() -> i32 {
    Option<str> name = find_name(1);
    match name {
        Some(n) -> io.println(n),
        None    -> io.println("not found"),
    }
    return 0;
}
```

---

## 10. Imports

```c
import io;                  // stdlib/io
import mem;                 // stdlib/mem
import collections.vec;    // stdlib/collections/vec
import "mylib/utils";      // local file import
```

---

## 11. Generics

```c
fn max<T>(T a, T b) -> T {
    if a > b { return a; }
    return b;
}

struct Stack<T> {
    T* data;
    usize len;
    usize cap;
}
```

---

## 12. Inline Assembly

```c
fn cpuid() -> u32 {
    u32 result;
    asm {
        mov eax, 0
        cpuid
        mov [result], eax
    }
    return result;
}
```

---

## 13. Compilation Model

```
source.cp
    │
    ▼
[Lexer] → Token stream
    │
    ▼
[Parser] → Abstract Syntax Tree (AST)
    │
    ▼
[Semantic Analyzer] → Typed AST + Borrow Checker
    │
    ▼
[Optimizer] → Optimized IR
    │
    ▼
[Code Generator] → x86_64 / ARM64 Assembly
    │
    ▼
[Assembler + Linker] → Native ELF Binary
```

---

*This spec is a living document. It evolves as the compiler is implemented.*
