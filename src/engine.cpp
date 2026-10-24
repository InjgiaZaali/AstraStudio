#include "astra/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace astra {
namespace {

struct Error : std::runtime_error {
    int line;
    Error(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

enum class TK {
    End, Identifier, Number, String, Let, Fn, If, Else, While, Return, True, False, Null,
    LParen, RParen, LBrace, RBrace, Comma, Semicolon, Plus, Minus, Star, Slash, Percent,
    Bang, Equal, BangEqual, EqualEqual, Less, LessEqual, Greater, GreaterEqual, AndAnd, OrOr
};

struct Token { TK type; std::string text; int line; };

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!atEnd()) {
            skipWhitespace();
            if (atEnd()) break;
            const int line = line_;
            const char c = advance();
            switch (c) {
                case '(': tokens.push_back({TK::LParen, "(", line}); break;
                case ')': tokens.push_back({TK::RParen, ")", line}); break;
                case '{': tokens.push_back({TK::LBrace, "{", line}); break;
                case '}': tokens.push_back({TK::RBrace, "}", line}); break;
                case ',': tokens.push_back({TK::Comma, ",", line}); break;
                case ';': tokens.push_back({TK::Semicolon, ";", line}); break;
                case '+': tokens.push_back({TK::Plus, "+", line}); break;
                case '-': tokens.push_back({TK::Minus, "-", line}); break;
                case '*': tokens.push_back({TK::Star, "*", line}); break;
                case '%': tokens.push_back({TK::Percent, "%", line}); break;
                case '/': tokens.push_back({TK::Slash, "/", line}); break;
                case '!': tokens.push_back({match('=') ? TK::BangEqual : TK::Bang, "!", line}); break;
                case '=': tokens.push_back({match('=') ? TK::EqualEqual : TK::Equal, "=", line}); break;
                case '<': tokens.push_back({match('=') ? TK::LessEqual : TK::Less, "<", line}); break;
                case '>': tokens.push_back({match('=') ? TK::GreaterEqual : TK::Greater, ">", line}); break;
                case '&':
                    if (!match('&')) throw Error(line, "Expected '&' to form '&&'");
                    tokens.push_back({TK::AndAnd, "&&", line}); break;
                case '|':
                    if (!match('|')) throw Error(line, "Expected '|' to form '||'");
                    tokens.push_back({TK::OrOr, "||", line}); break;
                case '"': {
                    std::string str;
                    bool closed = false;
                    while (!atEnd()) {
                        char ch = advance();
                        if (ch == '"') { closed = true; break; }
                        if (ch == '\n') ++line_;
                        if (ch == '\\') {
                            if (atEnd()) break;
                            ch = advance();
                            switch (ch) {
                                case 'n': ch = '\n'; break;
                                case 't': ch = '\t'; break;
                                case 'r': ch = '\r'; break;
                                case '"': ch = '"'; break;
                                case '\\': ch = '\\'; break;
                                default: throw Error(line_, "Unsupported escape sequence");
                            }
                        }
                        str += ch;
                    }
                    if (!closed) throw Error(line, "Unterminated string literal");
                    tokens.push_back({TK::String, std::move(str), line});
                    break;
                }
                default:
                    if (isDigit(c)) {
                        std::string n(1, c);
                        while (isDigit(peek())) n += advance();
                        if (peek() == '.' && isDigit(peekNext())) {
                            n += advance();
                            while (isDigit(peek())) n += advance();
                        }
                        tokens.push_back({TK::Number, std::move(n), line});
                    } else if (isAlpha(c)) {
                        std::string id(1, c);
                        while (isAlpha(peek()) || isDigit(peek())) id += advance();
                        static const std::unordered_map<std::string, TK> keywords = {
                            {"let", TK::Let}, {"fn", TK::Fn}, {"if", TK::If},
                            {"else", TK::Else}, {"while", TK::While}, {"return", TK::Return},
                            {"true", TK::True}, {"false", TK::False}, {"null", TK::Null}
                        };
                        const auto it = keywords.find(id);
                        tokens.push_back({it == keywords.end() ? TK::Identifier : it->second,
                                          std::move(id), line});
                    } else {
                        throw Error(line, "Unexpected character '" + std::string(1, c) + "'");
                    }
            }
        }
        tokens.push_back({TK::End, "", line_});
        return tokens;
    }

private:
    std::string_view source_;
    std::size_t position_ = 0;
    int line_ = 1;
    bool atEnd() const { return position_ >= source_.size(); }
    char peek() const { return atEnd() ? '\0' : source_[position_]; }
    char peekNext() const { return position_ + 1 >= source_.size() ? '\0' : source_[position_ + 1]; }
    char advance() { return source_[position_++]; }
    bool match(char c) { if (peek() != c) return false; ++position_; return true; }
    static bool isDigit(char c) { return c >= '0' && c <= '9'; }
    static bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    void skipWhitespace() {
        while (!atEnd()) {
            if (peek() == '/' && peekNext() == '/') {
                while (!atEnd() && peek() != '\n') advance();
            } else if (peek() == ' ' || peek() == '\t' || peek() == '\r') {
                advance();
            } else if (peek() == '\n') {
                advance(); ++line_;
            } else break;
        }
    }
};

