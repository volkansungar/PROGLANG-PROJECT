#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// max length for the data type of "number"
#define MAX_INT_LENGTH 100

#define MAX_VAR_LENGTH 20

// Maximum number of keywords (can be increased to add new keywords to the language)
#define MAX_KEYWORDS 6

// Maximum size of symbol table
#define SYMBOL_TABLE_SIZE 1024


// Symbol table entry
typedef struct {
    char* name;
    TokenType type;
    bool is_keyword;
} SymbolEntry;

// State types
typedef enum {
    STATE_START = 0,
    STATE_IDENTIFIER,
    STATE_INTEGER,
    STATE_INCREMENT,
    STATE_DECREMENT,
    STATE_ASSIGN,
    STATE_STRING,
    STATE_COMMENT,
	STATE_DASH,
    STATE_ERROR,
    STATE_FINAL,
    STATE_EOL,
    STATE_EOF,
    STATE_RETURN,

    // Number of states
    NUM_STATES
} State;

// character types
// !!should be updated when new characters are added to the language!!
typedef enum {
    CHAR_ALPHA = 0,   // a-z, A-Z, _
    CHAR_DIGIT,       // 0-9
	CHAR_PLUS,	      // +
    CHAR_EQUALS,        // =
    CHAR_COLON,      // :
	CHAR_DASH,        // -
    CHAR_QUOTE,       // "
    CHAR_STAR,        // *
    CHAR_WHITESPACE,  // space, tab, etc.
    CHAR_EOL,		  // ;
    CHAR_OPENB,       // {
    CHAR_CLOSEB,      // }
    CHAR_OTHER,       // Any other character
    CHAR_EOF,         // End of file

    // Number of character classes
    NUM_CHAR_CLASSES
} CharClass;

// Lexical analyzer context
typedef struct {
    // Input buffer
    FILE* input;
    char buffer[4096];
    int buffer_pos;
    int buffer_size;

    // current character
    int current_char;
    SourceLocation location;

    // Lexeme buffer
    char lexeme_buffer[MAX_LEXEME_LENGTH];
    int lexeme_length;

    // Symbol table (contains Keywords and identifiers)
    SymbolEntry symbol_table[SYMBOL_TABLE_SIZE];
    int symbol_count;

    // Keywords
    char* keywords[MAX_KEYWORDS];
    int keyword_count;

    // transition table (FINITE STATE MACHINE LOGIC)
    State transition_table[NUM_STATES][NUM_CHAR_CLASSES];

    // error message string
    char error_msg[256];
} LexContext;

// FUNCTION DECLARATIONS
void init_lexer(LexContext* ctx, FILE* input, const char* filename);
void setup_transition_table(LexContext* ctx);
CharClass get_char_class(int c);
Token get_next_token(LexContext* ctx);
int next_char(LexContext* ctx);
void unget_char(LexContext* ctx);
int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword);
int lookup_symbol(LexContext* ctx, const char* name);
const char* token_type_str(TokenType type);
void report_error(LexContext* ctx, const char* message);

// LEXER INITIALISE
/*****************/
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

    // allocate memory for symbol table
    memset(ctx->symbol_table, 0, sizeof(ctx->symbol_table));

    // initialise the transition table
    setup_transition_table(ctx);

    // add keywords to the symbol table
    add_to_symbol_table(ctx, "write", TOKEN_WRITE, true);
    add_to_symbol_table(ctx, "and", TOKEN_AND, true);
    add_to_symbol_table(ctx, "repeat", TOKEN_REPEAT, true);
    add_to_symbol_table(ctx, "number", TOKEN_NUMBER, true);
    add_to_symbol_table(ctx, "newline", TOKEN_NEWLINE, true);
    add_to_symbol_table(ctx, "times", TOKEN_TIMES, true);



}

