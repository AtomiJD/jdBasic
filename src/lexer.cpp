#include "lexer.h"
#include <algorithm>
#include <stdexcept>

Lexer::Lexer(const std::string& source) : src(source) {}

char Lexer::peek() const {
    if (pos >= src.size()) return '\0';
    return src[pos];
}

char Lexer::peek_next() const {
    if (pos + 1 >= src.size()) return '\0';
    return src[pos + 1];
}

char Lexer::advance() {
    char c = src[pos++];
    if (c == '\n') { line++; col = 1; } else { col++; }
    return c;
}

void Lexer::skip_whitespace() {
    while (pos < src.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
        } else {
            break;
        }
    }
}

void Lexer::skip_comment() {
    // REM or ' for comments
    while (pos < src.size() && peek() != '\n') advance();
}

Token Lexer::make_token(TokenType type, const std::string& val) {
    return {type, val, line, col};
}

Token Lexer::read_string() {
    advance(); // skip opening "
    std::string result;
    // BASIC string literals are RAW: backslashes are literal characters,
    // so paths like "C:\Users\test" or "\" parse as the user expects.
    // Use "" (doubled quote) to embed a literal quote, CHR$(10) for newline,
    // CHR$(9) for tab, VBNEWLINE / VBCRLF / VBTAB for the common cases.
    while (pos < src.size()) {
        char c = peek();
        if (c == '"') {
            if (pos + 1 < src.size() && src[pos + 1] == '"') {
                result += '"';
                advance(); advance();
                continue;
            }
            break; // real closing quote
        }
        result += advance();
    }
    if (pos < src.size()) advance(); // skip closing "
    return make_token(TokenType::STRING_LIT, result);
}

Token Lexer::read_number() {
    std::string num;
    bool is_float = false;
    while (pos < src.size() && (std::isdigit(peek()) || peek() == '.')) {
        if (peek() == '.') {
            if (is_float) break;
            is_float = true;
        }
        num += advance();
    }
    // Scientific notation
    if (pos < src.size() && (peek() == 'e' || peek() == 'E')) {
        is_float = true;
        num += advance();
        if (pos < src.size() && (peek() == '+' || peek() == '-')) {
            num += advance();
        }
        while (pos < src.size() && std::isdigit(peek())) {
            num += advance();
        }
    }
    return make_token(is_float ? TokenType::FLOAT_LIT : TokenType::INTEGER_LIT, num);
}

