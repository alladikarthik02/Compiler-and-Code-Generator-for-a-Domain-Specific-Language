#include "lexer.h"
#include <cctype>
#include <unordered_map>

namespace dsl {

const char* tok_name(Tok t) {
    switch (t) {
        case Tok::Int: return "Int";
        case Tok::Ident: return "Ident";
        case Tok::KwFn: return "fn";
        case Tok::KwLet: return "let";
        case Tok::KwIf: return "if";
        case Tok::KwElse: return "else";
        case Tok::KwWhile: return "while";
        case Tok::KwReturn: return "return";
        case Tok::KwPrint: return "print";
        case Tok::KwInt: return "int";
        case Tok::KwBool: return "bool";
        case Tok::KwTrue: return "true";
        case Tok::KwFalse: return "false";
        case Tok::LParen: return "(";
        case Tok::RParen: return ")";
        case Tok::LBrace: return "{";
        case Tok::RBrace: return "}";
        case Tok::Comma: return ",";
        case Tok::Colon: return ":";
        case Tok::Semicolon: return ";";
        case Tok::Arrow: return "->";
        case Tok::Plus: return "+";
        case Tok::Minus: return "-";
        case Tok::Star: return "*";
        case Tok::Slash: return "/";
        case Tok::Percent: return "%";
        case Tok::Assign: return "=";
        case Tok::Eq: return "==";
        case Tok::Ne: return "!=";
        case Tok::Lt: return "<";
        case Tok::Le: return "<=";
        case Tok::Gt: return ">";
        case Tok::Ge: return ">=";
        case Tok::AndAnd: return "&&";
        case Tok::OrOr: return "||";
        case Tok::Bang: return "!";
        case Tok::Eof: return "<eof>";
        case Tok::Error: return "<error>";
    }
    return "<?>";
}

static const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> kw = {
        {"fn", Tok::KwFn}, {"let", Tok::KwLet}, {"if", Tok::KwIf},
        {"else", Tok::KwElse}, {"while", Tok::KwWhile}, {"return", Tok::KwReturn},
        {"print", Tok::KwPrint}, {"int", Tok::KwInt}, {"bool", Tok::KwBool},
        {"true", Tok::KwTrue}, {"false", Tok::KwFalse},
    };
    return kw;
}

Lexer::Lexer(std::string src, std::string filename)
    : src_(std::move(src)), file_(std::move(filename)) {}

char Lexer::peek(int ahead) const {
    size_t p = pos_ + ahead;
    return p < src_.size() ? src_[p] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; }
    else { col_++; }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || src_[pos_] != expected) return false;
    advance();
    return true;
}

Token Lexer::make(Tok kind, const std::string& lexeme, int line, int col) {
    return Token{kind, lexeme, line, col, 0};
}

void Lexer::add_error(const std::string& msg, int line, int col) {
    errors_.push_back({msg, line, col});
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (!at_end()) {
        // Skip whitespace and // comments.
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') advance();
            continue;
        }

        int start_line = line_, start_col = col_;

        // Integer literal.
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num;
            while (std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
            Token t = make(Tok::Int, num, start_line, start_col);
            try {
                t.int_val = std::stoll(num);
            } catch (const std::out_of_range&) {
                add_error("integer literal out of range: " + num, start_line, start_col);
                t.kind = Tok::Error;
            }
            out.push_back(t);
            continue;
        }

        // Identifier or keyword.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string id;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') id += advance();
            auto it = keywords().find(id);
            Tok k = (it != keywords().end()) ? it->second : Tok::Ident;
            out.push_back(make(k, id, start_line, start_col));
            continue;
        }

        // Operators and punctuation.
        advance(); // consume c
        switch (c) {
            case '(': out.push_back(make(Tok::LParen, "(", start_line, start_col)); break;
            case ')': out.push_back(make(Tok::RParen, ")", start_line, start_col)); break;
            case '{': out.push_back(make(Tok::LBrace, "{", start_line, start_col)); break;
            case '}': out.push_back(make(Tok::RBrace, "}", start_line, start_col)); break;
            case ',': out.push_back(make(Tok::Comma, ",", start_line, start_col)); break;
            case ':': out.push_back(make(Tok::Colon, ":", start_line, start_col)); break;
            case ';': out.push_back(make(Tok::Semicolon, ";", start_line, start_col)); break;
            case '+': out.push_back(make(Tok::Plus, "+", start_line, start_col)); break;
            case '*': out.push_back(make(Tok::Star, "*", start_line, start_col)); break;
            case '/': out.push_back(make(Tok::Slash, "/", start_line, start_col)); break;
            case '%': out.push_back(make(Tok::Percent, "%", start_line, start_col)); break;
            case '-':
                if (match('>')) out.push_back(make(Tok::Arrow, "->", start_line, start_col));
                else out.push_back(make(Tok::Minus, "-", start_line, start_col));
                break;
            case '=':
                if (match('=')) out.push_back(make(Tok::Eq, "==", start_line, start_col));
                else out.push_back(make(Tok::Assign, "=", start_line, start_col));
                break;
            case '!':
                if (match('=')) out.push_back(make(Tok::Ne, "!=", start_line, start_col));
                else out.push_back(make(Tok::Bang, "!", start_line, start_col));
                break;
            case '<':
                if (match('=')) out.push_back(make(Tok::Le, "<=", start_line, start_col));
                else out.push_back(make(Tok::Lt, "<", start_line, start_col));
                break;
            case '>':
                if (match('=')) out.push_back(make(Tok::Ge, ">=", start_line, start_col));
                else out.push_back(make(Tok::Gt, ">", start_line, start_col));
                break;
            case '&':
                if (match('&')) out.push_back(make(Tok::AndAnd, "&&", start_line, start_col));
                else { add_error("unexpected '&' (did you mean '&&'?)", start_line, start_col);
                       out.push_back(make(Tok::Error, "&", start_line, start_col)); }
                break;
            case '|':
                if (match('|')) out.push_back(make(Tok::OrOr, "||", start_line, start_col));
                else { add_error("unexpected '|' (did you mean '||'?)", start_line, start_col);
                       out.push_back(make(Tok::Error, "|", start_line, start_col)); }
                break;
            default:
                add_error(std::string("unexpected character '") + c + "'", start_line, start_col);
                out.push_back(make(Tok::Error, std::string(1, c), start_line, start_col));
                break;
        }
    }
    out.push_back(make(Tok::Eof, "", line_, col_));
    return out;
}

} // namespace dsl
