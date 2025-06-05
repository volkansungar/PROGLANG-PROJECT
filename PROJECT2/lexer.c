#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// Maximum length for integer literals
#define MAX_INT_LENGTH 20 // A 64-bit integer has max 19 digits + sign
// Maximum length for variable names
#define MAX_VAR_LENGTH 32 // Reasonable length for identifiers

// Maximum number of keywords (can be increased to add new keywords to the language)
#define MAX_KEYWORDS 7 // Updated to 7 to include "number"

// Maximum size of symbol table
#define SYMBOL_TABLE_SIZE 1024


// Symbol table entry
typedef struct {
    char* name;
    TokenType type;
    bool is_keyword;
} SymbolEntry;

// State types for the Finite State Machine (FSM)
typedef enum {
    STATE_START = 0,
    STATE_IDENTIFIER,
    STATE_INTEGER,
    STATE_PLUS,         // For '+' and '+='
    STATE_COLON,        // For ':' and ':='
    STATE_DASH,         // For '-' and '-='
    STATE_STRING,
    STATE_COMMENT,
    STATE_FINAL,        // A state where a token is recognized
    STATE_ERROR,
    STATE_EOL,          // End of line (semicolon)
    STATE_EOF,          // End of file

    NUM_STATES          // Number of states in the FSM
} State;

// Character types for the FSM input
typedef enum {
    CHAR_ALPHA = 0,   // a-z, A-Z, _
    CHAR_DIGIT,       // 0-9
    CHAR_PLUS,        // +
    CHAR_EQUALS,      // =
    CHAR_COLON,       // :
    CHAR_DASH,        // -
    CHAR_QUOTE,       // "
    CHAR_STAR,        // *
    CHAR_WHITESPACE,  // space, tab, etc.
    CHAR_EOL_SEMICOLON, // ;
    CHAR_OPENB,       // {
    CHAR_CLOSEB,      // }
    CHAR_LPAREN,      // (
    CHAR_RPAREN,      // )
    CHAR_OTHER,       // Any other character not explicitly listed
    CHAR_EOF,         // End of file

    NUM_CHAR_CLASSES  // Number of character classes
} CharClass;

// Lexical analyzer context
typedef struct {
    // Input buffer for efficient file reading
    FILE* input;
    char buffer[4096];
    int buffer_pos;
    int buffer_size;

    // Current character being processed
    int current_char;
    SourceLocation location; // Current line and column for error reporting

    // Lexeme buffer to build the current token's string
    char lexeme_buffer[MAX_LEXEME_LENGTH];
    int lexeme_length;

    // Symbol table (contains Keywords and identifiers)
    SymbolEntry symbol_table[SYMBOL_TABLE_SIZE];
    int symbol_count;

    // Transition table for the FSM
    State transition_table[NUM_STATES][NUM_CHAR_CLASSES];

    // Error message string
    char error_msg[256];
} LexContext;

// --- FUNCTION DECLARATIONS (internal to lexer.c) ---
static void init_lexer(LexContext* ctx, FILE* input, const char* filename);
static void setup_transition_table(LexContext* ctx);
static CharClass get_char_class(int c);
static int next_char(LexContext* ctx);
static void unget_char(LexContext* ctx);
static int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword);
static int lookup_symbol(LexContext* ctx, const char* name);
static void report_error(LexContext* ctx, const char* message);
static Token get_next_token(LexContext* ctx); // Main token retrieval function

// --- GLOBAL FUNCTIONS (declared in lexer.h) ---

// Frees memory allocated for the symbol table names
void free_lex_context(void* ctx_ptr) {
    LexContext* ctx = (LexContext*)ctx_ptr;
    if (ctx) {
        for (int i = 0; i < ctx->symbol_count; ++i) {
            free(ctx->symbol_table[i].name); // Free memory allocated by strdup
        }
        // No need to free ctx itself as it's typically stack-allocated
    }
}