// SETUP THE TRANSITION TABLE FUNCTION
/************************************/
void setup_transition_table(LexContext* ctx) {
    // ALL STATES ARE STATE_RETURN BY DEFAULT
    for (int i = 0; i < NUM_STATES; i++) {
        for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
            if (j == CHAR_EOF) {
                ctx->transition_table[i][j] = STATE_EOF;
            } else if (j == CHAR_OTHER) {
                ctx->transition_table[i][j] = STATE_ERROR;
            } else {ctx->transition_table[i][j] = STATE_RETURN;}
        }
    }

    // START STATE TRANSITIONS
    ctx->transition_table[STATE_START][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_START][CHAR_DIGIT] = STATE_INTEGER;
    ctx->transition_table[STATE_START][CHAR_PLUS] = STATE_INCREMENT;
    ctx->transition_table[STATE_START][CHAR_COLON] = STATE_ASSIGN;
    ctx->transition_table[STATE_START][CHAR_DASH] = STATE_DASH;
    ctx->transition_table[STATE_START][CHAR_QUOTE] = STATE_STRING;
    ctx->transition_table[STATE_START][CHAR_STAR] = STATE_COMMENT;
    ctx->transition_table[STATE_START][CHAR_OPENB] = STATE_FINAL;
    ctx->transition_table[STATE_START][CHAR_CLOSEB] = STATE_FINAL;
    ctx->transition_table[STATE_START][CHAR_WHITESPACE] = STATE_START;
    ctx->transition_table[STATE_START][CHAR_EQUALS] = STATE_ERROR;
    ctx->transition_table[STATE_START][CHAR_EOL] = STATE_EOL;


    // ALL EOF CHARS LEAD TO STATE_EOF
    for (int i = 0; i < NUM_STATES; i++) {
            ctx->transition_table[i][CHAR_EOF] = STATE_EOF;
        }


    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
            ctx->transition_table[STATE_RETURN][j] = STATE_START;
        }

    // IDENTIFIER STATE TRANSITIONS
    ctx->transition_table[STATE_IDENTIFIER][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_IDENTIFIER][CHAR_DIGIT] = STATE_IDENTIFIER;

    // INTEGER STATE TRANSITIONS
    ctx->transition_table[STATE_INTEGER][CHAR_DIGIT] = STATE_INTEGER;

    // OPERATOR STATE TRANSITIONS
    ctx->transition_table[STATE_INCREMENT][CHAR_EQUALS] = STATE_FINAL; // assignment operator and increment operator
    ctx->transition_table[STATE_DECREMENT][CHAR_EQUALS] = STATE_FINAL;
    ctx->transition_table[STATE_ASSIGN][CHAR_EQUALS] = STATE_FINAL;


    // DASH STATE TRANSITIONS
    ctx->transition_table[STATE_DASH][CHAR_DIGIT] = STATE_INTEGER;
    ctx->transition_table[STATE_DASH][CHAR_EQUALS] = STATE_FINAL; // decrement operator

    // COMMENT_LINE STATE TRANSITIONS
    ctx->transition_table[STATE_COMMENT][CHAR_STAR] = STATE_START;
    ctx->transition_table[STATE_COMMENT][CHAR_EOF] = STATE_ERROR;
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_STAR && j != CHAR_EOF) {
            ctx->transition_table[STATE_COMMENT][j] = STATE_COMMENT;
        }

    }

    // STRING STATE TRANSITIONS
    ctx->transition_table[STATE_STRING][CHAR_QUOTE] = STATE_FINAL;
    ctx->transition_table[STATE_STRING][CHAR_EOF] = STATE_ERROR;
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_QUOTE && j != CHAR_EOF) {
            ctx->transition_table[STATE_STRING][j] = STATE_STRING;
        }
    }

}
// END OF SETUP_TRANSITION_TABLE
/******************************/


// character class
// !!should be updated when new characters are added to the language!!
CharClass get_char_class(int c) {
    if (c == EOF) return CHAR_EOF;
    if (isalpha(c) || c == '_') return CHAR_ALPHA;
    if (isdigit(c)) return CHAR_DIGIT;
    if (c == '"') return CHAR_QUOTE;
    if (c == '*') return CHAR_STAR;
    if (isspace(c)) return CHAR_WHITESPACE;
    if (c ==':') return CHAR_COLON;
    if (c =='+') return CHAR_PLUS;
    if (c =='=') return CHAR_EQUALS;
    if (c == '-') return CHAR_DASH;
    if (c == ';') return CHAR_EOL;
    if (c == '{') return CHAR_OPENB;
    if (c == '}') return CHAR_CLOSEB;
    return CHAR_OTHER;
}