using Value = std::variant<std::monostate, double, bool, std::string>;

std::string valueString(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "null";
    if (const auto p = std::get_if<double>(&value)) {
        std::ostringstream stream;
        stream << std::setprecision(12) << *p;
        return stream.str();
    }
    if (const auto p = std::get_if<bool>(&value)) return *p ? "true" : "false";
    return std::get<std::string>(value);
}

bool truthy(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return false;
    if (auto p = std::get_if<bool>(&value)) return *p;
    if (auto p = std::get_if<double>(&value)) return *p != 0.0;
    return !std::get<std::string>(value).empty();
}

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Expr {
    enum class Kind { Literal, Variable, Assign, Binary, Unary, Call } kind;
    Token token;
    Value literal;
    ExprPtr left;
    ExprPtr right;
    std::vector<ExprPtr> args;
    explicit Expr(Kind kind, Token token) : kind(kind), token(std::move(token)) {}
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct Stmt {
    enum class Kind { Expression, Let, Block, If, While, Return, Function } kind;
    Token token;
    ExprPtr expr;
    std::vector<StmtPtr> body;
    StmtPtr thenBranch;
    StmtPtr elseBranch;
    std::vector<std::string> params;
    explicit Stmt(Kind kind, Token token) : kind(kind), token(std::move(token)) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    std::vector<StmtPtr> parse() {
        std::vector<StmtPtr> result;
        while (!check(TK::End)) result.push_back(declaration());
        return result;
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    const Token& peek() const { return tokens_.at(current_); }
    const Token& previous() const { return tokens_.at(current_ - 1); }
    bool check(TK kind) const { return peek().type == kind; }
    bool match(TK kind) { if (!check(kind)) return false; ++current_; return true; }
    Token consume(TK kind, const std::string& message) {
        if (!check(kind)) throw Error(peek().line, message);
        return tokens_.at(current_++);
    }
    StmtPtr declaration() {
        if (match(TK::Fn)) return functionDeclaration(previous());
        if (match(TK::Let)) return letDeclaration(previous());
        return statement();
    }
    StmtPtr functionDeclaration(const Token& keyword) {
        auto stmt = std::make_unique<Stmt>(Stmt::Kind::Function, consume(TK::Identifier, "Expected function name"));
        consume(TK::LParen, "Expected '(' after function name");
        if (!check(TK::RParen)) {
            do {
                if (stmt->params.size() >= 64) throw Error(keyword.line, "Too many parameters (max 64)");
                stmt->params.push_back(consume(TK::Identifier, "Expected parameter name").text);
            } while (match(TK::Comma));
        }
        consume(TK::RParen, "Expected ')' after parameters");
        consume(TK::LBrace, "Expected '{' before function body");
        stmt->thenBranch = block(previous());
        return stmt;
    }
    StmtPtr letDeclaration(const Token& keyword) {
        auto stmt = std::make_unique<Stmt>(Stmt::Kind::Let,
                         consume(TK::Identifier, "Expected variable name after 'let'"));
        if (match(TK::Equal)) stmt->expr = expression();
        consume(TK::Semicolon, "Expected ';' after variable declaration");
        (void)keyword;
        return stmt;
    }
    StmtPtr block(const Token& token) {
        auto stmt = std::make_unique<Stmt>(Stmt::Kind::Block, token);
        while (!check(TK::RBrace) && !check(TK::End)) stmt->body.push_back(declaration());
        consume(TK::RBrace, "Expected '}' to close block");
        return stmt;
    }
    StmtPtr statement() {
        if (match(TK::LBrace)) return block(previous());
        if (match(TK::If)) {
            const Token keyword = previous();
            consume(TK::LParen, "Expected '(' after 'if'");
            auto stmt = std::make_unique<Stmt>(Stmt::Kind::If, keyword);
            stmt->expr = expression();
            consume(TK::RParen, "Expected ')' after condition");
            stmt->thenBranch = statement();
            if (match(TK::Else)) stmt->elseBranch = statement();
            return stmt;
        }
        if (match(TK::While)) {
            const Token keyword = previous();
            consume(TK::LParen, "Expected '(' after 'while'");
            auto stmt = std::make_unique<Stmt>(Stmt::Kind::While, keyword);
            stmt->expr = expression();
            consume(TK::RParen, "Expected ')' after condition");
            stmt->thenBranch = statement();
            return stmt;
        }
        if (match(TK::Return)) {
            auto stmt = std::make_unique<Stmt>(Stmt::Kind::Return, previous());
            if (!check(TK::Semicolon)) stmt->expr = expression();
            consume(TK::Semicolon, "Expected ';' after return");
            return stmt;
        }
        auto stmt = std::make_unique<Stmt>(Stmt::Kind::Expression, peek());
        stmt->expr = expression();
        consume(TK::Semicolon, "Expected ';' after expression");
        return stmt;
    }
    ExprPtr expression() { return assignment(); }
    ExprPtr assignment() {
        auto expr = logicOr();
        if (match(TK::Equal)) {
            const Token equals = previous();
            auto right = assignment();
            if (expr->kind != Expr::Kind::Variable) throw Error(equals.line, "Invalid assignment target");
            auto result = std::make_unique<Expr>(Expr::Kind::Assign, expr->token);
            result->right = std::move(right);
            return result;
        }
        return expr;
    }
    ExprPtr logicOr() { auto e = logicAnd(); while (match(TK::OrOr)) { auto op = previous(); e = binary(std::move(e), op, logicAnd()); } return e; }
    ExprPtr logicAnd() { auto e = equality(); while (match(TK::AndAnd)) { auto op = previous(); e = binary(std::move(e), op, equality()); } return e; }
    ExprPtr equality() {
        auto e = comparison();
        while (match(TK::EqualEqual) || match(TK::BangEqual)) { auto op = previous(); e = binary(std::move(e), op, comparison()); }
        return e;
    }
    ExprPtr comparison() {
        auto e = term();
        while (match(TK::Less) || match(TK::LessEqual) || match(TK::Greater) || match(TK::GreaterEqual)) {
            auto op = previous(); e = binary(std::move(e), op, term());
        }
        return e;
    }
    ExprPtr term() {
        auto e = factor();
        while (match(TK::Plus) || match(TK::Minus)) { auto op = previous(); e = binary(std::move(e), op, factor()); }
        return e;
    }
    ExprPtr factor() {
        auto e = unary();
        while (match(TK::Star) || match(TK::Slash) || match(TK::Percent)) {
            auto op = previous(); e = binary(std::move(e), op, unary());
        }
        return e;
    }
    ExprPtr unary() {
        if (match(TK::Bang) || match(TK::Minus)) {
            auto expr = std::make_unique<Expr>(Expr::Kind::Unary, previous());
            expr->right = unary();
            return expr;
        }
        return call();
    }
    ExprPtr call() {
        auto e = primary();
        while (match(TK::LParen)) {
            if (e->kind != Expr::Kind::Variable) throw Error(previous().line, "Only named functions may be called");
            auto result = std::make_unique<Expr>(Expr::Kind::Call, e->token);
            if (!check(TK::RParen)) {
                do {
                    if (result->args.size() >= 64) throw Error(previous().line, "Too many arguments (max 64)");
                    result->args.push_back(expression());
                } while (match(TK::Comma));
            }
            consume(TK::RParen, "Expected ')' after arguments");
            e = std::move(result);
        }
        return e;
    }
    ExprPtr primary() {
        const Token t = peek();
        if (match(TK::Number)) {
            auto e = std::make_unique<Expr>(Expr::Kind::Literal, t);
            try { e->literal = std::stod(t.text); }
            catch (...) { throw Error(t.line, "Invalid numeric literal"); }
            return e;
        }
        if (match(TK::String)) {
            auto e = std::make_unique<Expr>(Expr::Kind::Literal, t); e->literal = t.text; return e;
        }
        if (match(TK::True) || match(TK::False)) {
            auto e = std::make_unique<Expr>(Expr::Kind::Literal, t);
            e->literal = t.type == TK::True; return e;
        }
        if (match(TK::Null)) return std::make_unique<Expr>(Expr::Kind::Literal, t);
        if (match(TK::Identifier)) return std::make_unique<Expr>(Expr::Kind::Variable, t);
        if (match(TK::LParen)) {
            auto e = expression(); consume(TK::RParen, "Expected ')' after expression"); return e;
        }
        throw Error(t.line, "Expected an expression");
    }
    static ExprPtr binary(ExprPtr left, const Token& op, ExprPtr right) {
        auto e = std::make_unique<Expr>(Expr::Kind::Binary, op);
        e->left = std::move(left); e->right = std::move(right); return e;
    }
};

enum class Op {
    Constant, Nil, Pop, GetGlobal, DefineGlobal, SetGlobal, GetLocal, SetLocal,
    Add, Subtract, Multiply, Divide, Modulo, Negate, Not, Equal, NotEqual,
    Less, LessEqual, Greater, GreaterEqual, Jump, JumpIfFalse, Call, Return
};

struct Instruction { Op op; int a = 0; int b = 0; int line = 0; };
struct Function {
    std::string name;
    int arity = 0;
    int locals = 0;
    std::vector<Instruction> code;
    std::vector<Value> constants;
};
struct Program {
    std::vector<Function> functions;
    std::unordered_map<std::string, int> functionIds;
};

const char* opName(Op op) {
    switch (op) {
        case Op::Constant: return "CONSTANT";
        case Op::Nil: return "NIL";
        case Op::Pop: return "POP";
        case Op::GetGlobal: return "GET_GLOBAL";
        case Op::DefineGlobal: return "DEF_GLOBAL";
        case Op::SetGlobal: return "SET_GLOBAL";
        case Op::GetLocal: return "GET_LOCAL";
        case Op::SetLocal: return "SET_LOCAL";
        case Op::Add: return "ADD";
        case Op::Subtract: return "SUB";
        case Op::Multiply: return "MUL";
        case Op::Divide: return "DIV";
        case Op::Modulo: return "MOD";
        case Op::Negate: return "NEGATE";
        case Op::Not: return "NOT";
        case Op::Equal: return "EQ";
        case Op::NotEqual: return "NEQ";
        case Op::Less: return "LT";
        case Op::LessEqual: return "LTE";
        case Op::Greater: return "GT";
        case Op::GreaterEqual: return "GTE";
        case Op::Jump: return "JUMP";
        case Op::JumpIfFalse: return "JUMP_IF_FALSE";
        case Op::Call: return "CALL";
        case Op::Return: return "RETURN";
    }
    return "UNKNOWN";
}

struct Context {
    Function* function;
    bool topLevel;
    int nextSlot = 0;
    std::vector<std::map<std::string, int>> scopes;
};

class Compiler {
public:
    Program compile(const std::vector<StmtPtr>& stmts) {
        Program program;
        program.functions.emplace_back();
        program.functions[0].name = "<main>";
        for (auto& stmt : stmts) {
            if (stmt->kind != Stmt::Kind::Function) continue;
            if (program.functionIds.contains(stmt->token.text) || isBuiltin(stmt->token.text))
                throw Error(stmt->token.line, "Duplicate or reserved function name '" + stmt->token.text + "'");
            if (stmt->thenBranch->kind != Stmt::Kind::Block) throw Error(stmt->token.line, "Function must have a block");
            const int id = static_cast<int>(program.functions.size());
            program.functionIds[stmt->token.text] = id;
            program.functions.emplace_back();
            program.functions.back().name = stmt->token.text;
            program.functions.back().arity = static_cast<int>(stmt->params.size());
        }
        Context main{&program.functions[0], true, 0, {}};
        for (auto& stmt : stmts) if (stmt->kind != Stmt::Kind::Function) statement(*stmt, main, program);
        emit(main, Op::Nil, 0);
        emit(main, Op::Return, 0);
        for (auto& stmt : stmts) {
            if (stmt->kind != Stmt::Kind::Function) continue;
            auto& fn = program.functions.at(program.functionIds.at(stmt->token.text));
            Context ctx{&fn, false, 0, {}};
            ctx.scopes.emplace_back();
            for (auto& param : stmt->params) {
                if (ctx.scopes.back().contains(param)) throw Error(stmt->token.line, "Duplicate parameter '" + param + "'");
                ctx.scopes.back()[param] = ctx.nextSlot++;
            }
            statement(*stmt->thenBranch, ctx, program);
            emit(ctx, Op::Nil, stmt->token.line);
            emit(ctx, Op::Return, stmt->token.line);
            ctx.function->locals = ctx.nextSlot;
        }
        return program;
    }

private:
    static bool isBuiltin(const std::string& name) {
        return name == "print" || name == "len" || name == "str" || name == "sqrt" || name == "assert";
    }
    static int builtinId(const std::string& name) {
        if (name == "print") return -1;
        if (name == "len") return -2;
        if (name == "str") return -3;
        if (name == "sqrt") return -4;
        if (name == "assert") return -5;
        return 0;
    }
    static int emit(Context& ctx, Op op, int line, int a = 0, int b = 0) {
        ctx.function->code.push_back({op, a, b, line});
        return static_cast<int>(ctx.function->code.size()) - 1;
    }
    static void patch(Context& ctx, int index) {
        ctx.function->code.at(static_cast<std::size_t>(index)).a = static_cast<int>(ctx.function->code.size());
    }
    static int constant(Context& ctx, Value value) {
        ctx.function->constants.push_back(std::move(value));
        return static_cast<int>(ctx.function->constants.size()) - 1;
    }
    static int local(const Context& ctx, const std::string& name) {
        for (auto it = ctx.scopes.rbegin(); it != ctx.scopes.rend(); ++it)
            if (const auto var = it->find(name); var != it->end()) return var->second;
        return -1;
    }
    void expression(const Expr& expr, Context& ctx, const Program& program) {
        const int line = expr.token.line;
        switch (expr.kind) {
            case Expr::Kind::Literal:
                if (std::holds_alternative<std::monostate>(expr.literal)) emit(ctx, Op::Nil, line);
                else emit(ctx, Op::Constant, line, constant(ctx, expr.literal));
                return;
            case Expr::Kind::Variable: {
                const int slot = local(ctx, expr.token.text);
                if (slot >= 0) emit(ctx, Op::GetLocal, line, slot);
                else emit(ctx, Op::GetGlobal, line, constant(ctx, expr.token.text));
                return;
            }
            case Expr::Kind::Assign: {
                expression(*expr.right, ctx, program);
                const int slot = local(ctx, expr.token.text);
                if (slot >= 0) emit(ctx, Op::SetLocal, line, slot);
                else emit(ctx, Op::SetGlobal, line, constant(ctx, expr.token.text));
                return;
            }
            case Expr::Kind::Unary:
                expression(*expr.right, ctx, program);
                emit(ctx, expr.token.type == TK::Minus ? Op::Negate : Op::Not, line);
                return;
            case Expr::Kind::Binary:
                if (expr.token.type == TK::AndAnd) {
                    expression(*expr.left, ctx, program);
                    const int jump = emit(ctx, Op::JumpIfFalse, line);
                    emit(ctx, Op::Pop, line);
                    expression(*expr.right, ctx, program);
                    patch(ctx, jump);
                    return;
                }
                if (expr.token.type == TK::OrOr) {
                    expression(*expr.left, ctx, program);
                    const int otherwise = emit(ctx, Op::JumpIfFalse, line);
                    const int done = emit(ctx, Op::Jump, line);
                    patch(ctx, otherwise);
                    emit(ctx, Op::Pop, line);
                    expression(*expr.right, ctx, program);
                    patch(ctx, done);
                    return;
                }
                expression(*expr.left, ctx, program);
                expression(*expr.right, ctx, program);
                switch (expr.token.type) {
                    case TK::Plus: emit(ctx, Op::Add, line); break;
                    case TK::Minus: emit(ctx, Op::Subtract, line); break;
                    case TK::Star: emit(ctx, Op::Multiply, line); break;
                    case TK::Slash: emit(ctx, Op::Divide, line); break;
                    case TK::Percent: emit(ctx, Op::Modulo, line); break;
                    case TK::EqualEqual: emit(ctx, Op::Equal, line); break;
                    case TK::BangEqual: emit(ctx, Op::NotEqual, line); break;
                    case TK::Less: emit(ctx, Op::Less, line); break;
                    case TK::LessEqual: emit(ctx, Op::LessEqual, line); break;
                    case TK::Greater: emit(ctx, Op::Greater, line); break;
                    case TK::GreaterEqual: emit(ctx, Op::GreaterEqual, line); break;
                    default: throw Error(line, "Invalid binary operator");
                }
                return;
            case Expr::Kind::Call: {
                int id = builtinId(expr.token.text);
                int arity = 1;
                if (id == 0) {
                    const auto it = program.functionIds.find(expr.token.text);
                    if (it == program.functionIds.end()) throw Error(line, "Unknown function '" + expr.token.text + "'");
                    id = it->second;
                    arity = program.functions.at(static_cast<std::size_t>(id)).arity;
                }
                if (static_cast<int>(expr.args.size()) != arity)
                    throw Error(line, "Function '" + expr.token.text + "' expects " + std::to_string(arity) + " argument(s)");
                for (auto& arg : expr.args) expression(*arg, ctx, program);
                emit(ctx, Op::Call, line, id, static_cast<int>(expr.args.size()));
                return;
            }
        }
    }
    void statement(const Stmt& stmt, Context& ctx, const Program& program) {
        const int line = stmt.token.line;
        switch (stmt.kind) {
            case Stmt::Kind::Expression:
                expression(*stmt.expr, ctx, program);
                emit(ctx, Op::Pop, line);
                return;
            case Stmt::Kind::Let:
                if (stmt.expr) expression(*stmt.expr, ctx, program); else emit(ctx, Op::Nil, line);
                if (ctx.topLevel) {
                    emit(ctx, Op::DefineGlobal, line, constant(ctx, stmt.token.text));
                } else {
                    auto& scope = ctx.scopes.back();
                    if (scope.contains(stmt.token.text)) throw Error(line, "Variable already declared in this scope: " + stmt.token.text);
                    const int slot = ctx.nextSlot++;
                    scope[stmt.token.text] = slot;
                    emit(ctx, Op::SetLocal, line, slot);
                    emit(ctx, Op::Pop, line);
                }
                return;
            case Stmt::Kind::Block:
                if (!ctx.topLevel) ctx.scopes.emplace_back();
                for (auto& s : stmt.body) statement(*s, ctx, program);
                if (!ctx.topLevel) ctx.scopes.pop_back();
                return;
            case Stmt::Kind::If: {
                expression(*stmt.expr, ctx, program);
                const int elseJump = emit(ctx, Op::JumpIfFalse, line);
                emit(ctx, Op::Pop, line);
                statement(*stmt.thenBranch, ctx, program);
                const int endJump = emit(ctx, Op::Jump, line);
                patch(ctx, elseJump);
                emit(ctx, Op::Pop, line);
                if (stmt.elseBranch) statement(*stmt.elseBranch, ctx, program);
                patch(ctx, endJump);
                return;
            }
            case Stmt::Kind::While: {
                const int start = static_cast<int>(ctx.function->code.size());
                expression(*stmt.expr, ctx, program);
                const int endJump = emit(ctx, Op::JumpIfFalse, line);
                emit(ctx, Op::Pop, line);
                statement(*stmt.thenBranch, ctx, program);
                emit(ctx, Op::Jump, line, start);
                patch(ctx, endJump);
                emit(ctx, Op::Pop, line);
                return;
            }
            case Stmt::Kind::Return:
                if (ctx.topLevel) throw Error(line, "'return' cannot be used outside a function");
                if (stmt.expr) expression(*stmt.expr, ctx, program); else emit(ctx, Op::Nil, line);
                emit(ctx, Op::Return, line);
                return;
            case Stmt::Kind::Function:
                throw Error(line, "Nested function declarations are not supported");
        }
    }
};

std::string disassemble(const Program& program) {
    std::ostringstream out;
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        const auto& function = program.functions[index];
        out << "\n╔══ FUNCTION " << function.name << " (arity " << function.arity << ", slots " << function.locals << ")\n";
        for (std::size_t i = 0; i < function.code.size(); ++i) {
            const auto& inst = function.code[i];
            out << std::setfill('0') << std::setw(4) << i << std::setfill(' ') << "  "
                << std::setw(3) << inst.line << "  " << std::left << std::setw(17) << opName(inst.op) << std::right;
            switch (inst.op) {
                case Op::Constant: out << " #" << inst.a << " (" << valueString(function.constants.at(inst.a)) << ")"; break;
                case Op::GetGlobal: case Op::SetGlobal: case Op::DefineGlobal:
                    out << " " << std::get<std::string>(function.constants.at(inst.a)); break;
                case Op::GetLocal: case Op::SetLocal: case Op::Jump: case Op::JumpIfFalse: out << " " << inst.a; break;
                case Op::Call: out << " fn=" << inst.a << " args=" << inst.b; break;
                default: break;
            }
            out << '\n';
        }
    }
    return out.str();
}

class VM {
public:
    explicit VM(const Program& program) : program_(program) {}
    void run() {
        pushFrame(0, {}, 0, 0);
        while (!frames_.empty()) {
            if (++steps_ > 1'000'000) throw Error(currentLine(), "Execution limit exceeded (1,000,000 instructions)");
            Frame& frame = frames_.back();
            const auto& function = program_.functions.at(frame.id);
            if (frame.ip >= function.code.size()) throw Error(currentLine(), "Instruction pointer out of range");
            const Instruction inst = function.code[frame.ip++];
            switch (inst.op) {
                case Op::Constant: stack_.push_back(function.constants.at(inst.a)); break;
                case Op::Nil: stack_.emplace_back(std::monostate{}); break;
                case Op::Pop: pop(inst.line); break;
                case Op::GetGlobal: {
                    const auto& name = std::get<std::string>(function.constants.at(inst.a));
                    const auto found = globals_.find(name);
                    if (found == globals_.end()) throw Error(inst.line, "Undefined variable '" + name + "'");
                    stack_.push_back(found->second);
                    break;
                }
                case Op::DefineGlobal: {
                    const auto& name = std::get<std::string>(function.constants.at(inst.a));
                    globals_[name] = pop(inst.line);
                    break;
                }
                case Op::SetGlobal: {
                    const auto& name = std::get<std::string>(function.constants.at(inst.a));
                    auto found = globals_.find(name);
                    if (found == globals_.end()) throw Error(inst.line, "Assignment to undefined variable '" + name + "'");
                    found->second = peek(inst.line);
                    break;
                }
                case Op::GetLocal: stack_.push_back(frame.locals.at(inst.a)); break;
                case Op::SetLocal: frame.locals.at(inst.a) = peek(inst.line); break;
                case Op::Add: {
                    auto b = pop(inst.line); auto a = pop(inst.line);
                    if (auto x = std::get_if<double>(&a); x && std::holds_alternative<double>(b))
                        stack_.emplace_back(*x + std::get<double>(b));
                    else if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b)) {
                        const std::string left = valueString(a);
                        const std::string right = valueString(b);
                        if (left.size() > 1'048'576 || right.size() > 1'048'576 - left.size())
                            throw Error(inst.line, "String value exceeds 1 MiB limit");
                        stack_.emplace_back(left + right);
                    }
                    else throw Error(inst.line, "'+' requires numbers or a string");
                    break;
                }
                case Op::Subtract: case Op::Multiply: case Op::Divide: case Op::Modulo:
                case Op::Less: case Op::LessEqual: case Op::Greater: case Op::GreaterEqual: {
                    auto b = pop(inst.line); auto a = pop(inst.line);
                    if (!std::holds_alternative<double>(a) || !std::holds_alternative<double>(b))
                        throw Error(inst.line, "Arithmetic and ordering require two numbers");
                    const double x = std::get<double>(a), y = std::get<double>(b);
                    switch (inst.op) {
                        case Op::Subtract: stack_.emplace_back(x - y); break;
                        case Op::Multiply: stack_.emplace_back(x * y); break;
                        case Op::Divide:
                            if (y == 0) throw Error(inst.line, "Division by zero");
                            stack_.emplace_back(x / y); break;
                        case Op::Modulo:
                            if (y == 0) throw Error(inst.line, "Modulo by zero");
                            stack_.emplace_back(std::fmod(x, y)); break;
                        case Op::Less: stack_.emplace_back(x < y); break;
                        case Op::LessEqual: stack_.emplace_back(x <= y); break;
                        case Op::Greater: stack_.emplace_back(x > y); break;
                        case Op::GreaterEqual: stack_.emplace_back(x >= y); break;
                        default: break;
                    }
                    break;
                }
                case Op::Equal: case Op::NotEqual: {
                    auto b = pop(inst.line); auto a = pop(inst.line);
                    stack_.emplace_back(inst.op == Op::Equal ? a == b : a != b);
                    break;
                }
                case Op::Negate: {
                    auto value = pop(inst.line);
                    if (!std::holds_alternative<double>(value)) throw Error(inst.line, "Unary '-' requires a number");
                    stack_.emplace_back(-std::get<double>(value));
                    break;
                }
                case Op::Not: { auto value = pop(inst.line); stack_.emplace_back(!truthy(value)); break; }
                case Op::Jump: frame.ip = static_cast<std::size_t>(inst.a); break;
                case Op::JumpIfFalse:
                    if (!truthy(peek(inst.line))) frame.ip = static_cast<std::size_t>(inst.a);
                    break;
                case Op::Call: {
                    std::vector<Value> args(static_cast<std::size_t>(inst.b));
                    for (int i = inst.b - 1; i >= 0; --i) args.at(i) = pop(inst.line);
                    if (inst.a < 0) {
                        stack_.push_back(builtin(inst.a, args, inst.line));
                    } else {
                        pushFrame(static_cast<std::size_t>(inst.a), args, stack_.size(), inst.line);
                    }
                    break;
                }
                case Op::Return: {
                    Value result = pop(inst.line);
                    const std::size_t base = frame.stackBase;
                    frames_.pop_back();
                    stack_.resize(base);
                    if (!frames_.empty()) stack_.push_back(std::move(result));
                    break;
                }
            }
            if (stack_.size() > 10000) throw Error(inst.line, "Operand stack limit exceeded (10,000 values)");
        }
    }
    const std::string& output() const { return output_; }
    std::size_t steps() const { return steps_; }

