# GGLab layout rules

The root `AGENTS.md` retains the core control-body and macro call-site rules.
The root `.editorconfig` defines mechanical C++ formatting settings, while
`.gitattributes` defines newline exceptions. Use the conventions below for
manual review of layout choices those settings cannot fully express.

## Control flow, comments and operators

Expand control-flow bodies onto separate lines, with each brace on its own
line. Keep one executable statement per line, except the clauses of a `for`
header. Compact simple accessors, empty functions and macro definitions may
retain their established layout.

Put spaces around binary and ternary operators. Align `case` and `default`
labels with their `switch`. Indent branch statements one level. For a scoped
branch, align both braces with the label, indent the contents one level, and
keep the terminating `break;` inside the scope.

Align standalone comments with the code they describe, including inside
conditional compilation. A preprocessor directive does not add a code
indentation level. Preserve comments' contents and macro/preprocessor boundaries.

In HLSL, align `[unroll]`, `[loop]`, `[branch]` and `[flatten]` with the following
statement. An attribute adds no indentation level.

See [Control flow and HLSL attributes](examples.md#control-flow-and-hlsl-attributes).

## Wrapping and continuations

Use 100 columns as a soft target for C++ and HLSL, counting tabs as four columns.
Prioritize readability and semantic grouping. Keep a declaration, simple call
or initializer entry complete when that reads more clearly on one longer line.
Preserve existing readable wrapping; do not mechanically join everything that
fits or split everything that exceeds a threshold.

Keep types with their declared names and simple calls with their short arguments
when practical. Keep a member access with the object it identifies. In compound
conditions, prefer breaks after `&&` or `||` between complete subconditions.
Split inside a call or subcondition when its complexity or overall length makes
that clearer.

Indent a continued expression one level beyond its containing statement. Keep
continuations at the same logical nesting level aligned; a further line break
does not add another level. Nested calls and initializers can add their own
levels. Return to the containing expression's level after a nested construct.
Independent statements following a long expression return to their scope's
normal indentation.

Constructor member initializers are siblings. Wrapping one initializer must
not progressively indent later members. Preserve neighboring colon and comma
placement.

## Ternary expressions

Keep short ternaries on one line. For a multiline ternary, put `?` at the end
of the condition line and `:` at the end of the true branch. Put the branches
on separate lines, both one level beyond the condition's base indentation.

For a multiline condition, use its outer continuation indentation as the base.
Exclude deeper indentation belonging to nested call arguments. If the condition
is a single wrapped call followed by a comparison, its argument indentation does
not establish a new outer condition level. The physical line containing `?`
is not necessarily the condition's base.

Preserve nesting in conditional branches and their initializers. A ternary in a
false branch retains its own branch hierarchy. Adjacent pieces of one string
literal remain siblings.

See [Ternary expressions](examples.md#ternary-expressions).

## C++ lambdas

Keep readable capture lists on one line, even beyond the soft column target.
For a long header, prefer a break between the complete capture list and the
parameter list. Split complex capture lists when that improves readability.
For a lambda passed as a call argument, align a separately written capture line
and parameter-list line at the same indentation level.

Treat an ordinary assigned lambda as an initializer continuation. When its
capture starts on the declaration line, indent its separately written scope
braces one level beyond the declaration and its body one further level. When
the assignment breaks before the capture, align the capture and scope braces at
that continuation level; do not add another level just because the header wraps.

A lambda passed as an argument follows the call's argument nesting, with its
scope braces aligned with the capture. A control-flow body inside either lambda
follows the normal control-body rules.

Do not outdent ordinary assigned lambda braces or bodies to align with the
variable declaration.

See [Lambdas](examples.md#lambdas).

## Arrays, records and JSON

For arrays and repeated tables with uniform records, prefer one complete record
per line when its fields are straightforward values. Allow readable records to
exceed the soft column target. Expand records containing complex expressions,
nested structures or per-field comments when that improves readability.

Keep an array's type and name together. For a multiline declaration initializer,
put the outer opening brace on its own line and align the closing brace with
the declaration. Preserve intentional row grouping within the data.

For multiline JSON construction, put each named field on its own line and make
the nested object hierarchy clear. Outer closing braces may follow the last
data entry on its line. Preserve established initializer punctuation, including
`Type{}` and a comma immediately followed by `{`.

See [Arrays and JSON](examples.md#arrays-and-json).

## HLSL conversions

Write every explicit type conversion as `(type)(value)`, including scalar,
vector, matrix and aggregate conversions. Parenthesize both the type and the
complete operand, including a single variable or literal.

Preserve component construction such as `float3(x, y, z)` and
`float4(position, 1.0)`. Use `asfloat`, `asint` and `asuint` for bit
reinterpretation. Do not treat every type-named call as an interchangeable cast.

See [HLSL conversions](examples.md#hlsl-conversions).

## Literal data and layout review

Indentation inside a raw string is part of the string's value. Preserve raw
string bytes, other literal contents, and byte-pinned fixtures during formatting.
Unindented literal data can be intentional. Beautifying JSON embedded in a raw
string is a separate fixture edit, even if its parsed meaning is unchanged.

Keep a standalone stream continuation after a multiline raw string at the
expression's continuation indentation. It may stay beside the literal's closing
token when that reads clearly; change whitespace only outside the literal.

Check layout around ternaries, ordinary lambdas, nested aggregate call closers,
constructor initializers, `requires` parameters, pointer arrays and conditional
compilation. Preserve neighboring pointer/reference spacing and avoid unnecessary
changes such as `T& value` to `T & value` or spreading `new Element*[count]{}`
into unrelated declaration styles.

See [Raw strings and macros](examples.md#raw-strings-and-macros).
