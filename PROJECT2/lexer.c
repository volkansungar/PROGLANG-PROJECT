#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "bigint.h"
#include "lexer.h"

// LEXER INITIALIZATION
/*****************************************************************************/
void init_lexer(LexContext* ctx, FILE* input, const char* filename) {
    ctx->input = input;
    ctx->buffer_pos = 0;
    ctx->buffer_size = 0;
    ctx->current_char = 0;
    ctx->lexeme_length = 0;
    ctx->symbol_count = 0;
    ctx->keyword_count = 0;

    ctx->location.line = 1;
    ctx->location.column = 0;
    ctx->location.filename = filename;

    // Initialize symbol table with zeros
    memset(ctx->symbol_table, 0, sizeof(ctx->symbol_table));

    // Initialize the transition table for the FSM
    setup_transition_table(ctx);

    // Add all the keywords that the language supports with their specific token types
    add_keyword(ctx, "and", TOKEN_AND);
    add_keyword(ctx, "write", TOKEN_WRITE);
    add_keyword(ctx, "repeat", TOKEN_REPEAT);
    add_keyword(ctx, "newline", TOKEN_NEWLINE);
    add_keyword(ctx, "times", TOKEN_TIMES);
    add_keyword(ctx, "number", TOKEN_NUMBER);
}

// SETUP THE TRANSITION TABLE FUNCTION (FINITE STATE MACHINE LOGIC)
/*****************************************************************************/
void setup_transition_table(LexContext* ctx) {
    // Initialize all states to STATE_RETURN by default, which means
    // "unget the current character and return the token recognized so far."
    for (int i = 0; i < NUM_STATES; i++) {
        for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
            if (j == CHAR_EOF) {
                // EOF always leads to STATE_EOF_CHAR
                ctx->transition_table[i][j] = STATE_EOF_CHAR;
            } else if (j == CHAR_OTHER) {
                // An unrecognized character leads to an error
                ctx->transition_table[i][j] = STATE_ERROR;
            } else {
                ctx->transition_table[i][j] = STATE_RETURN;
            }
        }
    }

    // Define transitions from the START state
    ctx->transition_table[STATE_START][CHAR_ALPHA] = STATE_IDENTIFIER;    // 'a' -> Identifier
    ctx->transition_table[STATE_START][CHAR_DIGIT] = STATE_INTEGER;       // '1' -> Integer
    ctx->transition_table[STATE_START][CHAR_COLON] = STATE_COLON;        // ':' -> might be ':='
    ctx->transition_table[STATE_START][CHAR_PLUS] = STATE_PLUS;          // '+' -> might be '+='
    ctx->transition_table[STATE_START][CHAR_DASH] = STATE_DASH;          // '-' -> might be '-=' or negative numbers
    ctx->transition_table[STATE_START][CHAR_QUOTE] = STATE_STRING;       // '"' -> String
    ctx->transition_table[STATE_START][CHAR_STAR] = STATE_COMMENT;       // '*' -> Comment
    ctx->transition_table[STATE_START][CHAR_OPENB_CURLY] = STATE_FINAL;    // '{' -> Open Block
    ctx->transition_table[STATE_START][CHAR_CLOSEB_CURLY] = STATE_FINAL;   // '}' -> Close Block
    ctx->transition_table[STATE_START][CHAR_LPAREN_ROUND] = STATE_FINAL; // '('
    ctx->transition_table[STATE_START][CHAR_RPAREN_ROUND] = STATE_FINAL; // ')'
    ctx->transition_table[STATE_START][CHAR_WHITESPACE] = STATE_START;   // Whitespace -> Stay in START (skip)
    ctx->transition_table[STATE_START][CHAR_EQUALS] = STATE_ERROR;       // '=' is not a valid start of a token (only as part of ':=')
    ctx->transition_table[STATE_START][CHAR_EOL_SEMICOLON] = STATE_EOL_CHAR; // ';' -> End Of Line

    // Ensure all EOF transitions explicitly lead to STATE_EOF_CHAR
    for (int i = 0; i < NUM_STATES; i++) {
        ctx->transition_table[i][CHAR_EOF] = STATE_EOF_CHAR;
    }

    // IDENTIFIER STATE TRANSITIONS: continues as long as it sees alpha, digit or underscore
    ctx->transition_table[STATE_IDENTIFIER][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_IDENTIFIER][CHAR_DIGIT] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_IDENTIFIER][CHAR_UNDERSCORE] = STATE_IDENTIFIER;

    // INTEGER STATE TRANSITIONS: continues as long as it sees digit
    ctx->transition_table[STATE_INTEGER][CHAR_DIGIT] = STATE_INTEGER;

    // COLON STATE TRANSITIONS: expects '=' for ':='
    ctx->transition_table[STATE_COLON][CHAR_EQUALS] = STATE_FINAL; // ':='

    // PLUS STATE TRANSITIONS: expects '=' for '+='
    ctx->transition_table[STATE_PLUS][CHAR_EQUALS] = STATE_FINAL; // '+='

    // DASH STATE TRANSITIONS: expects '=' for '-=' or a digit for negative number
    ctx->transition_table[STATE_DASH][CHAR_EQUALS] = STATE_FINAL; // '-='
    ctx->transition_table[STATE_DASH][CHAR_DIGIT] = STATE_INTEGER; // '-123'

    // COMMENT STATE TRANSITIONS: continues until another '*' is found, then returns to START
    ctx->transition_table[STATE_COMMENT][CHAR_STAR] = STATE_START; // End of comment
    ctx->transition_table[STATE_COMMENT][CHAR_EOF] = STATE_ERROR; // Comment left open
    // Any other character continues the comment
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_STAR && j != CHAR_EOF) {
            ctx->transition_table[STATE_COMMENT][j] = STATE_COMMENT;
        }
    }

    // STRING STATE TRANSITIONS: continues until another '"' is found
    ctx->transition_table[STATE_STRING][CHAR_QUOTE] = STATE_FINAL; // End of string
    ctx->transition_table[STATE_STRING][CHAR_EOF] = STATE_ERROR; // String left open
    // Any other character is part of the string
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_QUOTE && j != CHAR_EOF) {
            ctx->transition_table[STATE_STRING][j] = STATE_STRING;
        }
    }
}
// END OF SETUP_TRANSITION_TABLE
/*****************************************************************************/