// Converts a TokenType enum value to its string representation
const char* token_type_str(TokenType type) {
    switch (type) {
        case TOKEN_EOF: return "EndOfFile";
        case TOKEN_EOL: return "EndOfLine";
        case TOKEN_IDENTIFIER: return "Identifier";
        case TOKEN_ASSIGN: return "Assign";
        case TOKEN_INTEGER: return "IntConstant";
        case TOKEN_NUMBER: return "NumberKeyword"; // Added
        case TOKEN_TIMES: return "Times";
        case TOKEN_NEWLINE: return "Newline";
        case TOKEN_REPEAT: return "Repeat";
        case TOKEN_AND: return "And";
        case TOKEN_WRITE: return "Write";
        case TOKEN_STRING: return "StringConstant";
        case TOKEN_PLUS_ASSIGN: return "PlusAssign";
        case TOKEN_MINUS_ASSIGN: return "MinusAssign";
        case TOKEN_OPENB: return "OpenBlock";
        case TOKEN_CLOSEB: return "CloseBlock";
        case TOKEN_PLUS: return "Plus";
        case TOKEN_STAR: return "Star";
        case TOKEN_LPAREN: return "LParen";
        case TOKEN_RPAREN: return "RParen";
        case TOKEN_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Prints a token's details to stdout
void print_token(Token token) {
    printf("%-12s %-16s  Line:%-4d Col:%-4d",
           token_type_str(token.type),
           token.lexeme,
           token.location.line,
           token.location.column);

    if (token.type == TOKEN_INTEGER) {
        printf("  Value: %lld", token.value.int_value);
    }
    printf("\n");
}

// Main lexer function: reads input file and returns an array of tokens
Token* lexer(FILE* inputFile, char* input_filename, int* num_tokens_out) {
    LexContext ctx;
    init_lexer(&ctx, inputFile, input_filename);

    // Dynamically allocate and grow the tokens array
    int capacity = 100; // Initial capacity
    Token* tokens = (Token*)malloc(capacity * sizeof(Token));
    if (!tokens) {
        fprintf(stderr, "Memory allocation failed for tokens array.\n");
        // No need to call free_lex_context here as the context is stack allocated.
        // The names in symbol_table need to be freed later in main.
        if (num_tokens_out) *num_tokens_out = 0;
        return NULL;
    }

    int tokencount = 0;
    Token token;

    // Ensure file pointer is at the beginning
    rewind(inputFile);

    // Re-initialize lexer context after rewind, to ensure consistent state
    // Especially for the initial read of `current_char`
    init_lexer(&ctx, inputFile, input_filename);


    do {
        // Grow array if capacity is reached
        if (tokencount >= capacity) {
            capacity *= 2;
            Token* new_tokens = (Token*)realloc(tokens, capacity * sizeof(Token));
            if (!new_tokens) {
                fprintf(stderr, "Memory re-allocation failed for tokens array.\n");
                free(tokens); // Free original array if realloc fails
                // No need to call free_lex_context here.
                if (num_tokens_out) *num_tokens_out = 0;
                return NULL;
            }
            tokens = new_tokens;
        }

        token = get_next_token(&ctx);
        tokens[tokencount++] = token;
        print_token(token); // For debugging/output

    } while (token.type != TOKEN_EOF && token.type != TOKEN_ERROR);

    printf("Lexical analysis completed.\n");

    // If an error occurred and it's not EOF, we might want to return less tokens
    // or handle error state differently. For now, include the error token.
    if (token.type == TOKEN_ERROR) {
        if (num_tokens_out) *num_tokens_out = tokencount;
        // Call free_lex_context to cleanup internal symbol table names.
        free_lex_context(&ctx);
        return tokens; // Return tokens up to the error
    }

    // Shrink to fit actual token count (optional, but good practice)
    Token* final_tokens = (Token*)realloc(tokens, tokencount * sizeof(Token));
    if (!final_tokens) {
        fprintf(stderr, "Final re-allocation failed, returning original array.\n");
        // In a real scenario, you might want to handle this more robustly
        if (num_tokens_out) *num_tokens_out = tokencount;
        // Call free_lex_context to cleanup internal symbol table names.
        free_lex_context(&ctx);
        return tokens; // Return the potentially oversized array
    }
    tokens = final_tokens;

    if (num_tokens_out) *num_tokens_out = tokencount; // Return the actual count
    // The LexContext `ctx` is a stack variable, so its memory is automatically reclaimed.
    // However, the `name` members within `symbol_table` are heap-allocated via `strdup`
    // and need to be freed manually. This is handled by `free_lex_context` before returning.
    free_lex_context(&ctx); // Free symbol table entries
    return tokens;
}

// --- INTERNAL LEXER FUNCTIONS ---

// Initializes the LexContext structure
static void init_lexer(LexContext* ctx, FILE* input, const char* filename) {
    ctx->input = input;
    ctx->buffer_pos = 0;
    ctx->buffer_size = 0;
    ctx->lexeme_length = 0;
    ctx->symbol_count = 0;

    ctx->location.line = 1;
    ctx->location.column = 0; // Column 0 means before the first character of the line.

    // filename needs to be a persistent string, assuming it is from argv or a static literal.
    // If it were dynamic, strdup would be needed here.
    ctx->location.filename = filename;

    // Initialize symbol table memory to all zeros
    memset(ctx->symbol_table, 0, sizeof(ctx->symbol_table));

    // Setup the FSM transition table
    setup_transition_table(ctx);

    // Add keywords to the symbol table
    add_to_symbol_table(ctx, "write", TOKEN_WRITE, true);
    add_to_symbol_table(ctx, "and", TOKEN_AND, true);
    add_to_symbol_table(ctx, "repeat", TOKEN_REPEAT, true);
    add_to_symbol_table(ctx, "newline", TOKEN_NEWLINE, true);
    add_to_symbol_table(ctx, "times", TOKEN_TIMES, true);
    add_to_symbol_table(ctx, "number", TOKEN_NUMBER, true); // Re-added "number" keyword

    // Read the first character to initialize ctx->current_char
    ctx->current_char = next_char(ctx);
}

// Sets up the FSM transition table
static void setup_transition_table(LexContext* ctx) {
    // Initialize all transitions to ERROR by default
    for (int i = 0; i < NUM_STATES; i++) {
        for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
            ctx->transition_table[i][j] = STATE_ERROR;
        }
    }

    // Define transitions from STATE_START
    ctx->transition_table[STATE_START][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_START][CHAR_DIGIT] = STATE_INTEGER;
    ctx->transition_table[STATE_START][CHAR_PLUS] = STATE_PLUS;
    ctx->transition_table[STATE_START][CHAR_COLON] = STATE_COLON;
    ctx->transition_table[STATE_START][CHAR_DASH] = STATE_DASH;
    ctx->transition_table[STATE_START][CHAR_QUOTE] = STATE_STRING;
    ctx->transition_table[STATE_START][CHAR_STAR] = STATE_COMMENT; // Start of a comment (e.g., "**" style)
    ctx->transition_table[STATE_START][CHAR_OPENB] = STATE_FINAL; // {
    ctx->transition_table[STATE_START][CHAR_CLOSEB] = STATE_FINAL; // }
    ctx->transition_table[STATE_START][CHAR_LPAREN] = STATE_FINAL; // (
    ctx->transition_table[STATE_START][CHAR_RPAREN] = STATE_FINAL; // )
    ctx->transition_table[STATE_START][CHAR_WHITESPACE] = STATE_START; // Skip whitespace
    ctx->transition_table[STATE_START][CHAR_EOL_SEMICOLON] = STATE_EOL; // ;
    ctx->transition_table[STATE_START][CHAR_EOF] = STATE_EOF; // Explicit EOF transition from start

    // Transitions for IDENTIFIER state
    ctx->transition_table[STATE_IDENTIFIER][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_IDENTIFIER][CHAR_DIGIT] = STATE_IDENTIFIER;

    // Transitions for INTEGER state
    ctx->transition_table[STATE_INTEGER][CHAR_DIGIT] = STATE_INTEGER;

    // Transitions for PLUS state (+, +=)
    ctx->transition_table[STATE_PLUS][CHAR_EQUALS] = STATE_FINAL; // +=

    // Transitions for COLON state (:, :=)
    ctx->transition_table[STATE_COLON][CHAR_EQUALS] = STATE_FINAL; // :=

    // Transitions for DASH state (-, -=)
    ctx->transition_table[STATE_DASH][CHAR_EQUALS] = STATE_FINAL; // -=
    ctx->transition_table[STATE_DASH][CHAR_DIGIT] = STATE_INTEGER; // Negative numbers, transitions to INTEGER state

    // Transitions for STRING state
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_QUOTE && j != CHAR_EOF) {
            ctx->transition_table[STATE_STRING][j] = STATE_STRING;
        }
    }
    ctx->transition_table[STATE_STRING][CHAR_QUOTE] = STATE_FINAL; // End of string

    // Transitions for COMMENT state (multi-line comments starting with '*')
    // This is simplified. Assuming a comment like `** ... **`
    // From STATE_COMMENT, any char keeps it in COMMENT unless it's '*'
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_STAR && j != CHAR_EOF) {
            ctx->transition_table[STATE_COMMENT][j] = STATE_COMMENT;
        }
    }
    ctx->transition_table[STATE_COMMENT][CHAR_STAR] = STATE_COMMENT; // Stays in comment, but need to check next char

    // All states transition to EOF on CHAR_EOF
    for (int i = 0; i < NUM_STATES; i++) {
        // Only if not already defined for a specific final token type
        if (ctx->transition_table[i][CHAR_EOF] == STATE_ERROR || ctx->transition_table[i][CHAR_EOF] == STATE_START) {
            ctx->transition_table[i][CHAR_EOF] = STATE_EOF;
        }
    }
}

