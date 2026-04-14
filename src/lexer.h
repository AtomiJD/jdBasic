#pragma once
#include <string>
#include <vector>
#include "token.h"

class Lexer {
public:
    explicit Lexer(const std::string& source);
    std::vector<Token> tokenize();

private:
    std::string src;
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
