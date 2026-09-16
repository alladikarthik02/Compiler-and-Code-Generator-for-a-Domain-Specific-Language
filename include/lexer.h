#pragma once
#include "token.h"
#include <string>
#include <vector>

namespace dsl {

struct LexError {
    std::string message;
    int line, col;
};

class Lexer {
public:
    Lexer(std::string src, std::string filename = "<input>");

    // Scans the whole input. Always ends with an Eof token. Lexical errors are
    // collected in errors() and also emitted as Tok::Error tokens so callers can
    // choose to stop or keep going.
    std::vector<Token> tokenize();

    const std::vector<LexError>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }

private:
    char peek(int ahead = 0) const;
    char advance();
    bool match(char expected);
    bool at_end() const { return pos_ >= src_.size(); }

    Token make(Tok kind, const std::string& lexeme, int line, int col);
    void add_error(const std::string& msg, int line, int col);

    std::string src_;
    std::string file_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<LexError> errors_;
};

} // namespace dsl
