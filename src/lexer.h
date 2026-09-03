#pragma once
#include <string>
#include <vector>
#include "token.h"

class Lexer {
public:
    // The source is read in place for the lexer's lifetime, so it has to
    // outlive it; a temporary is refused at compile time.
    explicit Lexer(const std::string& source);
    explicit Lexer(std::string&&) = delete;
    std::vector<Token> tokenize();

private:
    const std::string& src;
    size_t pos = 0;
    int line = 1;
    int col = 1;

    char peek() const;
    char peek_next() const;
    char advance();
    void skip_whitespace();
    void skip_comment();
    Token make_token(TokenType type, const std::string& val);
    Token read_string();
    Token read_number();
    Token read_identifier();
};
