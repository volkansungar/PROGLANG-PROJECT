#ifndef LEXER_H
#define LEXER_H
#include <stdio.h>
#include "bigint.h"
#include <stdbool.h> // Include for bool type

// Maximum lexeme length
#define MAX_LEXEME_LENGTH 256
// Max length for the string representation of an integer (for BigInt conversion)
#define MAX_INT_LENGTH MAX_BIGINT_STRING_LEN
#define MAX_VAR_LENGTH 20 // Max length for identifier names
#define MAX_KEYWORDS 6    // Maximum number of keywords
#define SYMBOL_TABLE_SIZE 1024 // Maximum size of symbol table


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
    TOKEN_LPAREN,       // (
    TOKEN_RPAREN,       // )
    TOKEN_ERROR,
    NUM_TOKEN_TYPES // Keep this last, represents the total number of distinct token types
} TokenType;

// The location of characters (for error handling and token location)
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
        BigInt big_int_value; // Changed name for consistency with lexer.c
        int symbol_index;
    } value;
} Token;

// Symbol table entry
typedef struct SymbolEntry {
    char* name;         // Dynamically allocated string for the symbol's name
    TokenType type;     // The token type associated with the symbol (e.g., TOKEN_IDENTIFIER, TOKEN_AND)
    bool is_keyword;    // True if this symbol is a keyword
} SymbolEntry;

// State types for the Finite State Machine (FSM)
typedef enum State {
    STATE_START = 0,      // Initial state, looking for a new token
    STATE_IDENTIFIER,     // Parsing an identifier or keyword
    STATE_INTEGER,        // Parsing an integer literal
    STATE_COLON,          // Special state for ':' to distinguish ':=', but not just ':'
    STATE_PLUS,           // Special state for '+' to distinguish '+='
    STATE_DASH,           // Special state for '-' to distinguish '-=' or negative numbers
    STATE_STRING,         // Parsing a string literal
    STATE_COMMENT,        // Parsing a comment (starts with '*')
    STATE_ERROR,          // Error state
    STATE_FINAL,          // State indicating a complete token has been recognized (and char should be ungot)
    STATE_EOL_CHAR,       // Intermediate state for End Of Line character (';')
    STATE_EOF_CHAR,         // End Of File state
    STATE_RETURN,         // Intermediate state to unget character and return token

    NUM_STATES            // Total number of states
} State;

// Character types for FSM transitions
typedef enum CharClass {
    CHAR_ALPHA = 0,   // a-z, A-Z
    CHAR_DIGIT,       // 0-9
    CHAR_UNDERSCORE,  // _
    CHAR_COLON,       // :
    CHAR_PLUS,        // +
    CHAR_DASH,        // -
    CHAR_EQUALS,      // =
    CHAR_QUOTE,       // "
    CHAR_STAR,        // *
    CHAR_WHITESPACE,  // space, tab, newline, etc.
    CHAR_EOL_SEMICOLON, // ;
    CHAR_OPENB_CURLY, // {
    CHAR_CLOSEB_CURLY,// }
    CHAR_LPAREN_ROUND, // (
    CHAR_RPAREN_ROUND, // )
    CHAR_OTHER,       // Any other character not specifically handled
    CHAR_EOF,         // End of file

    NUM_CHAR_CLASSES  // Total number of character classes
} CharClass;


// Lexical analyzer context structure
typedef struct {
    FILE* input;          // Input file pointer
    char buffer[4096];    // Input buffer for efficient character reading
    int buffer_pos;       // Current position in the buffer
    int buffer_size;      // Number of valid characters in the buffer

    int current_char;     // The current character being processed
    SourceLocation location; // Current line, column, and filename for error reporting

    char lexeme_buffer[MAX_LEXEME_LENGTH]; // Buffer to build the current token's lexeme
    int lexeme_length;    // Current length of the lexeme in the buffer

    SymbolEntry symbol_table[SYMBOL_TABLE_SIZE]; // Stores identifiers and keywords
    int symbol_count;     // Number of entries in the symbol table

    char* keywords[MAX_KEYWORDS]; // Array to hold pointers to keyword strings
    int keyword_count;    // Number of keywords registered

    // Transition table for the FSM: [current_state][char_class] -> next_state
    State transition_table[NUM_STATES][NUM_CHAR_CLASSES];

    char error_msg[256]; // Buffer for error messages
} LexContext;


// Function declarations for the lexer
Token* lexer(FILE* inputFile, char* input_filename, int* num_tokens_out);
void print_token(Token token);
// Function to get the string representation of a token type
const char* token_type_str(TokenType type);
// Function to free resources allocated by the lexer context
void free_lex_context(LexContext* ctx); // Changed parameter type to LexContext*

// Function prototypes (moved from lexer.c)
void init_lexer(LexContext* ctx, FILE* input, const char* filename);
void setup_transition_table(LexContext* ctx);
void add_keyword(LexContext* ctx, const char* keyword, TokenType type);
CharClass get_char_class(int c);
Token get_next_token(LexContext* ctx);
int next_char(LexContext* ctx);
void unget_char(LexContext* ctx);
int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword);
int lookup_symbol(LexContext* ctx, const char* name);
void report_error(LexContext* ctx, const char* message);

#endif // LEXER_H