// Determines the character class for a given character
static CharClass get_char_class(int c) {
    if (c == EOF) return CHAR_EOF;
    if (isalpha(c) || c == '_') return CHAR_ALPHA;
    if (isdigit(c)) return CHAR_DIGIT;
    if (c == '"') return CHAR_QUOTE;
    if (c == '*') return CHAR_STAR;
    if (isspace(c)) return CHAR_WHITESPACE;
    if (c == ':') return CHAR_COLON;
    if (c == '+') return CHAR_PLUS;
    if (c == '=') return CHAR_EQUALS;
    if (c == '-') return CHAR_DASH;
    if (c == ';') return CHAR_EOL_SEMICOLON;
    if (c == '{') return CHAR_OPENB;
    if (c == '}') return CHAR_CLOSEB;
    if (c == '(') return CHAR_LPAREN;
    if (c == ')') return CHAR_RPAREN;
    return CHAR_OTHER;
}

// Reads the next character from the input stream, handling buffering
static int next_char(LexContext* ctx) {
    // If buffer is empty, read more data from file
    if (ctx->buffer_pos >= ctx->buffer_size) {
        ctx->buffer_size = fread(ctx->buffer, 1, sizeof(ctx->buffer), ctx->input);
        ctx->buffer_pos = 0;
        if (ctx->buffer_size == 0) {
            // End of file, return EOF and do not update location as no char was read.
            return EOF;
        }
    }

    // Get the next character from the buffer
    int c = ctx->buffer[ctx->buffer_pos++];

    // Update source location for error reporting
    if (c == '\n') {
        ctx->location.line++;
        ctx->location.column = 0; // Reset column for new line (column 0 is before the first char)
    } else {
        ctx->location.column++;
    }

    return c;
}