private:
    struct Frame {
        std::size_t id;
        std::size_t ip;
        std::size_t stackBase;
        std::vector<Value> locals;
    };
    const Program& program_;
    std::vector<Frame> frames_;
    std::vector<Value> stack_;
    std::unordered_map<std::string, Value> globals_;
    std::string output_;
    std::size_t steps_ = 0;
    int currentLine() const {
        if (frames_.empty()) return 0;
        const auto& f = frames_.back();
        const auto& code = program_.functions.at(f.id).code;
        return code.empty() ? 0 : code.at(std::min(f.ip, code.size() - 1)).line;
    }
    void pushFrame(std::size_t id, const std::vector<Value>& args, std::size_t base, int line) {
        if (frames_.size() >= 256) throw Error(line, "Call stack exceeded 256 frames");
        const auto& function = program_.functions.at(id);
        Frame frame{id, 0, base, std::vector<Value>(static_cast<std::size_t>(function.locals))};
        for (std::size_t i = 0; i < args.size(); ++i) frame.locals.at(i) = args[i];
        frames_.push_back(std::move(frame));
    }
    Value pop(int line) {
        if (stack_.empty()) throw Error(line, "Internal VM stack underflow");
        Value value = std::move(stack_.back()); stack_.pop_back(); return value;
    }
    const Value& peek(int line) const {
        if (stack_.empty()) throw Error(line, "Internal VM stack underflow");
        return stack_.back();
    }
    Value builtin(int id, const std::vector<Value>& args, int line) {
        const Value& arg = args.at(0);
        switch (id) {
            case -1: {
                const std::string lineText = valueString(arg) + '\n';
                if (output_.size() + lineText.size() > 1'048'576) throw Error(line, "Output limit exceeded (1 MiB)");
                output_ += lineText;
                return {};
            }
            case -2:
                if (!std::holds_alternative<std::string>(arg)) throw Error(line, "len() expects a string");
                return static_cast<double>(std::get<std::string>(arg).size());
            case -3: return valueString(arg);
            case -4:
                if (!std::holds_alternative<double>(arg)) throw Error(line, "sqrt() expects a number");
                if (std::get<double>(arg) < 0) throw Error(line, "sqrt() requires a non-negative number");
                return std::sqrt(std::get<double>(arg));
            case -5:
                if (!truthy(arg)) throw Error(line, "Assertion failed");
                return {};
            default: throw Error(line, "Unknown builtin");
        }
    }
};

}  // namespace

Result execute(const std::string& source, bool run) {
    Result result;
    const auto start = std::chrono::steady_clock::now();
    try {
        if (source.size() > 131072) throw Error(0, "Source exceeds 128 KiB limit");
        auto tokens = Lexer(source).scan();
        auto syntax = Parser(std::move(tokens)).parse();
        auto program = Compiler().compile(syntax);
        result.functions = program.functions.size() - 1;
        for (const auto& fn : program.functions) result.instructions += fn.code.size();
        result.disassembly = disassemble(program);
        if (run) {
            VM vm(program);
            try { vm.run(); }
            catch (...) { result.output = vm.output(); result.steps = vm.steps(); throw; }
            result.output = vm.output();
            result.steps = vm.steps();
        }
        result.ok = true;
    } catch (const Error& e) {
        result.error = e.what();
        result.errorLine = e.line;
    } catch (const std::exception& e) {
        result.error = std::string("Unexpected engine error: ") + e.what();
    }
    const auto end = std::chrono::steady_clock::now();
    result.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

}  // namespace astra
