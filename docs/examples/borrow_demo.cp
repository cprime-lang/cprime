/*
 * C-Prime Example: Borrow Operator Showcase
 * docs/examples/borrow_demo.cp
 *
 * Demonstrates the ` (backtick) borrow operator.
 *
 * Compile: cpc borrow_demo.cp -o borrow_demo
 * Run:     ./borrow_demo
 */

import io;
import fmt;

struct Person {
    str  name;
    u32  age;
}

/* ─── Immutable borrow ─── */
fn greet(`Person p) -> void {
    io.printf("Hello, %s! You are %d years old.\n", p.name, p.age);
}   /* p is borrowed — NOT freed here */

/* ─── Mutable borrow ─── */
fn have_birthday(`mut Person p) -> void {
    p.age = p.age + 1;
    io.printf("%s is now %d!\n", p.name, p.age);
}

/* ─── Multiple immutable borrows are OK ─── */
fn compare_ages(`Person a, `Person b) -> void {
    if a.age > b.age {
        io.printf("%s is older.\n", a.name);
    } else if a.age < b.age {
        io.printf("%s is older.\n", b.name);
    } else {
        io.println("Same age!");
    }
}

/* ─── Move semantics ─── */
fn consume(Person p) -> void {
    io.printf("Consuming %s...\n", p.name);
}   /* p is freed here — owner is gone */

/* ─── Result type with borrows ─── */
fn find_by_name(`Person[] people, `str name) -> Option<`Person> {
    usize i = 0;
    while i < people.len {
        if string.eq(people[i].name, name) {
            return Some(`people[i]);   /* borrow from the array */
        }
        i = i + 1;
    }
    return None;
}

fn main() -> i32 {
    /* Create some people */
    Person alice = { name: "Alice", age: 30 };
    Person bob   = { name: "Bob",   age: 25 };

    /* Immutable borrows — alice and bob still owned by main */
    greet(`alice);
    greet(`bob);

    /* Mutable borrow — birthday! */
    have_birthday(`mut alice);

    /* Multiple borrows at once */
    compare_ages(`alice, `bob);

    /* Still valid after all the borrows */
    io.printf("Alice is still here: %s\n", alice.name);

    /* Move alice into consume — alice is gone after this */
    consume(alice);
    /* greet(`alice); would be a compile-time ERROR here */

    /* bob is still valid */
    io.printf("Bob is still here: %s\n", bob.name);

    return 0;
}
