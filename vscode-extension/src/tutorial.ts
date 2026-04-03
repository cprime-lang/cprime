/**
 * C-Prime Tutorial Provider — tutorial.ts
 * =========================================
 * Full interactive tutorial panel with:
 *   - Sidebar navigation (6 lessons)
 *   - Syntax-highlighted code examples
 *   - "Try it" buttons that open a new editor with example code
 *   - Progress tracking per session
 */

import * as vscode from 'vscode';

// ─── Tutorial Lessons ─────────────────────────────────────────────────────────

export interface Lesson {
    id:    string;
    title: string;
    icon:  string;
    html:  string;
    code?: string;   // Code to open when "Try it" is clicked
}

export const LESSONS: Lesson[] = [
    {
        id: 'intro',
        title: 'Introduction',
        icon: '🔷',
        code: `import io;

fn main() -> i32 {
    io.println("Hello, C-Prime!");
    return 0;
}`,
        html: `
<h2>Welcome to C-Prime</h2>
<p>C-Prime (<code>C\`</code>) is a systems programming language that combines
   C's simple syntax with Rust-style memory safety.</p>
<div class="tagline">Safe as Rust &nbsp;·&nbsp; Simple as C &nbsp;·&nbsp; Sharp as a backtick</div>
<h3>Your first program</h3>
<pre><code><span class="kw">import</span> io;

<span class="kw">fn</span> <span class="fn-name">main</span>() -&gt; <span class="type">i32</span> {
    io.println(<span class="str">"Hello, C-Prime!"</span>);
    <span class="kw">return</span> 0;
}</code></pre>
<p>Compile and run:</p>
<pre class="shell"><code>cpc hello.cp -o hello &amp;&amp; ./hello
<span class="comment"># or simply:</span>
cppm run hello.cp</code></pre>
<p>If you know C, you already know most of this. C-Prime adds one big thing:
   the <strong>borrow operator</strong>, which we'll learn in the next lesson.</p>`
    },
    {
        id: 'borrow',
        title: 'The Borrow Operator',
        icon: '⚡',
        code: 'import io;\n\nfn greet(name \`str) -> void {\n    io.println(name);\n}   // name is borrowed\n\nfn double_it(val \`mut i32) -> void {\n    *val = *val * 2;\n}\n\nfn main() -> i32 {\n    str message = "C-Prime";\n    greet(\`message);\n    io.println(message);\n\n    i32 x = 21;\n    double_it(\`mut x);\n    io.printf("%d\\n", x);\n    return 0;\n}',
        html: '<h2>The &#96; Borrow Operator</h2>' +
            '<p>The backtick is the heart of C-Prime. It lets you <strong>borrow</strong> a value without taking ownership.</p>' +
            '<h3>Immutable borrow</h3>' +
            '<pre><code><span class="kw">fn</span> <span class="fn-name">greet</span>(name <span class="borrow">&#96;</span><span class="type">str</span>) -&gt; <span class="type">void</span> {\n' +
            '    io.println(name);\n' +
            '}   <span class="comment">// name is NOT freed</span>\n\n' +
            '<span class="kw">fn</span> <span class="fn-name">main</span>() -&gt; <span class="type">i32</span> {\n' +
            '    <span class="type">str</span> message = <span class="str">"C-Prime"</span>;\n' +
            '    greet(<span class="borrow">&#96;</span>message);      <span class="comment">// borrow</span>\n' +
            '    io.println(message); <span class="comment">// still valid!</span>\n' +
            '    <span class="kw">return</span> 0;\n' +
            '}</code></pre>' +
            '<h3>Mutable borrow</h3>' +
            '<pre><code><span class="kw">fn</span> <span class="fn-name">double_it</span>(val <span class="borrow">&#96;mut</span> <span class="type">i32</span>) -&gt; <span class="type">void</span> {\n' +
            '    *val = *val * 2;\n' +
            '}\n\n' +
            '<span class="type">i32</span> x = 21;\n' +
            'double_it(<span class="borrow">&#96;mut</span> x);   <span class="comment">// mutable borrow</span>\n' +
            '<span class="comment">// x is now 42</span></code></pre>' +
            '<div class="rules-box"><h3>Borrow Rules (compile time)</h3><ul>' +
            '<li>✅ Many immutable borrows at once</li>' +
            '<li>✅ OR exactly one mutable borrow</li>' +
            '<li>❌ Cannot mix mutable + immutable borrows</li>' +
            '<li>❌ A borrow cannot outlive the owner</li>' +
            '</ul></div>'
    },
    {
        id: 'types',
        title: 'Types & Variables',
        icon: '📦',
        code: `import io;
import fmt;

fn main() -> i32 {
    // Primitive types
    i32  count = 42;
    f64  pi    = 3.14159;
    bool flag  = true;
    str  name  = "C-Prime";
    char letter = 'A';

    // Type inference
    auto n = 100;       // inferred as i32
    auto msg = "hello"; // inferred as str

    // Constants
    const i32 MAX = 1000;

    io.printf("count=%d pi=%f flag=%s\\n", count, pi, flag ? "true" : "false");
    io.printf("name=%s letter=%c n=%d MAX=%d\\n", name, letter, n, MAX);

    return 0;
}`,
        html: `
<h2>Types &amp; Variables</h2>

<h3>Primitive Types</h3>
<table>
<tr><th>Type</th><th>Size</th><th>Description</th></tr>
<tr><td><code>i8/i16/i32/i64</code></td><td>1-8B</td><td>Signed integers</td></tr>
<tr><td><code>u8/u16/u32/u64</code></td><td>1-8B</td><td>Unsigned integers</td></tr>
<tr><td><code>f32/f64</code></td><td>4/8B</td><td>Floating point</td></tr>
<tr><td><code>bool</code></td><td>1B</td><td>true / false</td></tr>
<tr><td><code>char</code></td><td>4B</td><td>Unicode scalar</td></tr>
<tr><td><code>str</code></td><td>16B</td><td>String slice (ptr+len)</td></tr>
<tr><td><code>usize/isize</code></td><td>8B</td><td>Pointer-sized</td></tr>
</table>

<h3>Declaring Variables</h3>
<pre><code><span class="type">i32</span>  x     = 42;
<span class="type">f64</span>  pi    = 3.14;
<span class="type">bool</span> flag  = <span class="kw">true</span>;
<span class="type">str</span>  name  = <span class="str">"hello"</span>;
<span class="kw">auto</span> n     = 100;     <span class="comment">// type inferred</span>
<span class="kw">const</span> <span class="type">i32</span> MAX = 1000; <span class="comment">// compile-time constant</span></code></pre>
<p>Variables are mutable by default. Use <code>const</code> to make them immutable.</p>`
    },
    {
        id: 'control',
        title: 'Control Flow',
        icon: '🔀',
        code: `import io;

fn classify(i32 n) -> str {
    if n < 0 {
        return "negative";
    } else if n == 0 {
        return "zero";
    } else {
        return "positive";
    }
}

fn main() -> i32 {
    // For range loop
    for i in 0..5 {
        io.printf("%d ", i);
    }
    io.println("");

    // While loop
    i32 x = 10;
    while x > 0 {
        x = x - 2;
    }

    // Match
    i32 code = 2;
    match code {
        1 -> io.println("one"),
        2 -> io.println("two"),
        3 -> io.println("three"),
        _ -> io.println("other"),
    }

    io.println(classify(-5));
    io.println(classify(0));
    io.println(classify(7));
    return 0;
}`,
        html: `
<h2>Control Flow</h2>

<h3>if / else if / else</h3>
<pre><code><span class="kw">if</span> x &lt; 0 {
    io.println(<span class="str">"negative"</span>);
} <span class="kw">else if</span> x == 0 {
    io.println(<span class="str">"zero"</span>);
} <span class="kw">else</span> {
    io.println(<span class="str">"positive"</span>);
}</code></pre>

<h3>For range loop</h3>
<pre><code><span class="kw">for</span> i <span class="kw">in</span> 0..10 {
    io.println(i);   <span class="comment">// prints 0 through 9</span>
}</code></pre>

<h3>While loop</h3>
<pre><code><span class="kw">while</span> x &gt; 0 {
    x = x - 1;
}</code></pre>

<h3>Match (exhaustive switch)</h3>
<pre><code><span class="kw">match</span> value {
    1  -&gt; io.println(<span class="str">"one"</span>),
    2  -&gt; io.println(<span class="str">"two"</span>),
    _  -&gt; io.println(<span class="str">"other"</span>),   <span class="comment">// wildcard — must be present</span>
}</code></pre>
<p>The <code>_</code> wildcard makes match exhaustive — the compiler errors if you forget a case.</p>`
    },
    {
        id: 'result',
        title: 'Option & Result',
        icon: '🛡️',
        code: `import io;

// No null — use Option<T> instead
fn find_item(i32 id) -> Option<str> {
    if id == 1 { return Some("Apple"); }
    if id == 2 { return Some("Banana"); }
    return None;
}

// No error codes — use Result<T,E> instead
fn safe_divide(i32 a, i32 b) -> Result<i32, str> {
    if b == 0 {
        return Err("division by zero");
    }
    return Ok(a / b);
}

fn main() -> i32 {
    // Option
    match find_item(1) {
        Some(name) -> io.printf("Found: %s\\n", name),
        None       -> io.println("Not found"),
    }

    // Result
    match safe_divide(10, 2) {
        Ok(n)  -> io.printf("Result: %d\\n", n),
        Err(e) -> io.printf("Error: %s\\n", e),
    }

    match safe_divide(10, 0) {
        Ok(n)  -> io.printf("Result: %d\\n", n),
        Err(e) -> io.printf("Error: %s\\n", e),
    }

    return 0;
}`,
        html: `
<h2>Option&lt;T&gt; &amp; Result&lt;T, E&gt;</h2>
<p>C-Prime has <strong>no null pointers</strong> and <strong>no implicit error codes</strong>.
   Instead, use these safe wrapper types.</p>

<h3>Option&lt;T&gt; — replace null</h3>
<pre><code><span class="kw">fn</span> <span class="fn-name">find_name</span>(<span class="type">i32</span> id) -> Option&lt;<span class="type">str</span>&gt; {
    <span class="kw">if</span> id == 1 { <span class="kw">return</span> Some(<span class="str">"Alice"</span>); }
    <span class="kw">return</span> <span class="kw">None</span>;
}

<span class="kw">match</span> find_name(1) {
    Some(name) -&gt; io.println(name),
    <span class="kw">None</span>       -&gt; io.println(<span class="str">"not found"</span>),
}</code></pre>

<h3>Result&lt;T, E&gt; — replace error codes</h3>
<pre><code><span class="kw">fn</span> <span class="fn-name">divide</span>(<span class="type">i32</span> a, <span class="type">i32</span> b) -> Result&lt;<span class="type">i32</span>, <span class="type">str</span>&gt; {
    <span class="kw">if</span> b == 0 { <span class="kw">return</span> Err(<span class="str">"divide by zero"</span>); }
    <span class="kw">return</span> Ok(a / b);
}

<span class="kw">match</span> divide(10, 2) {
    Ok(n)  -&gt; io.printf(<span class="str">"%d\\n"</span>, n),
    Err(e) -&gt; io.printf(<span class="str">"Error: %s\\n"</span>, e),
}</code></pre>
<p>The compiler forces you to handle both <code>Ok</code> and <code>Err</code> — no forgotten error checks.</p>`
    },
    {
        id: 'structs',
        title: 'Structs & Methods',
        icon: '🏗️',
        code: `import io;
import math;

struct Point {
    f64 x;
    f64 y;
}

fn Point.distance(\`Point self, \`Point other) -> f64 {
    f64 dx = other.x - self.x;
    f64 dy = other.y - self.y;
    return math.sqrt(dx*dx + dy*dy);
}

fn Point.to_string(\`Point self) -> str {
    return fmt.sprintf("(%f, %f)", self.x, self.y);
}

struct Rectangle {
    Point top_left;
    Point bottom_right;
}

fn Rectangle.area(\`Rectangle self) -> f64 {
    f64 w = self.bottom_right.x - self.top_left.x;
    f64 h = self.bottom_right.y - self.top_left.y;
    return w * h;
}

fn main() -> i32 {
    Point a = { x: 0.0, y: 0.0 };
    Point b = { x: 3.0, y: 4.0 };

    f64 dist = a.distance(\`b);
    io.printf("Distance: %f\\n", dist);   // 5.0

    Rectangle r = {
        top_left:     { x: 0.0, y: 0.0 },
        bottom_right: { x: 10.0, y: 5.0 },
    };
    io.printf("Area: %f\\n", r.area());   // 50.0
    return 0;
}`,
        html: `
<h2>Structs &amp; Methods</h2>

<h3>Defining a Struct</h3>
<pre><code><span class="kw">struct</span> <span class="type">Point</span> {
    <span class="type">f64</span> x;
    <span class="type">f64</span> y;
}</code></pre>

<h3>Methods</h3>
<p>Methods are functions with a <code>self</code> parameter (borrowed):</p>
<pre><code><span class="kw">fn</span> <span class="fn-name">Point.distance</span>(<span class="borrow">\`</span><span class="type">Point</span> self, <span class="borrow">\`</span><span class="type">Point</span> other) -> <span class="type">f64</span> {
    <span class="type">f64</span> dx = other.x - self.x;
    <span class="type">f64</span> dy = other.y - self.y;
    <span class="kw">return</span> math.sqrt(dx*dx + dy*dy);
}</code></pre>

<h3>Struct Initialization</h3>
<pre><code><span class="type">Point</span> p = { x: 3.0, y: 4.0 };</code></pre>

<h3>Calling Methods</h3>
<pre><code><span class="type">f64</span> d = a.distance(<span class="borrow">\`</span>b);   <span class="comment">// a is self, b is borrowed</span></code></pre>
<p>Notice: methods borrow <code>self</code> with <code>\`</code> — the struct is not moved or copied.</p>`
    }
];