// return the next char
int next_char(LexContext* ctx) {
    // add new chars if buffer is empty (buffered reader logic)
    if (ctx->buffer_pos >= ctx->buffer_size) {
        ctx->buffer_size = fread(ctx->buffer, 1, sizeof(ctx->buffer), ctx->input);
        ctx->buffer_pos = 0;
        if (ctx->buffer_size == 0) {
            return EOF;
        }
    }

    // get the next character from buffer and move forward the position
    int c = ctx->buffer[ctx->buffer_pos++];

    // move forward the error handler position
    if (c == '\n') {
        ctx->location.line++;
        ctx->location.column = 0;
    } else {
        ctx->location.column++;
    }

    return c;
}

// go back one char
void unget_char(LexContext* ctx) {
    if (ctx->buffer_pos > 0) {
        ctx->buffer_pos--;
        if (ctx->buffer[ctx->buffer_pos] == '\n') {
            ctx->location.line--;
            // We don't know the exact column, so we leave it as is
        } else {
            ctx->location.column--;
        }
    }
}

// add identifier or keyword to symbol table
int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword) {
    // First check if it already exists
    int index = lookup_symbol(ctx, name);
    if (index != -1) {
        return index;
    }

    // add
    if (ctx->symbol_count < SYMBOL_TABLE_SIZE) {
        ctx->symbol_table[ctx->symbol_count].name = strdup(name); // passed by value
        ctx->symbol_table[ctx->symbol_count].type = type;
        ctx->symbol_table[ctx->symbol_count].is_keyword = is_keyword;
        return ctx->symbol_count++;
    }

    // table full
    report_error(ctx, "Symbol table overflow");
    return -1;
}

