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
    STATE_DASH,         // For '-' and '-=' (and negative numbers)
    STATE_STRING,
    STATE_COMMENT_POTENTIAL_START, // For the first '*' of a '**' comment
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
        case TOKEN_NUMBER: return "NumberKeyword";
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
        if (num_tokens_out) *num_tokens_out = 0;
        return NULL;
    }

    int tokencount = 0;
    Token token;

    // Ensure file pointer is at the beginning
    rewind(inputFile);

    // Re-initialize lexer context after rewind, to ensure consistent state
    init_lexer(&ctx, inputFile, input_filename);


    do {
        // Grow array if capacity is reached
        if (tokencount >= capacity) {
            capacity *= 2;
            Token* new_tokens = (Token*)realloc(tokens, capacity * sizeof(Token));
            if (!new_tokens) {
                fprintf(stderr, "Memory re-allocation failed for tokens array.\n");
                free(tokens); // Free original array if realloc fails
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

    if (token.type == TOKEN_ERROR) {
        if (num_tokens_out) *num_tokens_out = tokencount;
        free_lex_context(&ctx);
        return tokens;
    }

    // Shrink to fit actual token count (optional, but good practice)
    Token* final_tokens = (Token*)realloc(tokens, tokencount * sizeof(Token));
    if (!final_tokens) {
        fprintf(stderr, "Final re-allocation failed, returning original array.\n");
        if (num_tokens_out) *num_tokens_out = tokencount;
        free_lex_context(&ctx);
        return tokens;
    }
    tokens = final_tokens;

    if (num_tokens_out) *num_tokens_out = tokencount;
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
    ctx->location.column = 0;

    ctx->location.filename = filename;

    memset(ctx->symbol_table, 0, sizeof(ctx->symbol_table));

    // Setup the FSM transition table
    setup_transition_table(ctx);

    // Add keywords to the symbol table
    add_to_symbol_table(ctx, "write", TOKEN_WRITE, true);
    add_to_symbol_table(ctx, "and", TOKEN_AND, true);
    add_to_symbol_table(ctx, "repeat", TOKEN_REPEAT, true);
    add_to_symbol_table(ctx, "newline", TOKEN_NEWLINE, true);
    add_to_symbol_table(ctx, "times", TOKEN_TIMES, true);
    add_to_symbol_table(ctx, "number", TOKEN_NUMBER, true);

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
    ctx->transition_table[STATE_START][CHAR_STAR] = STATE_FINAL; // A single '*' -> TOKEN_STAR
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

    // Transitions for DASH state (-, -=, negative numbers)
    ctx->transition_table[STATE_DASH][CHAR_EQUALS] = STATE_FINAL; // -=
    ctx->transition_table[STATE_DASH][CHAR_DIGIT] = STATE_INTEGER; // - followed by a digit is a negative number

    // Transitions for STRING state
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_QUOTE && j != CHAR_EOF) {
            ctx->transition_table[STATE_STRING][j] = STATE_STRING;
        }
    }
    // CHAR_QUOTE and CHAR_EOF are handled procedurally in get_next_token for strings

    // All states transition to EOF on CHAR_EOF unless it's the start state directly to EOF
    for (int i = 0; i < NUM_STATES; i++) {
        if (i != STATE_START) { // EOF from START is handled explicitly
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
    if (ctx->buffer_pos >= ctx->buffer_size) {
        ctx->buffer_size = fread(ctx->buffer, 1, sizeof(ctx->buffer), ctx->input);
        ctx->buffer_pos = 0;
        if (ctx->buffer_size == 0) {
            return EOF;
        }
    }

    int c = ctx->buffer[ctx->buffer_pos++];

    if (c == '\n') {
        ctx->location.line++;
        ctx->location.column = 0;
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
            // This column adjustment is approximate, as we don't know the length of the previous line.
            // For precise column tracking after unget, one would need to store previous line lengths.
            // For now, setting to 0 or 1 is a reasonable fallback.
            ctx->location.column = 0;
        } else {
            ctx->location.column--;
        }
    } else {
        // This indicates an attempt to unget beyond the current buffer or input start.
        // It's a critical error in lexer logic.
        fprintf(stderr, "Lexer error: Attempted to unget character past buffer start.\n");
        exit(EXIT_FAILURE); // Crash for unrecoverable state
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
        ctx->symbol_table[ctx->symbol_count].name = strdup(name);
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

    // Outer loop to find the start of a new token, skipping whitespace and comments
    while (true) {
        token_start_location = ctx->location; // Mark start of potential token
        CharClass char_class = get_char_class(ctx->current_char);

        // Handle whitespace
        if (char_class == CHAR_WHITESPACE) {
            ctx->current_char = next_char(ctx);
            continue;
        }
        // Handle multi-line comments starting with "**"
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
                // It was a single '*' not followed by another '*', so it will be TOKEN_STAR.
                // Put the temp_char back and let the FSM handle the single '*'.
                unget_char(ctx);
                break; // Break from while loop to process the '*' in the FSM
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
        State next_state = ctx->transition_table[state][current_char_class];

        // --- Special Handling for STRING state ---
        // Strings consume all characters until a closing quote or EOF
        if (state == STATE_STRING) {
            if (current_char_class == CHAR_QUOTE) {
                // End of string: Consume the quote, add to lexeme, then break.
                if (ctx->lexeme_length < MAX_LEXEME_LENGTH - 1) {
                    ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
                }
                ctx->current_char = next_char(ctx); // Consume the closing quote
                state = STATE_FINAL; // String token is complete
                break; // Exit FSM loop
            } else if (current_char_class == CHAR_EOF) {
                // Unterminated string: Report error and return TOKEN_ERROR.
                report_error(ctx, "Unterminated string literal at EOF.");
                token.type = TOKEN_ERROR;
                strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
                token.lexeme[MAX_LEXEME_LENGTH - 1] = '\0';
                return token;
            }
            // Consume and add character to string lexeme if not a quote or EOF
            if (ctx->lexeme_length < MAX_LEXEME_LENGTH - 1) {
                ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
            } else {
                report_error(ctx, "Lexeme too long (string literal).");
                token.type = TOKEN_ERROR;
                strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
                token.lexeme[MAX_LEXEME_LENGTH - 1] = '\0';
                return token;
            }
            ctx->current_char = next_char(ctx); // Consume string character
            continue; // Continue building the string token
        }

        // --- General FSM Transition Logic ---
        // If the FSM transitions to a valid, non-final state (i.e., we need to keep building the token)
        // OR it's a specific multi-char operator transition to a final state
        if (next_state != STATE_ERROR &&
            (next_state == state || state == STATE_START || // Continuing in same state, or starting multi-char token
             (state == STATE_PLUS && next_state == STATE_FINAL) || // For +=
             (state == STATE_COLON && next_state == STATE_FINAL) || // For :=
             (state == STATE_DASH && next_state == STATE_FINAL) || // For -=
             (state == STATE_DASH && next_state == STATE_INTEGER) )) { // For negative numbers

            // Check for lexeme buffer overflow BEFORE adding character
            if (ctx->lexeme_length >= MAX_LEXEME_LENGTH - 1) {
                report_error(ctx, "Lexeme too long.");
                token.type = TOKEN_ERROR;
                strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
                token.lexeme[MAX_LEXEME_LENGTH - 1] = '\0';
                return token; // Return error token immediately
            }

            // Add current_char to lexeme buffer and update state
            ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
            state = next_state; // Transition to the next state
            ctx->current_char = next_char(ctx); // Consume character
        } else {
            // The current character *terminates* the token being built,
            // or causes an error, or is part of a single-character token
            // that was handled by the outer loop (whitespace/comments) or `STATE_START` direct transitions.
            break; // Token is complete, or error, exit FSM loop
        }
    } // End of FSM loop

    // Post-FSM-loop processing: The token is now complete (or error).
    // `state` holds the state *before* `current_char` terminated the token,
    // or the final state after consuming the last character of a multi-char token.

    // Null-terminate the lexeme string
    ctx->lexeme_buffer[ctx->lexeme_length] = '\0';
    strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
    token.lexeme[MAX_LEXEME_LENGTH - 1] = '\0'; // Ensure null-termination

    switch (state) {
        case STATE_IDENTIFIER: {
            unget_char(ctx); // Current char belongs to the next token, put it back
            ctx->current_char = next_char(ctx); // Re-read for next iteration to ensure `current_char` is fresh
            int sym_idx = lookup_symbol(ctx, ctx->lexeme_buffer);
            if (sym_idx != -1 && ctx->symbol_table[sym_idx].is_keyword) {
                token.type = ctx->symbol_table[sym_idx].type; // Matched a keyword
            } else {
                token.type = TOKEN_IDENTIFIER; // It's an identifier
                if (sym_idx == -1) { // Add new identifiers to symbol table
                    sym_idx = add_to_symbol_table(ctx, ctx->lexeme_buffer, TOKEN_IDENTIFIER, false);
                }
                token.value.symbol_index = sym_idx;
            }
            break;
        }
        case STATE_INTEGER: {
            unget_char(ctx); // Current char belongs to the next token, put it back
            ctx->current_char = next_char(ctx); // Re-read for next iteration
            token.type = TOKEN_INTEGER;
            token.value.int_value = atoll(ctx->lexeme_buffer); // Convert lexeme to long long
            break;
        }
        case STATE_PLUS: // Single '+' token (e.g., '+ ', not '+=')
            token.type = TOKEN_PLUS;
            unget_char(ctx); // Current char belongs to the next token
            ctx->current_char = next_char(ctx); // Re-read for next iteration
            break;
        case STATE_COLON: // Single ':' token (error in this grammar, expects ':=')
            token.type = TOKEN_ERROR;
            report_error(ctx, "Unexpected standalone colon. Expected ':='.");
            unget_char(ctx); // Current char belongs to the next token
            ctx->current_char = next_char(ctx); // Re-read for next iteration
            break;
        case STATE_DASH: // Single '-' token (error in this grammar, expects '-=' or negative number)
            token.type = TOKEN_ERROR;
            report_error(ctx, "Unexpected standalone dash. Expected '-=' or start of negative number.");
            unget_char(ctx); // Current char belongs to the next token
            ctx->current_char = next_char(ctx); // Re-read for next iteration
            break;
        case STATE_FINAL: // Multi-character tokens (:=, +=, -=) or completed string
            if (strcmp(ctx->lexeme_buffer, ":=") == 0) {
                token.type = TOKEN_ASSIGN;
            } else if (strcmp(ctx->lexeme_buffer, "+=") == 0) {
                token.type = TOKEN_PLUS_ASSIGN;
            } else if (strcmp(ctx->lexeme_buffer, "-=") == 0) {
                token.type = TOKEN_MINUS_ASSIGN;
            } else if (ctx->lexeme_buffer[0] == '"' && ctx->lexeme_buffer[ctx->lexeme_length-1] == '"') {
                token.type = TOKEN_STRING;
            }
            // Add other single-character tokens that transitioned to STATE_FINAL directly from STATE_START
            else if (strcmp(ctx->lexeme_buffer, "{") == 0) token.type = TOKEN_OPENB;
            else if (strcmp(ctx->lexeme_buffer, "}") == 0) token.type = TOKEN_CLOSEB;
            else if (strcmp(ctx->lexeme_buffer, "(") == 0) token.type = TOKEN_LPAREN;
            else if (strcmp(ctx->lexeme_buffer, ")") == 0) token.type = TOKEN_RPAREN;
            else if (strcmp(ctx->lexeme_buffer, "*") == 0) token.type = TOKEN_STAR;
            else {
                token.type = TOKEN_ERROR;
                report_error(ctx, "Unknown token identified from STATE_FINAL.");
            }
            // `ctx->current_char` is already correctly advanced beyond this token.
            break;
        case STATE_EOL: // Semicolon token (;)
            token.type = TOKEN_EOL;
            // `ctx->current_char` is already correctly advanced beyond this token.
            break;
        case STATE_EOF: // End of file token (should be caught by outer loop, but for safety)
            token.type = TOKEN_EOF;
            // `ctx->current_char` is already EOF.
            break;
        default:
            // This catches any unexpected state or character combination that doesn't fit a known token pattern.
            token.type = TOKEN_ERROR;
            report_error(ctx, "Unhandled lexer state transition or unexpected character sequence.");
            // Consume the problematic character to avoid an infinite loop
            ctx->current_char = next_char(ctx);
            break;
    }
    return token;
}