// ─── Tutorial HTML ────────────────────────────────────────────────────────────

export function getTutorialHtml(
    panel: vscode.WebviewPanel,
    initialLesson: string = 'intro'
): string {

    const nav = LESSONS.map(l => `
        <button class="nav-btn" data-id="${l.id}" onclick="showLesson('${l.id}')">
            <span class="nav-icon">${l.icon}</span>
            <span class="nav-label">${l.title}</span>
        </button>`
    ).join('');

    const sections = LESSONS.map(l => `
        <div id="lesson-${l.id}" class="lesson ${l.id === initialLesson ? 'active' : ''}">
            ${l.html}
            ${l.code ? `<button class="try-btn" onclick="tryCode('${l.id}')">▶ Try it in editor</button>` : ''}
        </div>`
    ).join('');

    // Embed all lesson codes as JSON for the webview JS
    const codesJson = JSON.stringify(
        Object.fromEntries(LESSONS.filter(l => l.code).map(l => [l.id, l.code]))
    );

    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>C-Prime Tutorial</title>
<style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body {
        font-family: 'Segoe UI', system-ui, sans-serif;
        background: #0d1117;
        color: #e6edf3;
        display: flex;
        height: 100vh;
        overflow: hidden;
    }
    /* Sidebar */
    .sidebar {
        width: 190px;
        background: #161b22;
        border-right: 1px solid #30363d;
        display: flex;
        flex-direction: column;
        padding: 16px 0;
        flex-shrink: 0;
    }
    .sidebar-header {
        padding: 0 16px 16px;
        border-bottom: 1px solid #30363d;
        margin-bottom: 8px;
    }
    .sidebar-title {
        color: #58a6ff;
        font-size: 14px;
        font-weight: 700;
        letter-spacing: 0.5px;
        text-transform: uppercase;
    }
    .sidebar-sub {
        color: #6e7681;
        font-size: 11px;
        margin-top: 2px;
    }
    .nav-btn {
        display: flex;
        align-items: center;
        gap: 10px;
        width: 100%;
        padding: 8px 16px;
        background: none;
        border: none;
        color: #8b949e;
        cursor: pointer;
        font-size: 13px;
        text-align: left;
        transition: all 0.1s;
        border-left: 3px solid transparent;
    }
    .nav-btn:hover { background: #21262d; color: #e6edf3; }
    .nav-btn.active { 
        background: #1f6feb22; 
        color: #58a6ff; 
        border-left-color: #58a6ff;
    }
    .nav-icon { font-size: 16px; width: 20px; text-align: center; }
    /* Content */
    .content {
        flex: 1;
        overflow-y: auto;
        padding: 28px 32px;
    }
    .lesson { display: none; }
    .lesson.active { display: block; }
    h2 { color: #e6edf3; font-size: 22px; margin-bottom: 12px; }
    h3 { color: #79c0ff; font-size: 15px; margin: 20px 0 8px; }
    p  { color: #8b949e; line-height: 1.7; margin-bottom: 12px; font-size: 14px; }
    code { 
        background: #1c2128; 
        padding: 2px 6px; 
        border-radius: 4px; 
        font-family: 'Cascadia Code', 'Fira Code', monospace;
        font-size: 13px;
        color: #79c0ff;
    }
    pre {
        background: #161b22;
        border: 1px solid #30363d;
        border-radius: 8px;
        padding: 16px;
        margin: 12px 0;
        overflow-x: auto;
    }
    pre code {
        background: none;
        padding: 0;
        font-size: 13px;
        line-height: 1.6;
        color: #e6edf3;
    }
    pre.shell { background: #0d1117; border-color: #21262d; }
    pre.shell code { color: #7ee787; }
    .kw    { color: #ff7b72; font-weight: 500; }
    .type  { color: #79c0ff; }
    .str   { color: #a5d6ff; }
    .borrow{ color: #ff7b72; font-weight: 700; }
    .fn-name { color: #d2a8ff; }
    .comment { color: #6e7681; font-style: italic; }
    .tagline {
        background: linear-gradient(135deg, #1f6feb22, #58a6ff11);
        border: 1px solid #1f6feb55;
        border-radius: 8px;
        padding: 12px 16px;
        color: #79c0ff;
        font-style: italic;
        margin: 12px 0 20px;
        font-size: 14px;
        text-align: center;
    }
    table { width: 100%; border-collapse: collapse; margin: 12px 0; font-size: 13px; }
    th { background: #21262d; color: #79c0ff; padding: 8px 12px; text-align: left; }
    td { padding: 7px 12px; border-bottom: 1px solid #21262d; color: #8b949e; }
    td:first-child { color: #79c0ff; font-family: monospace; }
    .rules-box {
        background: #1c2128;
        border: 1px solid #30363d;
        border-radius: 8px;
        padding: 16px;
        margin: 16px 0;
    }
    .rules-box h3 { margin-top: 0; }
    .rules-box ul { padding-left: 4px; list-style: none; }
    .rules-box li { padding: 4px 0; font-size: 13px; color: #8b949e; }
    .try-btn {
        display: inline-flex;
        align-items: center;
        gap: 8px;
        margin-top: 20px;
        padding: 8px 18px;
        background: #1f6feb;
        border: none;
        border-radius: 6px;
        color: #fff;
        font-size: 13px;
        font-weight: 600;
        cursor: pointer;
        transition: background 0.2s;
    }
    .try-btn:hover { background: #388bfd; }
</style>
</head>
<body>
<nav class="sidebar">
    <div class="sidebar-header">
        <div class="sidebar-title">C-Prime</div>
        <div class="sidebar-sub">Interactive Tutorial</div>
    </div>
    ${nav}
</nav>
<main class="content">
    ${sections}
</main>
<script>
    const codes = ${codesJson};

    function showLesson(id) {
        document.querySelectorAll('.lesson').forEach(el => el.classList.remove('active'));
        document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
        const lesson = document.getElementById('lesson-' + id);
        const btn = document.querySelector('[data-id="' + id + '"]');
        if (lesson) lesson.classList.add('active');
        if (btn) btn.classList.add('active');
    }

    function tryCode(id) {
        const code = codes[id];
        if (code) {
            const vscode = acquireVsCodeApi();
            vscode.postMessage({ command: 'openCode', code: code, lessonId: id });
        }
    }

    // Set initial active nav button
    document.querySelector('[data-id="${initialLesson}"]')?.classList.add('active');
</script>
</body>
</html>`;
}