// Puts the last read character back into the input stream
static void unget_char(LexContext* ctx) {
    if (ctx->buffer_pos > 0) {
        ctx->buffer_pos--;
        // Adjust column based on the character being ungotten
        if (ctx->buffer[ctx->buffer_pos] == '\n') {
            ctx->location.line--;
            ctx->location.column = 0; // Approximate column to start of previous line
        } else {
            ctx->location.column--;
        }
    } else {
        fprintf(stderr, "Lexer error: Attempted to unget character past buffer start.\n");
    }
}

// Adds an identifier or keyword to the symbol table
static int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword) {
    // First, check if the symbol already exists
    int index = lookup_symbol(ctx, name);
    if (index != -1) {
        return index;
    }

    // If not, add it to the table
    if (ctx->symbol_count < SYMBOL_TABLE_SIZE) {
        ctx->symbol_table[ctx->symbol_count].name = strdup(name); // Allocate memory for the name
        if (!ctx->symbol_table[ctx->symbol_count].name) {
            report_error(ctx, "Memory allocation failed for symbol name.");
            return -1;
        }
        ctx->symbol_table[ctx->symbol_count].type = type;
        ctx->symbol_table[ctx->symbol_count].is_keyword = is_keyword;
        return ctx->symbol_count++;
    }

    report_error(ctx, "Symbol table overflow");
    return -1;
}