// Function to add keywords to the symbol table
// Each keyword is added with its specific TokenType
void add_keyword(LexContext* ctx, const char* keyword, TokenType type) {
    if (ctx->keyword_count < MAX_KEYWORDS) {
        // Store the keyword string (though not strictly necessary as symbol_table stores it)
        ctx->keywords[ctx->keyword_count] = strdup(keyword);
        // Add to the symbol table with its specific token type and marked as a keyword
        add_to_symbol_table(ctx, keyword, type, true);
        ctx->keyword_count++;
    } else {
        fprintf(stderr, "Warning: Maximum number of keywords (%d) reached. Could not add '%s'.\n", MAX_KEYWORDS, keyword);
    }
}

// Determines the character class for a given character
CharClass get_char_class(int c) {
    if (c == EOF) return CHAR_EOF;
    if (isalpha(c)) return CHAR_ALPHA;
    if (c == '_') return CHAR_UNDERSCORE;
    if (isdigit(c)) return CHAR_DIGIT;
    if (c == '"') return CHAR_QUOTE;
    if (c == '*') return CHAR_STAR;
    if (isspace(c)) return CHAR_WHITESPACE;
    if (c == ':') return CHAR_COLON;
    if (c == '+') return CHAR_PLUS;
    if (c == '-') return CHAR_DASH;
    if (c == '=') return CHAR_EQUALS;
    if (c == ';') return CHAR_EOL_SEMICOLON;
    if (c == '{') return CHAR_OPENB_CURLY;
    if (c == '}') return CHAR_CLOSEB_CURLY;
    if (c == '(') return CHAR_LPAREN_ROUND;
    if (c == ')') return CHAR_RPAREN_ROUND;
    return CHAR_OTHER; // Unrecognized character
}

// Reads the next character from the input stream, using a buffer
int next_char(LexContext* ctx) {
    // Fill buffer if empty
    if (ctx->buffer_pos >= ctx->buffer_size) {
        ctx->buffer_size = fread(ctx->buffer, 1, sizeof(ctx->buffer), ctx->input);
        ctx->buffer_pos = 0;
        if (ctx->buffer_size == 0) {
            return EOF; // End of file
        }
    }

    // Get the next character and update location for error reporting
    int c = ctx->buffer[ctx->buffer_pos++];

    if (c == '\n') {
        ctx->location.line++;
        ctx->location.column = 0; // Reset column for new line
    } else {
        ctx->location.column++;
    }

    return c;
}

