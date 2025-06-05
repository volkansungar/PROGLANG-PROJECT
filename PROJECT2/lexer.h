#ifndef LEXER_H
#define LEXER_H
#include <stdio.h>

// Maximum lexeme length
#define MAX_LEXEME_LENGTH 256
// Token types
typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_WRITE,
    TOKEN_AND,
    TOKEN_REPEAT,
    TOKEN_NEWLINE,
    TOKEN_TIMES,
    TOKEN_NUMBER,       // "number" keyword for type declaration
    TOKEN_INTEGER,      // For integer literals (e.g., 123)
    TOKEN_ASSIGN,       // :=
    TOKEN_PLUS_ASSIGN,  // +=
    TOKEN_MINUS_ASSIGN, // -=
    TOKEN_OPENB,        // {
    TOKEN_CLOSEB,       // }
    TOKEN_STRING,
    TOKEN_EOL,          // ;
    TOKEN_PLUS,         // +
    TOKEN_STAR,         // *
    TOKEN_LPAREN,       // (
    TOKEN_RPAREN,       // )
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

// Function declarations for the lexer
Token* lexer(FILE* inputFile, char* input_filename,int* num_tokens_out);
void print_token(Token token);
// Function to get the string representation of a token type
const char* token_type_str(TokenType type);
// Function to free resources allocated by the lexer context
void free_lex_context(void* ctx_ptr);

#endif // LEXER_H