int lookup_symbol(LexContext* ctx, const char* name) {
    for (int i = 0; i < ctx->symbol_count; i++) {
        if (strcmp(ctx->symbol_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
// RETURNS THE NEXT TOKEN (STATE TRANSITION LOGIC HAPPENS HERE)
/*************************************************************/
Token get_next_token(LexContext* ctx) {
    Token token;
    State state = STATE_START;
    State prev_state;
    CharClass char_class;
    SourceLocation token_start = ctx->location;

    // Clear lexeme buffer
    ctx->lexeme_length = 0;
    ctx->lexeme_buffer[0] = '\0';

    // process the characters. in every get_next_token, works until the lexeme end
    while (state != STATE_FINAL && state != STATE_ERROR && state != STATE_RETURN) {
        // Get next character if needed
        if (state == STATE_START || state == STATE_COMMENT) {
            ctx->current_char = next_char(ctx);
        }

        // Get character class
        char_class = get_char_class(ctx->current_char);

        // Save current state
        prev_state = state;

        // Get next state from transition table
        state = ctx->transition_table[state][char_class];

        // Handle special cases
        if (state == STATE_START || state == STATE_COMMENT) {
            // Skip whitespace and comments
            continue;
        } else if (state == STATE_RETURN || state == STATE_ERROR) {
            // We've reached a return or error state
            if (state == STATE_RETURN) {
                // STATE_RETURN ungets the char
                unget_char(ctx);
            } else if (state == STATE_ERROR) {
                if (prev_state == STATE_COMMENT) {
                    report_error(ctx, "Comment left open");
                    break;
                } else if (prev_state == STATE_STRING) {
                    report_error(ctx, "String left open");
                    break;
                } else if (char_class == CHAR_OTHER)
                {
                    report_error(ctx, "Unknown Character");
                    state = prev_state;
                    break;
                }
            }

        } else if (state == STATE_EOF) {
            break;
        } else {
            // add character to the lexeme buffer (this creates the valid token)
            if (ctx->lexeme_length < MAX_LEXEME_LENGTH - 1) {
                ctx->lexeme_buffer[ctx->lexeme_length++] = ctx->current_char;
                if (state == STATE_FINAL) break;
            } else {
                report_error(ctx, "Lexeme too long");
                state = STATE_ERROR;
                break;
            }

            ctx->current_char = next_char(ctx);

        }

    }


    // end the lexeme string (null termination)
    ctx->lexeme_buffer[ctx->lexeme_length] = '\0';

    token.location = token_start;
    strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);

    // determine the token type
    if (state == STATE_ERROR) {
        token.type = TOKEN_ERROR;
    } else if (state == STATE_EOF) {
        token.type = TOKEN_EOF;
    } else if (prev_state == STATE_ASSIGN) {
        token.type = TOKEN_ASSIGN;
    } else if (prev_state == STATE_INCREMENT) {
        token.type = TOKEN_INCREMENT;
    } else if (prev_state == STATE_DECREMENT) {
        token.type = TOKEN_DECREMENT;
    } else if (prev_state == STATE_EOL) {
        token.type = TOKEN_EOL;
    } else if (char_class == CHAR_OPENB) {
        token.type = TOKEN_OPENB;
    } else if (char_class == CHAR_CLOSEB) {
        token.type = TOKEN_CLOSEB;
    } else if (prev_state == STATE_IDENTIFIER) {
            // check if variable is not too long
        if (ctx->lexeme_length > MAX_VAR_LENGTH - 1) {
            token.type = TOKEN_ERROR;
            report_error(ctx, "Variable too long");
        } else {
        // keyword control
        int sym_idx = lookup_symbol(ctx, token.lexeme);
        if (sym_idx != -1 && ctx->symbol_table[sym_idx].is_keyword) {
            if (strcmp(token.lexeme, "write") == 0) {token.type = TOKEN_WRITE;}
            if (strcmp(token.lexeme, "and") == 0) {token.type = TOKEN_AND;}
            if (strcmp(token.lexeme, "repeat") == 0) {token.type = TOKEN_REPEAT;}
            if (strcmp(token.lexeme, "newline") == 0) {token.type = TOKEN_NEWLINE;}
            if (strcmp(token.lexeme, "number") == 0) {token.type = TOKEN_NUMBER;}
            if (strcmp(token.lexeme, "times") == 0) {token.type = TOKEN_TIMES;}
        } else {
            token.type = TOKEN_IDENTIFIER;
            // add symbol
            if (sym_idx == -1) {
                sym_idx = add_to_symbol_table(ctx, token.lexeme, TOKEN_IDENTIFIER, false);
            }
            token.value.symbol_index = sym_idx;
        }
        }
    } else if (prev_state == STATE_INTEGER) {
        // check if integer is not too long
        if (ctx->lexeme_length > MAX_INT_LENGTH - 1) {
            token.type = TOKEN_ERROR;
            report_error(ctx, "Integer too long");
        } else {
        token.type = TOKEN_INTEGER;
        token.value.int_value = atoll(token.lexeme);}
    } else if (prev_state == STATE_STRING) {
        token.type = TOKEN_STRING;
    } else {
        token.type = TOKEN_ERROR;
    }

    return token;
}
/***********************/
// END OF GET_NEXT_TOKEN

// Report lexical error
void report_error(LexContext* ctx, const char* message) {
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "Lexical error at %s:%d:%d: %s",
             ctx->location.filename,
             ctx->location.line,
             ctx->location.column,
             message);
    fprintf(stderr, "%s\n", ctx->error_msg);
}

// token type to string
const char* token_type_str(TokenType type) {
    switch (type) {
        case TOKEN_EOF: return "EndOfFile";
        case TOKEN_EOL: return "EndOfLine";
        case TOKEN_IDENTIFIER: return "Identifier";
        case TOKEN_INCREMENT: return "Increment";
        case TOKEN_ASSIGN: return "Assign";
        case TOKEN_DECREMENT: return "Decrement";
        case TOKEN_INTEGER: return "IntConstant";
        case TOKEN_TIMES: return "Times";
        case TOKEN_NUMBER: return "Number";
        case TOKEN_REPEAT: return "Repeat";
        case TOKEN_AND: return "And";
        case TOKEN_WRITE: return "Write";
        case TOKEN_STRING: return "StringConstant";
        case TOKEN_ERROR: return "ERROR";
        case TOKEN_OPENB: return "OpenBlock";
        case TOKEN_CLOSEB: return "CloseBlock";
        default: return "UNKNOWN";
    }

}

// Print token
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

Token* lexer(FILE* inputFile, char* input_filename) {

    fseek(inputFile, 0, SEEK_END);
    long file_size = ftell(inputFile);

	LexContext ctx;
	init_lexer(&ctx, inputFile, input_filename);


    Token* tokens = malloc(file_size*MAX_LEXEME_LENGTH);
	Token token;
    int tokencount = 0;
    rewind(inputFile);

	do {
        token = get_next_token(&ctx);
        tokens[tokencount++] = token;
        print_token(token);
    } while (token.type != TOKEN_EOF);

	printf("Lexical analysis completed.");

	return tokens;
}