// Puts the last read character back into the input stream (conceptually)
void unget_char(LexContext* ctx) {
    if (ctx->buffer_pos > 0) {
        ctx->buffer_pos--;
        // Adjust location accordingly
        if (ctx->buffer[ctx->buffer_pos] == '\n') {
            ctx->location.line--;
            // Column cannot be accurately restored to previous line's end, so leave as is
            // For simple line/column tracking, this might be a minor inaccuracy for multi-line tokens
        } else {
            ctx->location.column--;
        }
    }
}

// Adds a new symbol (identifier or keyword) to the symbol table
// Returns the index of the symbol in the table, or -1 if table is full
int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword) {
    // First, check if the symbol already exists to avoid duplicates
    int index = lookup_symbol(ctx, name);
    if (index != -1) {
        return index; // Return existing index
    }

    // Add new symbol if space is available
    if (ctx->symbol_count < SYMBOL_TABLE_SIZE) {
        ctx->symbol_table[ctx->symbol_count].name = strdup(name); // Duplicate string
        ctx->symbol_table[ctx->symbol_count].type = type;
        ctx->symbol_table[ctx->symbol_count].is_keyword = is_keyword;
        return ctx->symbol_count++; // Return new index and increment count
    }

    // Symbol table full error
    report_error(ctx, "Symbol table overflow");
    return -1;
}

