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

    TokenType kwtype;
    if (keyword_lookup(upper, kwtype)) return make_token(kwtype, upper);
    return make_token(TokenType::IDENTIFIER, upper);
}

Token Lexer::next() {
    Token t;
    if (!pending.empty()) { t = std::move(pending.front()); pending.pop_front(); }
    else t = scan();
    prev_type = t.type;
    return t;
}

// An old name from jdb_aliases.h at pos becomes the tokens of its new name.
// The longest dotted name that has an alias wins: HTTP.GET_ASYNC$ before HTTP.
bool Lexer::read_alias(Token& out) {
    if (prev_type == TokenType::DOT) return false;
    auto word_end = [&](size_t q) {
        while (q < src.size() && (std::isalnum((unsigned char)src[q]) || src[q] == '_')) ++q;
        if (q < src.size() && src[q] == '$') ++q;
        return q;
    };
    std::string name;
    std::vector<std::pair<std::string, size_t>> prefixes;
    size_t q = pos;
    for (;;) {
        size_t e = word_end(q);
        name += src.substr(q, e - q);
        prefixes.emplace_back(name, e);
        if (e + 1 < src.size() && src[e] == '.' &&
            (std::isalpha((unsigned char)src[e + 1]) || src[e + 1] == '_')) {
            name += '.';
            q = e + 1;
        } else {
            break;
        }
    }
    for (size_t k = prefixes.size(); k-- > 0; ) {
        std::string upper = prefixes[k].first;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        const NameAlias* a = jdb_name_alias(upper);
        if (!a) continue;
        size_t e = prefixes[k].second;
        size_t n = e;
        while (n < src.size() && (src[n] == ' ' || src[n] == '\t')) ++n;
        char follow = n < src.size() ? src[n] : '\0';
        if (!a->bare && follow != '(' && follow != '@') return false;
        while (pos < e) advance();
        uses.push_back({"", line, upper, a->new_name});
        std::string_view rest(a->new_name);
        bool first = true;
        while (!rest.empty()) {
            size_t dot = rest.find('.');
            std::string seg(rest.substr(0, dot));
            rest = dot == std::string_view::npos ? std::string_view() : rest.substr(dot + 1);
            TokenType kw;
            Token t = make_token(keyword_lookup(seg, kw) ? kw : TokenType::IDENTIFIER, seg);
            if (first) { out = t; first = false; }
            else { pending.push_back(make_token(TokenType::DOT, ".")); pending.push_back(t); }
        }
        return true;
    }
    return false;
}

Token Lexer::scan() {
    if (done) return make_token(TokenType::EOF_TOKEN, "");

    while (pos < src.size()) {
        skip_whitespace();
        if (pos >= src.size()) break;

        char c = peek();

        if (c == '\n') {
            advance();
            if (!last_was_newline) {
                last_was_newline = true;
                return make_token(TokenType::NEWLINE, "\\n");
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
        if (c == '"') return read_string();

        // Interpolated string: $"text {{ expr }} text". The body is read by
        // the ordinary string reader, so the doubled-quote rule holds inside
        // it and the parser sees text with real quotes in it.
        if (c == '$' && peek_next() == '"') {
            advance();
            Token t = read_string();
            t.type = TokenType::INTERP_STRING;
            return t;
        }

        // Hex literal: $FF, $80000
        if (c == '$' && std::isxdigit(peek_next())) {
            advance(); // skip $
            std::string hex;
            while (pos < src.size() && std::isxdigit(peek())) hex += advance();
            return make_token(TokenType::INTEGER_LIT,
                std::to_string((int64_t)std::stoull(hex, nullptr, 16)));
        }

        // Binary literal: %0101  (classic-BASIC syntax)
        if (c == '%' && (peek_next() == '0' || peek_next() == '1')) {
            advance(); // skip %
            std::string bin;
            while (pos < src.size() && (peek() == '0' || peek() == '1')) bin += advance();
            return make_token(TokenType::INTEGER_LIT,
                std::to_string((int64_t)std::stoull(bin, nullptr, 2)));
        }

        // Number
        if (std::isdigit(c) || (c == '.' && std::isdigit(peek_next()))) return read_number();

        // Identifier / keyword
        if (std::isalpha(c) || c == '_') {
#ifndef JDB_MCU
            Token aliased;
            if (read_alias(aliased)) return aliased;
#endif
            Token t = read_identifier();
            // A lone underscore before the end of the line joins it with
            // the next one: neither the underscore nor the newline is a
            // token.
            if (t.type == TokenType::IDENTIFIER && t.value == "_") {
                skip_whitespace();
                if (peek() == '\'') skip_comment();
                if (peek() == '\n') { advance(); continue; }
            }
            return t;
        }

        // Operators and punctuation
        switch (c) {
            case '+': advance(); return make_token(TokenType::PLUS, "+");
            case '-':
                advance();
                if (peek() == '>') { advance(); return make_token(TokenType::ARROW, "->"); }
                return make_token(TokenType::MINUS, "-");
            case '*': advance(); return make_token(TokenType::STAR, "*");
            case '/': advance(); return make_token(TokenType::SLASH, "/");
            case '\\': advance(); return make_token(TokenType::BACKSLASH, "\\");
            case '^': advance(); return make_token(TokenType::CARET, "^");
            case '(': advance(); return make_token(TokenType::LPAREN, "(");
            case ')': advance(); return make_token(TokenType::RPAREN, ")");
            case '[': advance(); return make_token(TokenType::LBRACKET, "[");
            case ']': advance(); return make_token(TokenType::RBRACKET, "]");
            case ',': advance(); return make_token(TokenType::COMMA, ",");
            case ':': advance(); return make_token(TokenType::COLON, ":");
            case ';': advance(); return make_token(TokenType::SEMICOLON, ";");
            case '.': advance(); return make_token(TokenType::DOT, ".");
            case '@': advance(); return make_token(TokenType::AT, "@");
            case '?':
                advance();
                // Two of them is the null-coalescing operator; one on its own
                // stays the pipe placeholder.
                if (peek() == '?') { advance(); return make_token(TokenType::COALESCE, "??"); }
                return make_token(TokenType::PLACEHOLDER, "?");
            case '|':
                advance();
                if (peek() == '>') { advance(); return make_token(TokenType::PIPE, "|>"); }
                throw std::runtime_error("Unexpected '|' at line " + std::to_string(line));
            case '{': advance(); return make_token(TokenType::LBRACE, "{");
            case '}': advance(); return make_token(TokenType::RBRACE, "}");
            case '<':
                advance();
                if (peek() == '=') { advance(); return make_token(TokenType::LE, "<="); }
                if (peek() == '>') { advance(); return make_token(TokenType::NE, "<>"); }
                return make_token(TokenType::LT, "<");
            case '>':
                advance();
                if (peek() == '=') { advance(); return make_token(TokenType::GE, ">="); }
                return make_token(TokenType::GT, ">");
            case '=': advance(); return make_token(TokenType::ASSIGN, "=");
            default:
                throw std::runtime_error("Unexpected character '" + std::string(1, c) +
                    "' at line " + std::to_string(line) + ":" + std::to_string(col));
        }
    }

    if (!last_was_newline) {
        last_was_newline = true;
        return make_token(TokenType::NEWLINE, "\\n");
    }
    done = true;
    return make_token(TokenType::EOF_TOKEN, "");
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    for (;;) {
        tokens.push_back(next());
        if (tokens.back().type == TokenType::EOF_TOKEN) break;
    }
    return tokens;
}
