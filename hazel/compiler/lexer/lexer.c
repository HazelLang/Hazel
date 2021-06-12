/*
_ _    _           ______   _______        
| |  | |    /\    /___  /   |  ____|| |    
| |__| |   /  \      / /    | |__   | |       Hazel - The Fast, Expressive * Elegant Programming Language
|  __  |  / /\ \    / /     |  __|  | |       Languages: C, C++, and Assembly
| |  | | / ____ \  / /___   | |____ | |____   https://github.com/HazelLang/hazel/
|_|_ |_|/_/    \_\/_______\ |______|_\______|

Licensed under the MIT License <http://opensource.org/licenses/MIT>
SPDX-License-Identifier: MIT
Copyright (c) 2021 Jason Dsouza <http://github.com/jasmcaus>
*/

#include <hazel/compiler/lexer/lexer.h>
#include <hazel/core/debug.h>
#include <hazel/core/string.h>
#include <stdarg.h>

// These macros are used in the switch() statements below during the Lexing of Hazel source files.
#define WHITESPACE_NO_NEWLINE \
    ' ': case '\r': case '\t'

#define DIGIT_NON_ZERO  \
    '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9'

#define HEX_DIGIT \
    'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': \
    case '0': case DIGIT_NON_ZERO

#define ALPHA \
         'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': \
    case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': \
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z': case 'A': case 'B': case 'C': case 'D': \
    case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': \
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': \
    case 'Y': case 'Z'                                                                                  \


Lexer* lexer_init(const char* buffer) {
    Lexer* lexer = (Lexer*)calloc(1, sizeof(Lexer));
    lexer->buffer = buffer; 
    lexer->buffer_capacity = strlen(buffer);
    lexer->offset = 0;

    // Tokens
    lexer->tokenList = (Token*)calloc(TOKENLIST_ALLOC_SIZE, sizeof(Token));
    lexer->tokenList_cap = TOKENLIST_ALLOC_SIZE;
    lexer->num_tokens = 0;

    // Location
    lexer->lineno = 1;
    lexer->colno = 1;
    lexer->fname = "";
    lexer->is_inside_str = false;
    return lexer;
}

static void lexer_tokenlist_append(Lexer* lexer, Token* token) {
    if(lexer->num_tokens >= lexer->tokenList_cap) {
        lexer->tokenList = (Token*)realloc(lexer->tokenList, sizeof(Token) * (2 * TOKENLIST_ALLOC_SIZE));
        CSTL_CHECK_NULL(lexer->tokenList, "Cannot realloc more memory. Memory full.");
        lexer->tokenList_cap += TOKENLIST_ALLOC_SIZE;
    }
    lexer->tokenList[lexer->num_tokens++] = *token;
}

static void lexer_free(Lexer* lexer) {
    free(lexer);
}

// Report an error and exit
void lexer_error(Lexer* lexer, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    vfprintf(stderr, format, vl);
    fprintf(stderr, "\n");
    va_end(vl);
    exit(1);
}

static void lexer_print_stats(Lexer* lexer) {
    printf("    Char: %c\n", lexer->buffer[lexer->offset]);
    printf("    Offset: %d\n", lexer->offset);
    printf("    Lineno: %d\n", lexer->lineno);
    printf("    Colno: %d\n", lexer->colno);
}

// Returns the curent character in the Lexical Buffer and advances to the next element
// It does this by incrementing the buffer offset.
static inline char lexer_advance(Lexer* lexer) {
    if(lexer->offset >= lexer->buffer_capacity) {
        return nullchar; 
    } else {
        LEXER_INCREMENT_COLNO;
        return (char)lexer->buffer[lexer->offset++];
    }
}

// Advance `n` characters in the Lexical Buffer
static inline char lexer_advancen(Lexer* lexer, UInt32 n) {
    // The '>=' is here because offset and buffer_capacity are off by 1 (0-index vs 1-index respectively)
    if(lexer->offset + n >= lexer->buffer_capacity) {
        return nullchar;
    } else {
        lexer->colno += n;
        lexer->offset += n;
        return (char)lexer->buffer[lexer->offset];
    }
}

// Returns the previous `n` elements in the Lexical buffer.
// This is non-destructive -- the buffer offset is not updated.
static inline char lexer_prev(Lexer* lexer, UInt32 n) {
    if(lexer->offset == 0) {
        return nullchar;
    } else {
        return (char)lexer->buffer[lexer->offset-1];
    }
}

// Returns the current element in the Lexical Buffer.
static inline char lexer_peek(Lexer* lexer) {
    return (char)lexer->buffer[lexer->offset];
}

