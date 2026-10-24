#include "astra/engine.hpp"
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(const std::string& name, const std::string& program, const std::string& expected) {
    const auto result = astra::execute(program);
    if (!result.ok || result.output != expected) {
        ++failures;
        std::cerr << "FAIL " << name << ": " << result.error << " line " << result.errorLine
                  << " output=" << result.output << " expected=" << expected << '\n';
    }
}
void fail(const std::string& name, const std::string& program, const std::string& contains) {
    const auto result = astra::execute(program);
    if (result.ok || result.error.find(contains) == std::string::npos) {
        ++failures;
        std::cerr << "FAIL " << name << ": got " << (result.ok ? "success" : result.error)
                  << ", expected error with '" << contains << "'\n";
    }
}
}  // namespace

int main() {
    check("arithmetic", "print(1 + 2 * 3 - 4 / 2); print(7 % 3);", "5\n1\n");
    check("precedence", "print((1 + 2) * 3); print(3 <= 4); print(4 != 5);", "9\ntrue\ntrue\n");
    check("literals", "print(\"hello\\nworld\"); print(true); print(false); print(null);", "hello\nworld\ntrue\nfalse\nnull\n");
    check("strings", "print(\"Hi, \" + 12); print(len(\"hello\")); print(str(false));", "Hi, 12\n5\nfalse\n");
    check("loop", "let x=0; let sum=0; while(x<5){ sum=sum+x; x=x+1; } print(sum);", "10\n");
    check("conditions", "if(1<2){ print(\"yes\"); }else{print(\"no\");} if(false) print(\"bad\");", "yes\n");
    check("short circuit", "print(false && (1/0)); print(true || (1/0));", "false\ntrue\n");
    check("functions", "fn square(n){ return n*n; } print(square(7));", "49\n");
    check("forward declarations", "print(go(3)); fn go(n){return n+1;}", "4\n");
    check("recursion", "fn fib(n){if(n<=1)return n;return fib(n-1)+fib(n-2);} print(fib(10));", "55\n");
    check("scope", "fn f(a){let b=2; {let b=7;print(b);}return a+b;}print(f(3));", "7\n5\n");
    check("builtins", "print(sqrt(81)); assert(2*2==4);", "9\n");
    check("assignment expr", "let a=1; print(a=4); print(a);", "4\n4\n");
    check("empties", "print(\"\" || \"fallback\"); print(0 && 4);", "fallback\n0\n");
    check("unicode bytes", "print(\"გამარჯობა\");", "გამარჯობა\n");
    fail("divide zero", "print(2 / 0);", "Division by zero");
    fail("bad assignment", "print(x=3);", "undefined variable");
    fail("undefined", "print(missing);", "Undefined variable");
    fail("arity", "fn f(a) { return a; } f();", "expects 1 argument");
    fail("parse", "let a = ;", "Expected an expression");
    fail("missing semicolon", "print(5)", "Expected ';'");
    fail("nested function", "fn f(){ fn g(){} }", "Nested function");
    fail("duplicate function", "fn f(){} fn f(){}", "Duplicate");
    fail("duplicate parameter", "fn f(x,x){}", "Duplicate parameter");
    fail("duplicate local", "fn f(){let x=1;let x=2;}", "already declared");
    fail("invalid return", "return 1;", "outside a function");
    fail("assertion", "assert(false);", "Assertion failed");
    fail("infinite loop", "while(true) {}", "Execution limit exceeded");
    fail("deep recursion", "fn loop(n){ return loop(n+1); }loop(0);", "Call stack exceeded");
    fail("negative sqrt", "print(sqrt(-1));", "non-negative");
    fail("type error", "print(\"s\"-3);", "two numbers");
    const auto analysis = astra::execute("print(3);", false);
    if (!analysis.ok || analysis.output.size() != 0 || analysis.disassembly.find("CALL") == std::string::npos || analysis.instructions == 0) {
        ++failures; std::cerr << "FAIL bytecode analysis\n";
    }
    std::cout << "Astra engine tests: " << (failures == 0 ? "PASS" : "FAIL") << "\n";
    return failures == 0 ? 0 : 1;
}
