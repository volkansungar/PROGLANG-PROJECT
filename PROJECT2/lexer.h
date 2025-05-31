#ifndef LEXER_H
#define LEXER_H
#include <stdio.h>

// Maximum lexeme length
#define MAX_LEXEME_LENGTH 256
// Token types
typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_KEYWORD,
    TOKEN_INTEGER,
    TOKEN_OPERATOR,
    TOKEN_OPENB,
    TOKEN_CLOSEB,   
    TOKEN_STRING,
    TOKEN_EOL,
    TOKEN_ERROR
} TokenType;

// the location of characters (for error handling and token location)
typedef struct {
    int line;
    int column;
    const char* filename;
} SourceLocation;

// Token structure
typedef struct {
    TokenType type;
    char lexeme[MAX_LEXEME_LENGTH];
    SourceLocation location;
    union {
        long long int_value;
        int symbol_index;
    } value;
} Token;

Token* lexer(FILE* inputFile, char* input_filename);




#endif // LEXER_H