// "Look ahead" `n` characters in the Lexical buffer.
// It _does not_ increment the buffer offset.
static inline char lexer_peekn(Lexer* lexer, UInt32 n) {
    if(lexer->offset + n >= lexer->buffer_capacity) {
        return nullchar;
    } else {
        return (char)lexer->buffer[lexer->offset + n];
    }
}

static inline void lexer_maketoken(Lexer* lexer, TokenKind kind) {  
    Token* token = (Token*)calloc(1, sizeof(Token));
    CSTL_CHECK_NOT_NULL(token, "Could not allocate memory. Memory full.");

    if(kind == STRING)
        CSTL_WARN("For string tokens, use `lexer_makestrtoken instead\n");
    token->kind = kind;
    token->offset = lexer->offset;
    token->colno = lexer->colno;
    token->lineno = lexer->lineno;
    token->fname = lexer->fname;
    token->value = token_toString(kind);
    lexer_tokenlist_append(lexer, token);
}

// Make a String token 
static inline void lexer_makestrtoken(Lexer* lexer, char* str_value) {
    Token* token = (Token*)calloc(1, sizeof(Token));
    CSTL_CHECK_NOT_NULL(token, "Could not allocate memory. Memory full.");
    token->kind = STRING;
    token->offset = lexer->offset;
    token->colno = lexer->colno;
    token->lineno = lexer->lineno;
    token->fname = lexer->fname;
    token->value = str_value;
    lexer_tokenlist_append(lexer, token);
    // free(str_value);
}

// Make an Identifier Token
static void lexer_makeidenttoken(Lexer* lexer, char* ident_value) {
    Token* token = (Token*)calloc(1, sizeof(Token));
    CSTL_CHECK_NOT_NULL(token, "Could not allocate memory. Memory full.");
    token->kind = IDENTIFIER;
    token->offset = lexer->offset;
    token->colno = lexer->colno;
    token->lineno = lexer->lineno;
    token->fname = lexer->fname;
    token->value = ident_value;
    lexer_tokenlist_append(lexer, token);
    // free(ident_value);
}

// Scan a comment (single line)
static inline void lexer_lex_sl_comment(Lexer* lexer) {
    char ch = lexer_advance(lexer);
    while(ch && ch != '\n') {
        ch = lexer_advance(lexer);
    }
}

// Scan a comment (multi-line)
static inline void lexer_lex_ml_comment(Lexer* lexer) {
    char ch = lexer_advance(lexer);
    bool asterisk_found = false; 
    while(ch && !(ch == '/' && asterisk_found)) {
        asterisk_found = false; 
        while(ch && ch!= '*')
            ch = lexer_advance(lexer);
        
        if(ch == '*')
            asterisk_found = true;

        ch = lexer_advance(lexer);
    }
    ch = lexer_advance(lexer);
}

// Scan a character
static inline void lexer_lex_char(Lexer* lexer) {
    char ch = lexer_advance(lexer);
    if(ch) {
        LEXER_INCREMENT_OFFSET;
        if(ch == '\n') {
            LEXER_INCREMENT_LINENO;
            LEXER_RESET_COLNO;
        }
    }
    // return LEXER_CURR_CHAR;
}

// Scan an escape char
static inline void lexer_lex_esc_char(Lexer* lexer) {
}

// Scan a string
static inline void lexer_lex_string(Lexer* lexer) {
    // We already know that the curr char is _not_ a quote (`"`) --> an empty string token (`""`) is
    // handled by `lexer_lex()`
    CSTL_CHECK_NE(LEXER_CURR_CHAR, '"');

    char ch = lexer_advance(lexer);
    char* str_value = (char*)calloc(MAX_TOKEN_SIZE, sizeof(char));
    int prev_offset = lexer->offset - 1;

    while(ch != '"') {
        if(ch == '\\') {
            lexer_lex_esc_char(lexer); 
        } else {
            ch = lexer_advance(lexer);
        }
    }
    printf("Current char = `%c`\n", ch);
    CSTL_CHECK_EQ(ch, '"');
    int offset_diff = lexer->offset - prev_offset;
    // offset_diff should never be 0 because we already handle the `""` case in `lexer_lex()`
    CSTL_CHECK_NE(offset_diff, 0);
    // `offset_diff - 1` so as to ignore the closing quote `"` from being copied into `str_value`
    substr(str_value, lexer->buffer, prev_offset, offset_diff - 1);
    lexer_makestrtoken(lexer, str_value);
}

