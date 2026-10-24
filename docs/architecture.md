# Astra Studio — Architecture

## Compilation pipeline

Astra is a register-free, stack-oriented bytecode language. The front end converts raw source into tokens with line metadata, parses the tokens into an abstract syntax tree, then recursively compiles AST nodes into instruction streams.

- **Lexer:** whitespace/comments, identifiers, keywords, numeric literals, escaped string literals, punctuation and operators.
- **Parser:** recursive-descent grammar with ascending-precedence functions. Assignment is right-associative. `&&` and `||` produce short-circuit branches.
- **Compiler:** collects function signatures before compilation, emits one main function plus user functions; maintains local variable scopes and indices. Jump targets are patched once branches are generated.
- **VM:** operands on a LIFO value stack, globals in a hashmap, local variables in per-call frames. Every function has its own constants, instructions, arity and local slot count.

## Instruction set

`CONSTANT`, `NIL`, `POP`, `GET_GLOBAL`, `DEF_GLOBAL`, `SET_GLOBAL`, `GET_LOCAL`, `SET_LOCAL`, `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `NEGATE`, `NOT`, `EQ`, `NEQ`, `LT`, `LTE`, `GT`, `GTE`, `JUMP`, `JUMP_IF_FALSE`, `CALL`, `RETURN`.

Instructions store an opcode, up to two signed integer operands and a source line. Jump operands contain **absolute instruction indices** within a function's code vector, not byte offsets.

## Call frames and locals

A call pops arguments from the operand stack and pushes a frame with its own locals (parameter slots first), instruction pointer and saved caller stack height. `RETURN` pops the result, trims temporary stack entries, removes the frame and places the result on the caller's operand stack. There is a depth cap of 256 frames.

## Error diagnostics

Syntax and compilation errors carry lexer/parser source line numbers; runtime checks report the executing bytecode instruction's source line. Error responses also include partially emitted `stdout` if execution fails after printing.

## Why browser UI?

The local-web frontend provides a cohesive custom IDE without requiring Qt/Electron dependencies to build and avoids shipping a large runtime. The backend is 100% C++20. The UI is ordinary HTML/CSS/JavaScript and runs against the loopback-only server.

## Design tradeoffs

- Bytecode uses typed C++ `std::variant` values rather than NaN-boxing: easy to audit, slower than production VMs.
- Local scope entries are determined at compile time and allocated per frame rather than dynamically creating hash maps for each statement.
- Globals can be declared in top-level control-flow blocks and are runtime-resolved; local variables are lexically resolved in the compiler.
- Standard-library functions are deliberately few and sandbox-neutral.
- Threading and optimization passes are future work. Simple sequential execution helps expose compiler/VM correctness.
