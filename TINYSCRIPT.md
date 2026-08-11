# TinyScript Manual

TinyScript is the small C-like scripting language built into TinyOS (`src/script.c`). It's a tree-walking interpreter — lexer → recursive-descent parser → AST → evaluator — with 32-bit integers and strings as its only data types, and a handful of native functions that call straight into the OS (filesystem, console).

This is the complete reference. For a quick taste, see the [README](README.md#tinyscript).

## Contents

- [Quick start](#quick-start)
- [Lexical structure](#lexical-structure)
- [Types and values](#types-and-values)
- [Variables and scope](#variables-and-scope)
- [Operators](#operators)
- [Statements](#statements)
- [Functions](#functions)
- [Native functions](#native-functions)
- [Errors](#errors)
- [Limits](#limits)
- [Grammar reference](#grammar-reference)
- [Gotchas](#gotchas)

## Quick start

Write a script with `nano` (Ctrl+O to save, Ctrl+X to exit), then run it:

```
rp2040_sh$ nano hello.ts
-- (type your script, save, exit) --
rp2040_sh$ run hello.ts
```

Or for anything short enough to fit on one line, skip the editor:

```
rp2040_sh$ write hello.ts print("Hello from TinyScript!");
rp2040_sh$ run hello.ts
Hello from TinyScript!
```

## Lexical structure

**Comments** — `//` to end of line. No block comments.

**Whitespace** — spaces, tabs, `\r`, `\n` are all insignificant; statements are separated by `;`, not newlines. A whole script can legally be one line.

**Identifiers** — `[a-zA-Z_][a-zA-Z0-9_]*`, used for both variable and function names. Limited to 23 characters; anything longer is *silently truncated* to 23 chars (not an error — two long names sharing the first 23 characters will silently collide as the same variable).

**Keywords** (reserved, can't be used as identifiers): `if` `else` `while` `function` `return` `true` `false`

**Number literals** — decimal digits only. No hex/octal/binary, no float literals, no negative literals (`-5` is the unary `-` operator applied to `5`, not a single token).

**String literals** — double-quoted, up to 255 characters. Supported escapes: `\n` `\t` `\"` `\\`. Any other `\x` drops the backslash and keeps `x` literally (e.g. `"\q"` is just `q`). A string longer than 255 characters, or one missing its closing `"`, is a parse error ("unterminated string").

**Boolean literals** — `true`, `false`.

## Types and values

Four value types, dynamically typed (a variable can hold any type, and its type can change on reassignment):

| Type | Description |
|---|---|
| `int` | 32-bit signed integer. No overflow checking — arithmetic wraps. |
| `string` | Immutable, arena-allocated. |
| `bool` | `true`/`false`. |
| `nil` | The value of an undeclared `return;`, and other "nothing" cases. |

**Truthiness** (used by `if`, `while`, `!`, `&&`, `||`):

| Value | Truthy? |
|---|---|
| `nil` | false |
| `int`/`bool` | false only if `0`/`false` |
| `string` | false only if `""` (empty) — **`"0"` and `"false"` are both truthy strings** |

## Variables and scope

No declaration keyword — assigning to a name creates it:

```js
x = 5;        // creates (or updates) x
```

**Two scopes: global, and per-function-call.** There's no block scoping — an `if`/`while` body shares its enclosing scope.

- At the top level (outside any function), assignment always writes a **global**.
- Inside a function body, assignment **always** writes a **local** to that call — even if a global of the same name exists. This is a deliberate design choice: a function can never accidentally clobber global state through assignment.
- *Reading* a name inside a function checks locals first, then falls back to globals if not found. So a function can read a global it hasn't shadowed, but the moment it assigns to that name, it starts writing a local instead.
- Function parameters are locals, seeded from the call's arguments before the body runs.

Assignment is itself an expression (it evaluates to the assigned value) and is right-associative, so chained assignment works:

```js
x = y = 5;   // both x and y become 5
```

## Operators

From loosest to tightest binding:

| Precedence | Operators | Associativity |
|---|---|---|
| 1 (loosest) | `=` | right |
| 2 | `\|\|` | left |
| 3 | `&&` | left |
| 4 | `==` `!=` | left |
| 5 | `<` `<=` `>` `>=` | left |
| 6 | `+` `-` (binary) | left |
| 7 | `*` `/` `%` | left |
| 8 | unary `-` `!` | right |
| 9 (tightest) | `^` (power) | right |

Notes:

- **`+` on strings concatenates**, converting the other operand to its string form if needed (`"x = " + 5` → `"x = 5"`).
- **`==`/`!=` do string comparison whenever either side is a string** — including comparing an int/bool against a string, by converting the non-string side to its string form first. So `5 == "5"` is `true`, and `true == "true"` is `true`.
- **`==`/`!=` on two non-strings compare their internal integer representation.** `nil`, `false`, and `0` are therefore all `==` to each other (none of them are strings, so this falls through to a raw integer compare, and all three happen to be internally `0`).
- **`<` `<=` `>` `>=` only compare integers.** There is no string ordering comparison — see [Gotchas](#gotchas).
- **`^` is exponentiation**, not XOR (there's no bitwise XOR in TinyScript). It's right-associative and binds *tighter* than unary minus, matching ordinary math notation: `-2^2` is `-4`, and `2^3^2` is `2^(3^2)` = `512`. The exponent must be non-negative — `2^-1` is a runtime error.
- `/` and `%` are integer division/modulo; dividing by zero is a runtime error, not a crash.

## Statements

```
expr;                        expression statement
if (cond) { ... }
if (cond) { ... } else { ... }
while (cond) { ... }
return;                      return nil
return expr;
function name(a, b) { ... }
{ ... }                      a bare block
```

**`if`/`while` bodies must be `{ }` blocks — there is no bare single-statement form.** `if (x) y = 1;` is a parse error; it must be `if (x) { y = 1; }`.

**There is no `else if`.** `else` must be followed directly by `{`, not by another `if`. Chain conditions by nesting instead:

```js
// wrong: parse error ("expected '{'")
if (a) { ... } else if (b) { ... }

// right
if (a) {
  ...
} else {
  if (b) {
    ...
  }
}
```

**No `for` loop, no `break`, no `continue`.** A `while` loop can only end via its condition going false, a `return` from inside a function, or hitting the runaway-script guard (see [Limits](#limits)).

**Function definitions are statements, executed like any other.** A `function` statement registers (or re-registers, if the name already exists) that function the moment it *runs* — not at parse time. In practice this means: define a function before you call it (normal top-to-bottom script order handles this automatically), and a `function` statement inside an `if`/`while` only takes effect if that branch actually executes.

## Functions

```js
function add(a, b) {
  return a + b;
}
```

- Parameters become locals in the new call frame, bound from the call's arguments in order.
- Calling with fewer arguments than parameters: missing ones are `nil`. Calling with more: extras beyond the declared parameters are ignored (but see the 8-argument cap below).
- `return;` (no value) and falling off the end of a function both yield `nil`.
- **Recursion works**, bounded by a fixed call-depth limit — see [Limits](#limits). Exceeding it is a runtime error ("stack overflow"), not a crash.

```js
function fact(n) {
  if (n <= 1) { return 1; }
  return n * fact(n - 1);
}
```

## Native functions

The "exec functions" — calls straight into the OS:

| Function | Behavior |
|---|---|
| `print(a, b, ...)` | Prints each argument's string form, space-separated, with a trailing newline. Returns `nil`. |
| `write(name, content)` | Writes `content` (converted to its string form) to file `name` via `fs_write`. Returns `true`/`false`. |
| `read(name)` | Reads file `name` as a string via `fs_read`. Returns `nil` if the file doesn't exist or is too large for the internal read buffer (4KB). |
| `exists(name)` | `true`/`false`, via `fs_stat`. |
| `len(s)` | String length. Returns `0` for non-string arguments. |

Calling one of these with too few required arguments is a runtime error naming the function (e.g. `write(name, content) needs 2 arguments`).

Adding your own: one entry in the `natives[]` table in `src/script.c`, wrapping a `value_t (*)(value_t *args, int argc)` function. Nothing else needs to change — no grammar/parser work required.

## Errors

Two kinds, both reported to the console and both **abort the script without crashing the shell** — you're always dropped back to `rp2040_sh$`.

**Parse errors** — reported with a line number, e.g. `script: parse error (line 3): expected ';'`. Nothing in the script runs; the whole thing is parsed before any of it executes.

**Runtime errors** — reported as `script: runtime error: <reason>`, e.g.:

- `undefined variable` — read of a name that was never assigned
- `undefined function` — call to a name that isn't a native or a defined function
- `division by zero`
- `negative exponent not supported`
- `stack overflow` — call depth exceeded (see [Limits](#limits))
- `too many local variables` / `too many global variables` / `too many function definitions`
- `while loop exceeded iteration limit`
- `out of script memory` — the AST arena filled up (see [Limits](#limits))

A runtime error sets a flag that's checked after every statement, and also before any native/user function actually runs (even if the error happened while evaluating that call's own arguments) — so a statement that triggers an error partway through evaluating itself won't go on to do partial work with garbage values; execution stops at that point and no further statements run.

## Limits

| Limit | Value |
|---|---|
| Max identifier length (var/function names) | 23 characters (silently truncated beyond that) |
| Max string literal length | 255 characters |
| Max globals | 32 |
| Max locals per function call | 16 |
| Max function definitions | 16 |
| Max call depth (recursion) | 16 |
| Max arguments evaluated per call | 8 (extras are never evaluated, not just ignored) |
| `while` loop iteration guard | 1,000,000 iterations, then a runtime error |
| AST arena (parsed program storage) | 32KB, reset fresh before every `run` |
| Max script file size | 4KB (TinyFS's per-file limit — see the [README](README.md#filesystem-tinyfs)) |
| Max bytes `read()` can return | 4KB |

## Grammar reference

```
program     := statement*

statement   := funcdef | block | if_stmt | while_stmt | return_stmt | expr_stmt

funcdef     := "function" IDENT "(" params? ")" block
params      := IDENT ("," IDENT)*
block       := "{" statement* "}"
if_stmt     := "if" "(" expr ")" block ("else" block)?
while_stmt  := "while" "(" expr ")" block
return_stmt := "return" expr? ";"
expr_stmt   := expr ";"

expr        := assignment
assignment  := IDENT "=" assignment | logic_or
logic_or    := logic_and ("||" logic_and)*
logic_and   := equality ("&&" equality)*
equality    := comparison (("==" | "!=") comparison)*
comparison  := term ((">" | ">=" | "<" | "<=") term)*
term        := factor (("+" | "-") factor)*
factor      := unary (("*" | "/" | "%") unary)*
unary       := ("!" | "-") unary | power
power       := primary ("^" unary)?
primary     := NUMBER | STRING | "true" | "false"
             | IDENT | IDENT "(" args? ")" | "(" expr ")"
args        := expr ("," expr)*
```

## Gotchas

Things that don't error, but might not do what you expect:

- **`else if` doesn't parse.** See [Statements](#statements) for the nesting workaround.
- **`<` `<=` `>` `>=` don't compare strings.** They always compare an internal numeric field that's unused (effectively `0`) for string operands, regardless of the strings' actual content or length. Only `==`/`!=` do real string comparison. Sorting or ordering strings isn't currently possible in TinyScript.
- **`"0"` and `"false"` are truthy.** String truthiness is purely "empty or not" — content doesn't matter.
- **`nil == false == 0` are all mutually equal** (see [Operators](#operators)) — but `nil == "0"` is `false` (string comparison kicks in and `"nil"` ≠ `"0"`).
- **A function's assignment never touches a same-named global**, even if you meant to update it — it always creates/updates a local instead. There's no way to write to a global from inside a function.
- **Identifiers over 23 characters silently collide** rather than erroring — no truncation warning.
- **Extra call arguments beyond 8 are never evaluated**, not just dropped — if one has a side effect (e.g. a 9th argument that's itself a function call), that call never happens.