// Scan an identifier
static inline void lexer_lex_identifier(Lexer* lexer) {
    // When this function is called, we alread know that the first character statisfies the `case ALPHA`
    // in `lexer_lex()`, we need to substring from the previous char as well.
    char* ident = (char*)calloc(MAX_TOKEN_SIZE, sizeof(char));
    char ch = lexer_advance(lexer);
    int prev_offset = lexer->offset - 1;

    while(isLetter(ch) || isDigit(ch)) {
        ch = lexer_advance(lexer);
    }
    int offset_diff = lexer->offset - prev_offset;
    CSTL_CHECK_NE(offset_diff, 0);
    substr(ident, lexer->buffer, prev_offset - 1, offset_diff);
    lexer_makeidenttoken(lexer, ident);
}

static inline void lexer_lex_digit(Lexer* lexer) {
    char ch = LEXER_CURR_CHAR;
    int prev_offset = lexer->offset;

    while(isDigit(ch)) {
        ch = lexer_advance(lexer);
    }

    // Integer for now
    // (TODO) Add more base types
    return lexer_maketoken(lexer, INTEGER);
}

// Lex the Source files
static void lexer_lex(Lexer* lexer) {
    // Some UTF8 text may start with a 3-byte 'BOM' marker sequence. If it exists, skip over them because they 
    // are useless bytes. Generally, it is not recommended to add BOM markers to UTF8 texts, but it's not 
    // uncommon (especially on Windows).
    if(lexer->buffer[0] == (char)0xef && lexer->buffer[1] == (char)0xbb && lexer->buffer[2] == (char)0xbf) {
        lexer_advancen(lexer, 3);
    }

    char next = nullchar;
    char curr = nullchar;
    TokenKind tokenkind = TOK_ILLEGAL;

    // NB: The fact that `lexer->offset > lexer->buffer_capacity` is handled by the first case block (nullchar).
    // This is faster (performance-wise) than repeatedly checking for the above condition.
    while(true) { 
        // `lexer_advance()` returns the current character and moves forward, and `lexer_peek()` returns the current
        // character (after the advance).
        // For example, if we start from buff[0], 
        //      curr = buff[0]
        //      next = buff[1]
        curr = lexer_advance(lexer);
        next = lexer_peek(lexer);
        tokenkind = TOK_ILLEGAL;

        switch(curr) {
            case nullchar: goto lex_eof;
            // The `-1` is there to prevent an ILLEGAL token kind from being appended to `lexer->tokenList`
            // NB: Whitespace as a token is useless for our case (will this change later?)
            // case WHITESPACE_NO_NEWLINE: LEXER_INCREMENT_OFFSET; tokenkind = -1; break;
            case WHITESPACE_NO_NEWLINE: tokenkind = -1; break;
            case '\n':
                // LEXER_INCREMENT_OFFSET_ONLY;
                LEXER_INCREMENT_LINENO;
                LEXER_RESET_COLNO;
                tokenkind = -1;
                break;
            // Identifier
            case ALPHA: case '_': tokenkind = -1; lexer_lex_identifier(lexer); break;
            // Do _NOT_ include '0' because that clashes with `nullchar`
            case DIGIT_NON_ZERO: tokenkind = -1; lexer_lex_digit(lexer); break;
            case '"': 
                switch(next) {
                    // Empty String literal 
                    case '"': LEXER_INCREMENT_OFFSET; lexer_makestrtoken(lexer, "\"\""); break;
                    default: tokenkind = -1; lexer_lex_string(lexer); break;
                }
                break;
            case ';': tokenkind = SEMICOLON; break;
            case ',': tokenkind = COMMA; break;
            case '\\': tokenkind = BACKSLASH; break;
            case '[': tokenkind = LSQUAREBRACK; break;
            case ']': tokenkind = RSQUAREBRACK; break;
            case '{': tokenkind = LBRACE; break;
            case '}': tokenkind = RBRACE; break;
            case '(': tokenkind = LPAREN; break;
            case ')': tokenkind = RPAREN; break;
            
            case '=':
                switch(next) {
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = EQUALS_EQUALS; break;
                    case '>': LEXER_INCREMENT_OFFSET; tokenkind = EQUALS_ARROW; break;
                    default: tokenkind = EQUALS; break;
                }
                break;
            case '+':
                switch(next) {
                    // This might be removed at some point. 
                    // '++' serves no purpose since Hazel doesn't (and won't) support pointer arithmetic.
                    case '+': LEXER_INCREMENT_OFFSET; tokenkind = PLUS_PLUS; break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind  = PLUS_EQUALS; break;
                    default: tokenkind = PLUS; break;
                }
                break;
            case '-':
                switch(next) {
                    // This might be removed at some point. 
                    // '--' serves no purpose since Hazel doesn't (and won't) support pointer arithmetic.
                    case '-': LEXER_INCREMENT_OFFSET; tokenkind = MINUS_MINUS; break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = MINUS_EQUALS; break;
                    case '>': LEXER_INCREMENT_OFFSET; tokenkind = RARROW; break;
                    default: tokenkind = MINUS; break;
                } 
                break;
            case '*':
                switch(next) {
                    case '*': LEXER_INCREMENT_OFFSET; tokenkind = MULT_MULT; break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = MULT_EQUALS; break;
                    default: tokenkind = MULT; break;
                }
                break;
            case '/':
                switch(next) {
                    // Add tokenkind here? 
                    // (TODO) jasmcaus
                    case '/': tokenkind = -1; lexer_lex_sl_comment(lexer); break;
                    case '*': tokenkind = -1; lexer_lex_ml_comment(lexer); break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = SLASH_EQUALS; break;
                    default: tokenkind = SLASH; break;
                }
                break;
            case '#': LEXER_INCREMENT_OFFSET; tokenkind = HASH_SIGN; break;
            case '!':
                switch(next) {
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = EXCLAMATION_EQUALS; break;
                    default: tokenkind = MINUS_MINUS; break;
                }
                break;
            case '%':
                switch(next) {
                    case '%': LEXER_INCREMENT_OFFSET; tokenkind = MOD_MOD; break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = MOD_EQUALS; break;
                    default: tokenkind = MOD; break;
                }
                break;
            case '&':
                switch(next) {
                    case '&': LEXER_INCREMENT_OFFSET; tokenkind = AND_AND; break;
                    case '^': LEXER_INCREMENT_OFFSET; tokenkind = AND_NOT; break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = AND_EQUALS; break;
                    default: tokenkind = AND; break;
                }
                break;
            case '|':
                switch(next) {
                    case '|': LEXER_INCREMENT_OFFSET; tokenkind = OR_OR; break;
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = OR_EQUALS; break;
                    default: tokenkind = OR; break;
                }
                break;
            case '^':
                switch(next) {
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = XOR_EQUALS; break;
                    default: tokenkind = XOR; break;
                }
                break;
            case '<':
                switch(next) {
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = LESS_THAN_OR_EQUAL_TO; break;
                    case '-': LEXER_INCREMENT_OFFSET; tokenkind = LARROW; break;
                    case '<': LEXER_INCREMENT_OFFSET;
                        char c = lexer_peek(lexer);
                        if(c == '=') {
                            LEXER_INCREMENT_OFFSET; tokenkind = LBITSHIFT_EQUALS;
                        } else {
                            tokenkind = LBITSHIFT;
                        }
                        break;
                    default: tokenkind = LESS_THAN; break;
                }
                break;
            case '>':
                switch(next) {
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = GREATER_THAN_OR_EQUAL_TO; break;
                    case '>': LEXER_INCREMENT_OFFSET;
                        char c = lexer_peek(lexer);
                        if(c == '=') {
                            LEXER_INCREMENT_OFFSET; tokenkind = RBITSHIFT_EQUALS;
                        } else {
                            tokenkind = RBITSHIFT;
                        }
                        break;
                    default: tokenkind = GREATER_THAN; break;
                }
                break;
            case '~':
                switch(next) {
                    case '=': LEXER_INCREMENT_OFFSET; tokenkind = TILDA_EQUALS; break;
                    default: tokenkind = TILDA; break;
                }
                break;
            case '.':
                switch(next) {
                    case '.': LEXER_INCREMENT_OFFSET;
                        char c = lexer_peek(lexer);
                        if(c == '.') {
                            LEXER_INCREMENT_OFFSET; tokenkind = ELLIPSIS;
                        } else {
                            tokenkind = DDOT;
                        }
                        break;
                    default: tokenkind = DOT; break;
                }
                break;
            case ':':
                switch(next) {
                    case ':': LEXER_INCREMENT_OFFSET; tokenkind = COLON_COLON; break;
                    default: tokenkind = COLON; break;
                }
                break;
            case '?': tokenkind = QUESTION; break;
            case '@': tokenkind = MACRO; break;
            default:
                fprintf(stderr, "SyntaxError: Invalid character `%c` at %s:%d:%d\n", curr, lexer->fname, 
                                                                                           lexer->lineno,
                                                                                           lexer->colno);
                break;
        } // switch(ch)

        if(tokenkind == -1) continue;
        lexer_maketoken(lexer, tokenkind);
    } // while

lex_eof:;

    lexer_maketoken(lexer, TOK_EOF);
}