Token Lexer::read_identifier() {
    std::string id;
    while (pos < src.size() && (std::isalnum(peek()) || peek() == '_')) {
        id += advance();
    }
    // Allow trailing $ for BASIC string-type function names (GETENV$, DATE$, etc.)
    if (pos < src.size() && peek() == '$') {
        id += advance();
    }
    // BASIC is case-insensitive for keywords
    std::string upper = id;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // Check for REM comment
    if (upper == "REM") {
        skip_comment();
        return make_token(TokenType::NEWLINE, "\\n");
    }

    auto& kw = keywords();
    auto it = kw.find(upper);
    if (it != kw.end()) {
        return make_token(it->second, upper);
    }
    return make_token(TokenType::IDENTIFIER, upper);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    bool last_was_newline = true;

    while (pos < src.size()) {
        skip_whitespace();
        if (pos >= src.size()) break;

        char c = peek();

        if (c == '\n') {
            advance();
            // Line continuation: if last non-whitespace token was '_', remove it
            // and skip the newline (join with next line)
            if (!tokens.empty() && tokens.back().type == TokenType::IDENTIFIER &&
                tokens.back().value == "_") {
                tokens.pop_back(); // remove the '_' token
                // Don't emit NEWLINE - lines are joined
                continue;
            }
            if (!last_was_newline) {
                tokens.push_back(make_token(TokenType::NEWLINE, "\\n"));
                last_was_newline = true;
            }
            continue;
        }

        last_was_newline = false;

        // Comment with '
        if (c == '\'') {
            skip_comment();
            continue;
        }

        // String
        if (c == '"') {
            tokens.push_back(read_string());
            continue;
        }

        // Hex literal: $FF, $80000
        if (c == '$' && std::isxdigit(peek_next())) {
            advance(); // skip $
            std::string hex;
            while (pos < src.size() && std::isxdigit(peek())) hex += advance();
            tokens.push_back(make_token(TokenType::INTEGER_LIT,
                std::to_string((int64_t)std::stoull(hex, nullptr, 16))));
            continue;
        }

        // Binary literal: %0101  (classic-BASIC syntax)
        if (c == '%' && (peek_next() == '0' || peek_next() == '1')) {
            advance(); // skip %
            std::string bin;
            while (pos < src.size() && (peek() == '0' || peek() == '1')) bin += advance();
            tokens.push_back(make_token(TokenType::INTEGER_LIT,
                std::to_string((int64_t)std::stoull(bin, nullptr, 2))));
            continue;
        }

        // Number
        if (std::isdigit(c) || (c == '.' && std::isdigit(peek_next()))) {
            tokens.push_back(read_number());
            continue;
        }

        // Identifier / keyword
        if (std::isalpha(c) || c == '_') {
            tokens.push_back(read_identifier());
            continue;
        }

        // Operators and punctuation
        switch (c) {
            case '+': advance(); tokens.push_back(make_token(TokenType::PLUS, "+")); break;
            case '-':
                advance();
                if (peek() == '>') { advance(); tokens.push_back(make_token(TokenType::ARROW, "->")); }
                else { tokens.push_back(make_token(TokenType::MINUS, "-")); }
                break;
            case '*': advance(); tokens.push_back(make_token(TokenType::STAR, "*")); break;
            case '/': advance(); tokens.push_back(make_token(TokenType::SLASH, "/")); break;
            case '\\': advance(); tokens.push_back(make_token(TokenType::BACKSLASH, "\\")); break;
            case '^': advance(); tokens.push_back(make_token(TokenType::CARET, "^")); break;
            case '(': advance(); tokens.push_back(make_token(TokenType::LPAREN, "(")); break;
            case ')': advance(); tokens.push_back(make_token(TokenType::RPAREN, ")")); break;
            case '[': advance(); tokens.push_back(make_token(TokenType::LBRACKET, "[")); break;
            case ']': advance(); tokens.push_back(make_token(TokenType::RBRACKET, "]")); break;
            case ',': advance(); tokens.push_back(make_token(TokenType::COMMA, ",")); break;
            case ':': advance(); tokens.push_back(make_token(TokenType::COLON, ":")); break;
            case ';': advance(); tokens.push_back(make_token(TokenType::SEMICOLON, ";")); break;
            case '.': advance(); tokens.push_back(make_token(TokenType::DOT, ".")); break;
            case '@': advance(); tokens.push_back(make_token(TokenType::AT, "@")); break;
            case '?': advance(); tokens.push_back(make_token(TokenType::PLACEHOLDER, "?")); break;
            case '|':
                advance();
                if (peek() == '>') { advance(); tokens.push_back(make_token(TokenType::PIPE, "|>")); }
                else { throw std::runtime_error("Unexpected '|' at line " + std::to_string(line)); }
                break;
            case '{': advance(); tokens.push_back(make_token(TokenType::LBRACE, "{")); break;
            case '}': advance(); tokens.push_back(make_token(TokenType::RBRACE, "}")); break;
            case '<':
                advance();
                if (peek() == '=') { advance(); tokens.push_back(make_token(TokenType::LE, "<=")); }
                else if (peek() == '>') { advance(); tokens.push_back(make_token(TokenType::NE, "<>")); }
                else { tokens.push_back(make_token(TokenType::LT, "<")); }
                break;
            case '>':
                advance();
                if (peek() == '=') { advance(); tokens.push_back(make_token(TokenType::GE, ">=")); }
                else { tokens.push_back(make_token(TokenType::GT, ">")); }
                break;
            case '=': advance(); tokens.push_back(make_token(TokenType::ASSIGN, "=")); break;
            default:
                throw std::runtime_error("Unexpected character '" + std::string(1, c) +
                    "' at line " + std::to_string(line) + ":" + std::to_string(col));
        }
    }

    if (!last_was_newline) {
        tokens.push_back(make_token(TokenType::NEWLINE, "\\n"));
    }
    tokens.push_back(make_token(TokenType::EOF_TOKEN, ""));
    return tokens;
}
