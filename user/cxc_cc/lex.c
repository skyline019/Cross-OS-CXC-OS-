#include "cxc_cc/cc.h"

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void cc_lex_init(CcLexer* lx, const char* src, size_t len) {
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->tok = TOK_EOF;
    lx->number = 0;
    lx->text[0] = 0;
    cc_lex_next(lx);
}

static void set_kw_or_ident(CcLexer* lx) {
    if (strcmp(lx->text, "if") == 0) {
        lx->tok = TOK_KW_IF;
    } else if (strcmp(lx->text, "else") == 0) {
        lx->tok = TOK_KW_ELSE;
    } else if (strcmp(lx->text, "while") == 0) {
        lx->tok = TOK_KW_WHILE;
    } else if (strcmp(lx->text, "return") == 0) {
        lx->tok = TOK_KW_RETURN;
    } else if (strcmp(lx->text, "write") == 0) {
        lx->tok = TOK_KW_WRITE;
    } else if (strcmp(lx->text, "print") == 0) {
        lx->tok = TOK_KW_PRINT;
    } else if (strcmp(lx->text, "input") == 0) {
        lx->tok = TOK_KW_INPUT;
    } else if (strcmp(lx->text, "fn") == 0) {
        lx->tok = TOK_KW_FN;
    } else if (strcmp(lx->text, "struct") == 0) {
        lx->tok = TOK_KW_STRUCT;
    } else if (strcmp(lx->text, "for") == 0) {
        lx->tok = TOK_KW_FOR;
    } else if (strcmp(lx->text, "break") == 0) {
        lx->tok = TOK_KW_BREAK;
    } else if (strcmp(lx->text, "continue") == 0) {
        lx->tok = TOK_KW_CONTINUE;
    } else {
        lx->tok = TOK_IDENT;
    }
}

void cc_lex_next(CcLexer* lx) {
    while (lx->pos < lx->len && is_space(lx->src[lx->pos])) {
        lx->pos++;
    }
    if (lx->pos >= lx->len) {
        lx->tok = TOK_EOF;
        return;
    }
    const char c = lx->src[lx->pos];
    if (is_alpha(c)) {
        size_t n = 0;
        while (lx->pos < lx->len && is_alnum(lx->src[lx->pos])) {
            if (n < CC_IDENT_MAX) {
                lx->text[n++] = lx->src[lx->pos];
            }
            lx->pos++;
        }
        lx->text[n] = 0;
        set_kw_or_ident(lx);
        return;
    }
    if (c >= '0' && c <= '9') {
        long v = 0;
        while (lx->pos < lx->len && lx->src[lx->pos] >= '0' &&
               lx->src[lx->pos] <= '9') {
            v = v * 10 + (lx->src[lx->pos] - '0');
            if (v > 255) {
                v = 255;
            }
            lx->pos++;
        }
        lx->number = v;
        lx->tok = TOK_NUMBER;
        return;
    }
    lx->pos++;
    if (c == '(') {
        lx->tok = TOK_LPAREN;
    } else if (c == ')') {
        lx->tok = TOK_RPAREN;
    } else if (c == '{') {
        lx->tok = TOK_LBRACE;
    } else if (c == '}') {
        lx->tok = TOK_RBRACE;
    } else if (c == '[') {
        lx->tok = TOK_LBRACK;
    } else if (c == ']') {
        lx->tok = TOK_RBRACK;
    } else if (c == ';') {
        lx->tok = TOK_SEMI;
    } else if (c == ',') {
        lx->tok = TOK_COMMA;
    } else if (c == '.') {
        lx->tok = TOK_DOT;
    } else if (c == '+') {
        lx->tok = TOK_PLUS;
    } else if (c == '-') {
        lx->tok = TOK_MINUS;
    } else if (c == '*') {
        lx->tok = TOK_STAR;
    } else if (c == '/') {
        lx->tok = TOK_SLASH;
    } else if (c == '!') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++;
            lx->tok = TOK_NE;
        } else {
            lx->tok = TOK_BANG;
        }
    } else if (c == '&') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '&') {
            lx->pos++;
            lx->tok = TOK_AMPAMP;
        } else {
            lx->tok = TOK_AMP;
        }
    } else if (c == '|') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '|') {
            lx->pos++;
            lx->tok = TOK_BARBAR;
        } else {
            lx->tok = TOK_EOF;
        }
    } else if (c == '=') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++;
            lx->tok = TOK_EQ;
        } else {
            lx->tok = TOK_ASSIGN;
        }
    } else if (c == '<') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++;
            lx->tok = TOK_LE;
        } else {
            lx->tok = TOK_LT;
        }
    } else if (c == '>') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++;
            lx->tok = TOK_GE;
        } else {
            lx->tok = TOK_GT;
        }
    } else {
        lx->tok = TOK_EOF;
    }
}