// Looks up a symbol by name in the symbol table
// Returns the index of the symbol, or -1 if not found
int lookup_symbol(LexContext* ctx, const char* name) {
    for (int i = 0; i < ctx->symbol_count; i++) {
        if (strcmp(ctx->symbol_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

// RETURNS THE NEXT TOKEN (STATE TRANSITION LOGIC)
/*****************************************************************************/
Token get_next_token(LexContext* ctx) {
    Token token;
    State state = STATE_START;
    State prev_state;
    CharClass char_class;
    SourceLocation token_start_location = ctx->location; // Save start location of the token

    // Clear lexeme buffer for the new token
    ctx->lexeme_length = 0;
    ctx->lexeme_buffer[0] = '\0';

    // Loop through characters to build the token based on FSM transitions
    while (state != STATE_FINAL && state != STATE_ERROR && state != STATE_RETURN && state != STATE_EOL_CHAR && state != STATE_EOF_CHAR) {
        // Read the next character if in START or COMMENT states (to skip whitespace/comments)
        if (state == STATE_START || state == STATE_COMMENT) {
            ctx->current_char = next_char(ctx);
        }

        // Determine the character class of the current character
        char_class = get_char_class(ctx->current_char);

        // Save current state before transition
        prev_state = state;

        // Get the next state from the transition table
        state = ctx->transition_table[state][char_class];

        // Handle state transitions
        if (state == STATE_START || state == STATE_COMMENT) {
            // If we transition back to START or stay in COMMENT, it means we are skipping
            // whitespace or comment characters. Continue the loop to get the next char.
            continue;
        } else if (state == STATE_RETURN || state == STATE_ERROR) {
            // We've hit a boundary or an error condition.
            if (state == STATE_RETURN) {
                // If it's a RETURN state, the last character read does not belong
                // to the current token, so put it back.
                unget_char(ctx);
            } else if (state == STATE_ERROR) {
                // Specific error messages based on the previous state
                if (prev_state == STATE_COMMENT) {
                    report_error(ctx, "Unterminated comment block.");
                } else if (prev_state == STATE_STRING) {
                    report_error(ctx, "Unterminated string literal.");
                } else if (prev_state == STATE_COLON && char_class != CHAR_EQUALS) {
                    report_error(ctx, "Invalid operator: expected '=' after ':'.");
                } else if (prev_state == STATE_PLUS && char_class != CHAR_EQUALS) {
                    report_error(ctx, "Invalid operator: expected '=' after '+'.");
                } else if (prev_state == STATE_DASH && char_class != CHAR_EQUALS && char_class != CHAR_DIGIT) {
                    report_error(ctx, "Invalid operator: expected '=' or digit after '-'.");
                } else if (char_class == CHAR_OTHER || char_class == CHAR_UNDERSCORE) { // underscore can't start an identifier
                    // For unrecognized characters
                    report_error(ctx, "Unknown character or invalid start of identifier.");
                }
                // Break from loop to signify an error token
                break;
            }
        } else {
            // If not a skipping state, add the current character to the lexeme buffer
            if (ctx->lexeme_length < MAX_LEXEME_LENGTH - 1) {
                ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
            } else {
                // Lexeme too long (buffer overflow prevention)
                report_error(ctx, "Lexeme too long.");
                state = STATE_ERROR; // Transition to error state
                break;
            }

            // If the state is FINAL, we have completed a token (e.g., '{', '}', ':=')
            if (state == STATE_FINAL) break;

            // Read the next character for the next iteration (unless it's a final state)
            ctx->current_char = next_char(ctx);
        }
    }

    // Null-terminate the lexeme string
    ctx->lexeme_buffer[ctx->lexeme_length] = '\0';

    // Populate the token structure
    token.location = token_start_location;
    strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
    token.lexeme[MAX_LEXEME_LENGTH - 1] = '\0'; // Ensure null termination

    // Determine the final token type based on the final state and lexeme content
    if (state == STATE_ERROR) {
        token.type = TOKEN_ERROR;
    } else if (state == STATE_EOF_CHAR) {
        token.type = TOKEN_EOF;
    } else if (state == STATE_EOL_CHAR) { // Direct EOL token recognition
        token.type = TOKEN_EOL;
    } else if (prev_state == STATE_COLON && strcmp(token.lexeme, ":=") == 0) {
        token.type = TOKEN_ASSIGN;
    } else if (prev_state == STATE_PLUS && strcmp(token.lexeme, "+=") == 0) {
        token.type = TOKEN_PLUS_ASSIGN;
    } else if (prev_state == STATE_DASH && strcmp(token.lexeme, "-=") == 0) {
        token.type = TOKEN_MINUS_ASSIGN;
    } else if (prev_state == STATE_START) { // Single character tokens from START state
        if (strcmp(token.lexeme, "{") == 0) token.type = TOKEN_OPENB;
        else if (strcmp(token.lexeme, "}") == 0) token.type = TOKEN_CLOSEB;
        else if (strcmp(token.lexeme, "(") == 0) token.type = TOKEN_LPAREN;
        else if (strcmp(token.lexeme, ")") == 0) token.type = TOKEN_RPAREN;
        else {
            token.type = TOKEN_ERROR; // Should not happen if FSM is complete
            report_error(ctx, "Unrecognized single character token.");
        }
    }
    else if (prev_state == STATE_IDENTIFIER) {
        // Check for identifier length limit
        if (ctx->lexeme_length > MAX_VAR_LENGTH - 1) {
            token.type = TOKEN_ERROR;
            report_error(ctx, "Identifier name too long.");
        } else {
            // Lookup in symbol table to differentiate between keywords and user-defined identifiers
            int sym_idx = lookup_symbol(ctx, token.lexeme);
            if (sym_idx != -1 && ctx->symbol_table[sym_idx].is_keyword) {
                // If it's a keyword, assign its specific TokenType (e.g., TOKEN_AND, TOKEN_WRITE)
                token.type = ctx->symbol_table[sym_idx].type;
            } else {
                // It's a regular identifier
                token.type = TOKEN_IDENTIFIER;
                // Add to symbol table if not already present
                if (sym_idx == -1) {
                    sym_idx = add_to_symbol_table(ctx, token.lexeme, TOKEN_IDENTIFIER, false);
                }
                token.value.symbol_index = sym_idx;
            }
        }
    } else if (prev_state == STATE_INTEGER || (prev_state == STATE_DASH && char_class == CHAR_DIGIT)) { // Handle numbers starting with '-' too
        // Check for integer literal length limit based on MAX_INT_LENGTH (from MAX_BIGINT_STRING_LEN)
        if (ctx->lexeme_length > MAX_INT_LENGTH - 1) {
            token.type = TOKEN_ERROR;
            report_error(ctx, "Integer literal exceeds maximum allowed digits.");
        } else {
            token.type = TOKEN_INTEGER;
            // Convert the lexeme string to a BigInt value and store it
            big_int_from_string(&token.value.big_int_value, token.lexeme);
        }
    } else if (prev_state == STATE_STRING) {
        token.type = TOKEN_STRING;
    } else {
        token.type = TOKEN_ERROR; // Catch-all for unhandled states
    }

    return token;
}
/***********************/
// END OF GET_NEXT_TOKEN

// Report lexical error to stderr
void report_error(LexContext* ctx, const char* message) {
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "Lexical error at %s:%d:%d: %s",
             ctx->location.filename,
             ctx->location.line,
             ctx->location.column,
             message);
    fprintf(stderr, "%s\n", ctx->error_msg);
}

// Converts TokenType enum value to a human-readable string
const char* token_type_str(TokenType type) {
    switch (type) {
        case TOKEN_EOF: return "EndOfFile";
        case TOKEN_EOL: return "EndOfLine";
        case TOKEN_IDENTIFIER: return "Identifier";
        case TOKEN_AND: return "KEYWORD_AND";
        case TOKEN_WRITE: return "KEYWORD_WRITE";
        case TOKEN_REPEAT: return "KEYWORD_REPEAT";
        case TOKEN_NEWLINE: return "KEYWORD_NEWLINE";
        case TOKEN_TIMES: return "KEYWORD_TIMES";
        case TOKEN_NUMBER: return "KEYWORD_NUMBER";
        case TOKEN_INTEGER: return "IntConstant";
        case TOKEN_ASSIGN: return "AssignmentOp";
        case TOKEN_PLUS_ASSIGN: return "PlusAssignOp";
        case TOKEN_MINUS_ASSIGN: return "MinusAssignOp";
        case TOKEN_OPENB: return "OpenBlock";
        case TOKEN_CLOSEB: return "CloseBlock";
        case TOKEN_LPAREN: return "LeftParen";
        case TOKEN_RPAREN: return "RightParen";
        case TOKEN_STRING: return "StringConstant";
        case TOKEN_ERROR: return "ERROR";
        case NUM_TOKEN_TYPES: return "NUM_TOKEN_TYPES_MARKER"; // Should not be directly used
        default: return "UNKNOWN_TOKEN_TYPE";
    }
}

// Prints a token's details to stdout for debugging/output
void print_token(Token token) {
    printf("%-15s %-20s  Line:%-4d Col:%-4d",
           token_type_str(token.type),
           token.lexeme,
           token.location.line,
           token.location.column);

    if (token.type == TOKEN_INTEGER) {
        char big_int_str[MAX_BIGINT_STRING_LEN + 1]; // Buffer for BigInt string
        big_int_to_string(&token.value.big_int_value, big_int_str); // Convert BigInt to string
        printf("  Value: %s", big_int_str); // Print the BigInt string
    } else if (token.type == TOKEN_IDENTIFIER) {
        printf("  Symbol Index: %d", token.value.symbol_index);
    }
    printf("\n");
}

// Function to free dynamically allocated memory within the LexContext
void free_lex_context(LexContext* ctx) {
    // Free names in the symbol table (allocated with strdup)
    for (int i = 0; i < ctx->symbol_count; i++) {
        free(ctx->symbol_table[i].name);
    }

    for (int i = 0; i < ctx->keyword_count; i++) {
        free(ctx->keywords[i]);
    }
}


// Main lexer function: reads input file and returns a dynamically allocated array of tokens
Token* lexer(FILE* inputFile, char* input_filename, int* num_tokens_out) {
    LexContext ctx;
    // Initialize the lexer context. This should happen once for the file.
    init_lexer(&ctx, inputFile, input_filename);

    // Dynamically allocate and grow the tokens array to store recognized tokens
    int capacity = 100; // Initial capacity for the token array
    Token* tokens = (Token*)malloc(capacity * sizeof(Token));
    if (!tokens) {
        fprintf(stderr, "Memory allocation failed for tokens array.\n");
        if (num_tokens_out) *num_tokens_out = 0;
        return NULL;
    }

    int tokencount = 0; // Counter for the number of tokens found
    Token token;        // Temporary variable to hold each token

    do {
        // Expand the tokens array if capacity is reached
        if (tokencount >= capacity) {
            capacity *= 2; // Double the capacity
            Token* new_tokens = (Token*)realloc(tokens, capacity * sizeof(Token));
            if (!new_tokens) {
                fprintf(stderr, "Memory re-allocation failed for tokens array.\n");
                free(tokens); // Free the original array if realloc fails
                if (num_tokens_out) *num_tokens_out = 0;
                return NULL;
            }
            tokens = new_tokens; // Update pointer to the new, larger array
        }

        token = get_next_token(&ctx); // Get the next token from the input
        tokens[tokencount++] = token; // Store the token and increment count
        print_token(token);           // Print the token (for debugging/output)

    } while (token.type != TOKEN_EOF && token.type != TOKEN_ERROR); // Continue until EOF or an error token is found

    printf("Lexical analysis completed.\n");

    // Set the output parameter with the total number of tokens (including EOF/ERROR)
    if (num_tokens_out) *num_tokens_out = tokencount;

    // Free any dynamically allocated memory within the lexer context
    free_lex_context(&ctx);

    return tokens; // Return the array of tokens
}
