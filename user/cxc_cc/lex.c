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

/* Advance one source byte and keep scan_line/scan_col in sync. */
static void lex_adv(CcLexer* lx) {
    if (lx->pos >= lx->len) {
        return;
    }
    if (lx->src[lx->pos] == '\n') {
        lx->scan_line++;
        lx->scan_col = 1;
    } else {
        lx->scan_col++;
    }
    lx->pos++;
}

void cc_lex_init(CcLexer* lx, const char* src, size_t len) {
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->tok = TOK_EOF;
    lx->number = 0;
    lx->text[0] = 0;
    lx->str[0] = 0;
    lx->str_len = 0;
    lx->line = 1;
    lx->col = 1;
    lx->scan_line = 1;
    lx->scan_col = 1;
    cc_lex_next(lx);
}

static void set_kw_or_ident(CcLexer* lx) {
    if (strcmp(lx->text, "if") == 0) {
        lx->tok = TOK_KW_IF;
    } else if (strcmp(lx->text, "else") == 0) {
        lx->tok = TOK_KW_ELSE;
    } else if (strcmp(lx->text, "while") == 0) {
        lx->tok = TOK_KW_WHILE;
    } else if (strcmp(lx->text, "do") == 0) {
        lx->tok = TOK_KW_DO;
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
    } else if (strcmp(lx->text, "int") == 0) {
        lx->tok = TOK_KW_INT;
    } else if (strcmp(lx->text, "switch") == 0) {
        lx->tok = TOK_KW_SWITCH;
    } else if (strcmp(lx->text, "case") == 0) {
        lx->tok = TOK_KW_CASE;
    } else if (strcmp(lx->text, "default") == 0) {
        lx->tok = TOK_KW_DEFAULT;
    } else if (strcmp(lx->text, "enum") == 0) {
        lx->tok = TOK_KW_ENUM;
    } else if (strcmp(lx->text, "sizeof") == 0) {
        lx->tok = TOK_KW_SIZEOF;
    } else if (strcmp(lx->text, "extern") == 0) {
        lx->tok = TOK_KW_EXTERN;
    } else if (strcmp(lx->text, "import") == 0) {
        lx->tok = TOK_KW_IMPORT;
    } else if (strcmp(lx->text, "typedef") == 0) {
        lx->tok = TOK_KW_TYPEDEF;
    } else if (strcmp(lx->text, "void") == 0) {
        lx->tok = TOK_KW_VOID;
    } else if (strcmp(lx->text, "char") == 0) {
        lx->tok = TOK_KW_CHAR;
    } else if (strcmp(lx->text, "long") == 0) {
        lx->tok = TOK_KW_LONG;
    } else if (strcmp(lx->text, "const") == 0) {
        lx->tok = TOK_KW_CONST;
    } else if (strcmp(lx->text, "unsigned") == 0) {
        lx->tok = TOK_KW_UNSIGNED;
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
        lex_adv(lx);
    }
    lx->line = lx->scan_line;
    lx->col = lx->scan_col;
    if (lx->pos >= lx->len) {
        lx->tok = TOK_EOF;
        return;
    }
    const char c = lx->src[lx->pos];
    if (c == '"') {
        size_t n = 0;
        lex_adv(lx);
        while (lx->pos < lx->len && lx->src[lx->pos] != '"' &&
               lx->src[lx->pos] != '\n') {
            char ch = lx->src[lx->pos];
            lex_adv(lx);
            if (ch == '\\') {
                if (lx->pos >= lx->len) {
                    lx->tok = TOK_EOF;
                    return;
                }
                ch = lx->src[lx->pos];
                lex_adv(lx);
                if (ch == 'n') {
                    ch = '\n';
                } else if (ch == 't') {
                    ch = '\t';
                } else if (ch == '0') {
                    ch = 0;
                } else if (ch == 'x' || ch == 'X') {
                    int d1;
                    int d2;
                    if (lx->pos + 1 >= lx->len) {
                        lx->tok = TOK_UNKNOWN;
                        return;
                    }
                    {
                        char h1 = lx->src[lx->pos];
                        lex_adv(lx);
                        char h2 = lx->src[lx->pos];
                        lex_adv(lx);
                        if (h1 >= '0' && h1 <= '9') {
                            d1 = h1 - '0';
                        } else if (h1 >= 'a' && h1 <= 'f') {
                            d1 = 10 + (h1 - 'a');
                        } else if (h1 >= 'A' && h1 <= 'F') {
                            d1 = 10 + (h1 - 'A');
                        } else {
                            lx->tok = TOK_UNKNOWN;
                            return;
                        }
                        if (h2 >= '0' && h2 <= '9') {
                            d2 = h2 - '0';
                        } else if (h2 >= 'a' && h2 <= 'f') {
                            d2 = 10 + (h2 - 'a');
                        } else if (h2 >= 'A' && h2 <= 'F') {
                            d2 = 10 + (h2 - 'A');
                        } else {
                            lx->tok = TOK_UNKNOWN;
                            return;
                        }
                        ch = (char)((d1 << 4) | d2);
                    }
                } else if (ch == '\\' || ch == '"') {
                    /* keep ch */
                } else {
                    lx->tok = TOK_UNKNOWN;
                    return;
                }
            }
            if (n >= CC_STR_MAX) {
                lx->tok = TOK_UNKNOWN;
                return;
            }
            lx->str[n++] = ch;
        }
        if (lx->pos >= lx->len || lx->src[lx->pos] != '"') {
            lx->tok = TOK_EOF;
            return;
        }
        lex_adv(lx);
        lx->str[n] = 0;
        lx->str_len = n;
        lx->tok = TOK_STR;
        return;
    }
    if (c == '\'') {
        lex_adv(lx);
        if (lx->pos >= lx->len) {
            lx->tok = TOK_EOF;
            return;
        }
        lx->number = (unsigned char)lx->src[lx->pos];
        lex_adv(lx);
        if (lx->pos >= lx->len || lx->src[lx->pos] != '\'') {
            lx->tok = TOK_EOF;
            return;
        }
        lex_adv(lx);
        lx->tok = TOK_NUMBER;
        return;
    }
    if (is_alpha(c)) {
        size_t n = 0;
        while (lx->pos < lx->len && is_alnum(lx->src[lx->pos])) {
            if (n < CC_IDENT_MAX) {
                lx->text[n++] = lx->src[lx->pos];
            }
            lex_adv(lx);
        }
        lx->text[n] = 0;
        set_kw_or_ident(lx);
        return;
    }
    if (c >= '0' && c <= '9') {
        unsigned long long v = 0;
        if (c == '0' && lx->pos + 1 < lx->len &&
            (lx->src[lx->pos + 1] == 'x' || lx->src[lx->pos + 1] == 'X')) {
            lex_adv(lx);
            lex_adv(lx);
            if (lx->pos >= lx->len) {
                lx->tok = TOK_EOF;
                return;
            }
            while (lx->pos < lx->len) {
                char h = lx->src[lx->pos];
                int d;
                if (h >= '0' && h <= '9') {
                    d = h - '0';
                } else if (h >= 'a' && h <= 'f') {
                    d = 10 + (h - 'a');
                } else if (h >= 'A' && h <= 'F') {
                    d = 10 + (h - 'A');
                } else {
                    break;
                }
                v = (v << 4) + (unsigned long long)d;
                lex_adv(lx);
            }
            lx->number = (long long)v;
            lx->tok = TOK_NUMBER;
            return;
        }
        while (lx->pos < lx->len && lx->src[lx->pos] >= '0' &&
               lx->src[lx->pos] <= '9') {
            v = v * 10ull + (unsigned long long)(lx->src[lx->pos] - '0');
            if (v > 0x7fffffffffffffffull) {
                v = 0x7fffffffffffffffull;
            }
            lex_adv(lx);
        }
        lx->number = (long long)v;
        lx->tok = TOK_NUMBER;
        return;
    }
    lex_adv(lx);
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
    } else if (c == '?') {
        lx->tok = TOK_QUESTION;
    } else if (c == ':') {
        lx->tok = TOK_COLON;
    } else if (c == '%') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_PERCENTEQ;
        } else {
            lx->tok = TOK_PERCENT;
        }
    } else if (c == '^') {
        lx->tok = TOK_CARET;
    } else if (c == '~') {
        lx->tok = TOK_TILDE;
    } else if (c == '+') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_PLUSEQ;
        } else if (lx->pos < lx->len && lx->src[lx->pos] == '+') {
            lex_adv(lx);
            lx->tok = TOK_PLUSPLUS;
        } else {
            lx->tok = TOK_PLUS;
        }
    } else if (c == '-') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_MINUSEQ;
        } else if (lx->pos < lx->len && lx->src[lx->pos] == '-') {
            lex_adv(lx);
            lx->tok = TOK_MINUSMINUS;
        } else if (lx->pos < lx->len && lx->src[lx->pos] == '>') {
            lex_adv(lx);
            lx->tok = TOK_ARROW;
        } else {
            lx->tok = TOK_MINUS;
        }
    } else if (c == '*') {
        lx->tok = TOK_STAR;
    } else if (c == '/') {
        lx->tok = TOK_SLASH;
    } else if (c == '!') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_NE;
        } else {
            lx->tok = TOK_BANG;
        }
    } else if (c == '&') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '&') {
            lex_adv(lx);
            lx->tok = TOK_AMPAMP;
        } else {
            lx->tok = TOK_AMP;
        }
    } else if (c == '|') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '|') {
            lex_adv(lx);
            lx->tok = TOK_BARBAR;
        } else {
            lx->tok = TOK_EOF;
        }
    } else if (c == '=') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_EQ;
        } else {
            lx->tok = TOK_ASSIGN;
        }
    } else if (c == '<') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '<') {
            lex_adv(lx);
            lx->tok = TOK_LSHIFT;
        } else if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_LE;
        } else {
            lx->tok = TOK_LT;
        }
    } else if (c == '>') {
        if (lx->pos < lx->len && lx->src[lx->pos] == '>') {
            lex_adv(lx);
            lx->tok = TOK_RSHIFT;
        } else if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lex_adv(lx);
            lx->tok = TOK_GE;
        } else {
            lx->tok = TOK_GT;
        }
    } else {
        /* M287/D1: unknown char must hard-fail (already advanced once). */
        lx->tok = TOK_UNKNOWN;
    }
}