// Looks up a symbol in the symbol table
static int lookup_symbol(LexContext* ctx, const char* name) {
    for (int i = 0; i < ctx->symbol_count; i++) {
        if (strcmp(ctx->symbol_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

// Reports a lexical error to stderr
static void report_error(LexContext* ctx, const char* message) {
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "Lexical error at %s:%d:%d: %s",
             ctx->location.filename,
             ctx->location.line,
             ctx->location.column,
             message);
    fprintf(stderr, "%s\n", ctx->error_msg);
}

// Main function to get the next token from the input stream
static Token get_next_token(LexContext* ctx) {
    Token token;
    State state = STATE_START;
    SourceLocation token_start_location;

    // Loop to find the start of a new token, skipping whitespace and comments
    while (true) {
        token_start_location = ctx->location; // Mark start of potential token
        CharClass char_class = get_char_class(ctx->current_char);

        // Handle whitespace
        if (char_class == CHAR_WHITESPACE) {
            ctx->current_char = next_char(ctx);
            continue;
        }
        // Handle comments
        else if (char_class == CHAR_STAR) {
            int temp_char = next_char(ctx); // Read the second character
            if (temp_char == '*') { // Found "**" - multi-line comment start
                bool comment_closed = false;
                while (true) {
                    temp_char = next_char(ctx);
                    if (temp_char == EOF) {
                        report_error(ctx, "Unterminated comment.");
                        token.type = TOKEN_ERROR;
                        strncpy(token.lexeme, "", MAX_LEXEME_LENGTH);
                        token.location = token_start_location;
                        return token;
                    }
                    if (temp_char == '*') {
                        temp_char = next_char(ctx); // Check next char for '*'
                        if (temp_char == '*') { // Found "**" - comment end
                            comment_closed = true;
                            ctx->current_char = next_char(ctx); // Consume character AFTER comment end
                            break;
                        }
                        // If it was just one '*', stay in comment and read next char
                        ctx->current_char = temp_char;
                    } else {
                        ctx->current_char = temp_char; // Move to next char in comment
                    }
                }
                if (comment_closed) {
                    continue; // Comment ended, go back to STATE_START to find next token
                }
            } else {
                // It was a single '*' not followed by another '*', so it's TOKEN_STAR
                unget_char(ctx); // Put the temp_char back
                // Now, current_char still holds the first '*', which will be processed as a token below
                break; // Break from while loop to process the '*'
            }
        }
        // Handle EOF
        else if (char_class == CHAR_EOF) {
            token.type = TOKEN_EOF;
            strncpy(token.lexeme, "EOF", MAX_LEXEME_LENGTH);
            token.location = ctx->location;
            return token; // Immediately return EOF token
        }
        // If not whitespace, comment, or EOF, break to process the character as part of a token
        break;
    }

    // Initialize lexeme buffer for the current token
    ctx->lexeme_length = 0;
    ctx->lexeme_buffer[0] = '\0';
    token.location = token_start_location; // Token starts at the saved location
    state = STATE_START; // Reset FSM state for this new token

    // FSM loop to build the lexeme and determine token type
    while (true) {
        CharClass current_char_class = get_char_class(ctx->current_char);
        State next_state_from_table = ctx->transition_table[state][current_char_class];

        // Condition 1: Continue building the current token (character is consumed)
        // This includes transitions from START to initial states for multi-char tokens,
        // or continuing in IDENTIFIER/INTEGER/STRING states, or forming multi-char operators.
        bool should_consume_char_and_continue = false;

        // Transition from START to an initial state for multi-char tokens or continuing states
        if (state == STATE_START && (next_state_from_table == STATE_IDENTIFIER ||
                                      next_state_from_table == STATE_INTEGER ||
                                      next_state_from_table == STATE_PLUS ||
                                      next_state_from_table == STATE_COLON ||
                                      next_state_from_table == STATE_DASH ||
                                      next_state_from_table == STATE_STRING)) {
            should_consume_char_and_continue = true;
        }
        // Continuing in a multi-character token state (e.g., Identifier, Integer)
        else if ((state == STATE_IDENTIFIER || state == STATE_INTEGER) && next_state_from_table == state) {
            should_consume_char_and_continue = true;
        }
        // Forming multi-character operators (e.g., ':=', '+=', '-=')
        else if ((state == STATE_PLUS || state == STATE_COLON || state == STATE_DASH) && current_char_class == CHAR_EQUALS) {
            should_consume_char_and_continue = true;
        }
        // Inside a string (any char except quote or EOF)
        else if (state == STATE_STRING && current_char_class != CHAR_QUOTE && current_char_class != CHAR_EOF) {
            should_consume_char_and_continue = true;
        }


        if (should_consume_char_and_continue) {
            // Add current_char to lexeme buffer
            if (ctx->lexeme_length < MAX_LEXEME_LENGTH - 1) {
                ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
            } else {
                report_error(ctx, "Lexeme too long.");
                token.type = TOKEN_ERROR;
                break; // Exit loop, token found (error)
            }
            state = next_state_from_table; // Advance state
            ctx->current_char = next_char(ctx); // Consume character
        }
        else { // Condition 2: Current character ends the token, or it's a single-character token, or an error.
               // The `current_char` is the character that *terminated* the token being built,
               // or it *is* the single-character token, or it's an unexpected character.

            // Null-terminate the lexeme string before determining token type
            ctx->lexeme_buffer[ctx->lexeme_length] = '\0';
            strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
            token.lexeme[MAX_LEXEME_LENGTH - 1] = '\0'; // Ensure null-termination

            // Determine token type based on the *state we were just in* (`state` variable)
            switch (state) {
                case STATE_START:
                    // This case handles single-character tokens that directly transition from STATE_START
                    // to a final state (like '{', '}', ';', '(', ')', '+', '*').
                    // The `current_char` is the one that forms this token.
                    // It needs to be added to the lexeme_buffer here.
                    if (ctx->lexeme_length < MAX_LEXEME_LENGTH - 1) {
                         ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
                         ctx->lexeme_buffer[ctx->lexeme_length] = '\0'; // Null-terminate after adding
                    }

                    if (current_char_class == CHAR_OPENB) token.type = TOKEN_OPENB;
                    else if (current_char_class == CHAR_CLOSEB) token.type = TOKEN_CLOSEB;
                    else if (current_char_class == CHAR_LPAREN) token.type = TOKEN_LPAREN;
                    else if (current_char_class == CHAR_RPAREN) token.type = TOKEN_RPAREN;
                    else if (current_char_class == CHAR_PLUS) token.type = TOKEN_PLUS;
                    else if (current_char_class == CHAR_STAR) token.type = TOKEN_STAR;
                    else if (current_char_class == CHAR_EOL_SEMICOLON) token.type = TOKEN_EOL;
                    else if (current_char_class == CHAR_EOF) token.type = TOKEN_EOF; // Should be caught by outer loop, but for safety.
                    else {
                        token.type = TOKEN_ERROR;
                        report_error(ctx, "Unexpected single character or unhandled transition from START state.");
                    }
                    ctx->current_char = next_char(ctx); // Consume this single-char token
                    break;

                case STATE_IDENTIFIER: {
                    // An identifier token has been formed, and `current_char` is the start of the next token.
                    unget_char(ctx); // Put `current_char` back into input stream
                    int sym_idx = lookup_symbol(ctx, ctx->lexeme_buffer);
                    if (sym_idx != -1 && ctx->symbol_table[sym_idx].is_keyword) {
                        token.type = ctx->symbol_table[sym_idx].type; // Matched a keyword
                    } else {
                        token.type = TOKEN_IDENTIFIER; // It's an identifier
                        if (sym_idx == -1) {
                            sym_idx = add_to_symbol_table(ctx, ctx->lexeme_buffer, TOKEN_IDENTIFIER, false);
                        }
                        token.value.symbol_index = sym_idx;
                    }
                    break;
                }

                case STATE_INTEGER: {
                    // An integer literal has been formed, and `current_char` is the start of the next token.
                    unget_char(ctx); // Put `current_char` back into input stream
                    token.type = TOKEN_INTEGER;
                    token.value.int_value = atoll(ctx->lexeme_buffer); // Convert lexeme to long long
                    break;
                }

                case STATE_PLUS: // We were in STATE_PLUS, but the current char is NOT '=' (e.g., '+ ').
                                 // This implies it's a single '+' token.
                    token.type = TOKEN_PLUS;
                    unget_char(ctx); // Current char belongs to the next token
                    break;

                case STATE_COLON: // We were in STATE_COLON, but the current char is NOT '=' (e.g., ': ').
                                  // In this grammar, a standalone ':' is an error.
                    token.type = TOKEN_ERROR;
                    report_error(ctx, "Unexpected standalone colon. Expected ':='.");
                    unget_char(ctx); // Current char belongs to the next token
                    break;

                case STATE_DASH: // We were in STATE_DASH, but the current char is NOT '=' or a digit.
                                 // A standalone '-' is currently not a defined token type (like TOKEN_SUBTRACT), so it's an error.
                    token.type = TOKEN_ERROR;
                    report_error(ctx, "Unexpected standalone dash. Expected '-=' or start of negative number.");
                    unget_char(ctx); // Current char belongs to the next token
                    break;

                case STATE_STRING:
                    // This implies we were in STATE_STRING, but `current_char_class` was CHAR_EOF.
                    // If it was CHAR_QUOTE, it would have been handled by `should_consume_char_and_continue`.
                    if (current_char_class == CHAR_EOF) {
                         token.type = TOKEN_ERROR;
                         report_error(ctx, "Unterminated string literal at EOF.");
                         // Do not unget EOF.
                    } else {
                        token.type = TOKEN_ERROR;
                        report_error(ctx, "Internal error in string lexing logic.");
                        unget_char(ctx); // Revert last char
                    }
                    break;

                case STATE_FINAL: // This state is reached when a multi-character token (like ':=', '+=', '-=')
                                  // or a string (ending quote) has just been fully recognized.
                                  // The `current_char` is already the one *after* this complete token
                                  // because the final character of the token was consumed in `should_consume_char_and_continue`.
                    if (strcmp(ctx->lexeme_buffer, ":=") == 0) {
                        token.type = TOKEN_ASSIGN;
                    } else if (strcmp(ctx->lexeme_buffer, "+=") == 0) {
                        token.type = TOKEN_PLUS_ASSIGN;
                    } else if (strcmp(ctx->lexeme_buffer, "-=") == 0) {
                        token.type = TOKEN_MINUS_ASSIGN;
                    } else if (ctx->lexeme_buffer[0] == '"' && ctx->lexeme_buffer[ctx->lexeme_length-1] == '"') {
                        token.type = TOKEN_STRING; // String ended
                    }
                    else {
                        token.type = TOKEN_ERROR;
                        report_error(ctx, "Unknown token identified from STATE_FINAL.");
                    }
                    // `ctx->current_char` is already correctly advanced.
                    break;

                case STATE_EOL: // Semicolon token was directly from START to EOL.
                    // This is handled by the `case STATE_START`
                    token.type = TOKEN_EOL; // Should not be reached independently if `case STATE_START` is correct
                    ctx->current_char = next_char(ctx);
                    break;

                case STATE_EOF: // EOF token was directly from START to EOF.
                    token.type = TOKEN_EOF; // Should not be reached independently if outer loop is correct
                    break;

                default:
                    // This catches any unexpected state or character combination that doesn't fit a known token pattern.
                    token.type = TOKEN_ERROR;
                    report_error(ctx, "Unhandled lexer state transition or unexpected character sequence.");
                    // Consume the problematic character to avoid an infinite loop
                    ctx->current_char = next_char(ctx);
                    break;
            }
            break; // Token has been identified or an error occurred, exit lexeme building loop
        }
    }
    return token;
}
