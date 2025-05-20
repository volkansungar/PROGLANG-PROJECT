
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

/*
 * Production-grade lexical analyzer using a table-driven FSM approach
 * Features:
 * - Table-driven state transitions for efficiency
 * - Buffer management with lookahead
 * - Multiple token types (keywords, identifiers, numbers, operators)
 * - Source location tracking
 * - Error reporting and recovery
 * - Symbol table for identifiers and keywords
 */

// Maximum lexeme length
#define MAX_LEXEME_LENGTH 256

//not implemented yet
#define MAX_INT_LENGTH

// Maximum number of keywords
#define MAX_KEYWORDS 6

// Maximum size of symbol table
#define SYMBOL_TABLE_SIZE 1024

// Token çeşitleri
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

// Alınan karakterlerin bulunduğu yer (error handling ve token konumu için)
typedef struct {
    int line;
    int column;
    const char* filename;
} SourceLocation;

// Token yapısı
typedef struct {
    TokenType type;
    char lexeme[MAX_LEXEME_LENGTH];
    SourceLocation location;
    union {
        long long int_value;
        double float_value;
        int symbol_index;
    } value;
} Token;

// Symbol table entry
typedef struct {
    char* name;
    TokenType type;
    bool is_keyword;
} SymbolEntry;

// State type
typedef enum {
    STATE_START = 0,
    STATE_IDENTIFIER,
    STATE_INTEGER,
    STATE_OPERATOR,
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

// Karakter çeşitleri
typedef enum {
    CHAR_ALPHA = 0,   // a-z, A-Z, _
    CHAR_DIGIT,       // 0-9
	CHAR_OPERATOR,	  // :, +
    CHAR_EQUALS,      // =
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
    
    // Anlık karakter
    int current_char;
    SourceLocation location;
    
    // Lexeme buffer
    char lexeme_buffer[MAX_LEXEME_LENGTH];
    int lexeme_length;
    
    // Sembol Tablosu (Keyword'ler ve identifier'lar bulunur)
    SymbolEntry symbol_table[SYMBOL_TABLE_SIZE];
    int symbol_count;
    
    // Keywords
    char* keywords[MAX_KEYWORDS];
    int keyword_count;
    
    // Geçiş tablosu
    State transition_table[NUM_STATES][NUM_CHAR_CLASSES];
    
    // Hata mesajı alanı
    char error_msg[256];
} LexContext;

// FONKSIYON TANIMLAMALARI
void init_lexer(LexContext* ctx, FILE* input, const char* filename);
void setup_transition_table(LexContext* ctx);
void add_keyword(LexContext* ctx, const char* keyword);
CharClass get_char_class(int c);
Token get_next_token(LexContext* ctx);
int next_char(LexContext* ctx);
void unget_char(LexContext* ctx);
int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword);
int lookup_symbol(LexContext* ctx, const char* name);
const char* token_type_str(TokenType type);
void print_token(Token token);
void report_error(LexContext* ctx, const char* message);

// LEXER INITIALISE
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
    
    // Sembol tablosu için hafıza ayırma
    memset(ctx->symbol_table, 0, sizeof(ctx->symbol_table));
    
    // Geçiş tablosu tanım
    setup_transition_table(ctx);
    
    // Keyword'leri ekle
    add_keyword(ctx, "write");
    add_keyword(ctx, "newline");
    add_keyword(ctx, "repeat");
    add_keyword(ctx, "times");
    add_keyword(ctx, "number");
    add_keyword(ctx, "and");
}

// GEÇİŞ TABLOSU OLUŞTUR
void setup_transition_table(LexContext* ctx) {
    // Tüm state'ler varsayılan olarak STATE_RETURN
    for (int i = 0; i < NUM_STATES; i++) {
        for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
            if (j == CHAR_EOF) {
                ctx->transition_table[i][j] = STATE_EOF;
            } else {ctx->transition_table[i][j] = STATE_RETURN;}
        }
    }
    
    // START STATE GEÇİŞLERİ
    ctx->transition_table[STATE_START][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_START][CHAR_DIGIT] = STATE_INTEGER;
    ctx->transition_table[STATE_START][CHAR_OPERATOR] = STATE_OPERATOR;
	ctx->transition_table[STATE_START][CHAR_DASH] = STATE_DASH;
    ctx->transition_table[STATE_START][CHAR_QUOTE] = STATE_STRING;
    ctx->transition_table[STATE_START][CHAR_STAR] = STATE_COMMENT;
    ctx->transition_table[STATE_START][CHAR_OPENB] = STATE_FINAL;
    ctx->transition_table[STATE_START][CHAR_CLOSEB] = STATE_FINAL;
    ctx->transition_table[STATE_START][CHAR_WHITESPACE] = STATE_START;
    ctx->transition_table[STATE_START][CHAR_EOL] = STATE_EOL;


    for (int i = 0; i < NUM_STATES; i++) {
            ctx->transition_table[i][CHAR_EOF] = STATE_EOF;
        }
    
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
            ctx->transition_table[STATE_RETURN][j] = STATE_START;
        }
    
    // IDENTIFIER state geçişleri
    ctx->transition_table[STATE_IDENTIFIER][CHAR_ALPHA] = STATE_IDENTIFIER;
    ctx->transition_table[STATE_IDENTIFIER][CHAR_DIGIT] = STATE_IDENTIFIER;
    //ctx->transition_table[STATE_IDENTIFIER][CHAR_WHITESPACE] = STATE_RETURN; ///////////////////

    // INTEGER state geçişleri
    ctx->transition_table[STATE_INTEGER][CHAR_DIGIT] = STATE_INTEGER;
    
    // OPERATOR state geçişleri
    ctx->transition_table[STATE_OPERATOR][CHAR_EQUALS] = STATE_FINAL;


    //DASH
    ctx->transition_table[STATE_DASH][CHAR_DIGIT] = STATE_INTEGER;
    ctx->transition_table[STATE_DASH][CHAR_EQUALS] = STATE_FINAL; /////////////////////
    
    // COMMENT_LINE state geçişleri
    ctx->transition_table[STATE_COMMENT][CHAR_STAR] = STATE_START; ///////////////////////
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_STAR) {
            ctx->transition_table[STATE_COMMENT][j] = STATE_COMMENT;
        }
    }
    
    // STRING state geçişleri
    ctx->transition_table[STATE_STRING][CHAR_QUOTE] = STATE_FINAL;
    ctx->transition_table[STATE_STRING][CHAR_EOF] = STATE_ERROR;
    for (int j = 0; j < NUM_CHAR_CLASSES; j++) {
        if (j != CHAR_QUOTE && j != CHAR_EOF) {
            ctx->transition_table[STATE_STRING][j] = STATE_STRING;
        }
    }

}

// keyword kaydetme fonks
void add_keyword(LexContext* ctx, const char* keyword) {
    if (ctx->keyword_count < MAX_KEYWORDS) {
        ctx->keywords[ctx->keyword_count] = strdup(keyword);
        add_to_symbol_table(ctx, keyword, TOKEN_KEYWORD, true);
        ctx->keyword_count++;
    }
}

// character class
CharClass get_char_class(int c) {
    if (c == EOF) return CHAR_EOF;
    if (isalpha(c) || c == '_') return CHAR_ALPHA;
    if (isdigit(c)) return CHAR_DIGIT;
    if (c == '"') return CHAR_QUOTE;
    if (c == '*') return CHAR_STAR;
    if (isspace(c)) return CHAR_WHITESPACE;
    if (strchr(":+", c)) return CHAR_OPERATOR;
    if (c =='=') return CHAR_EQUALS;
	if (c == '-') return CHAR_DASH;
    if (c == ';') return CHAR_EOL;
    if (c == '{') return CHAR_OPENB;
    if (c == '}') return CHAR_CLOSEB;
    return CHAR_OTHER;
}

// sonraki karakteri oku
int next_char(LexContext* ctx) {
    // buffer boşsa buffer'a yenilerini ekle
    if (ctx->buffer_pos >= ctx->buffer_size) {
        ctx->buffer_size = fread(ctx->buffer, 1, sizeof(ctx->buffer), ctx->input);
        ctx->buffer_pos = 0;
        if (ctx->buffer_size == 0) {
            return EOF;
        }
    }

    //karakteri bufferdan al
    int c = ctx->buffer[ctx->buffer_pos++];
    
    // konumu ilerlet
    if (c == '\n') {
        ctx->location.line++;
        ctx->location.column = 0;
    } else {
        ctx->location.column++;
    }
    
    return c;
}

// bir karakter geri dön
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

// tabloya sembol ekle (identifier, keyword)
int add_to_symbol_table(LexContext* ctx, const char* name, TokenType type, bool is_keyword) {
    // First check if it already exists
    int index = lookup_symbol(ctx, name);
    if (index != -1) {
        return index;
    }
    
    // ekle
    if (ctx->symbol_count < SYMBOL_TABLE_SIZE) {
        ctx->symbol_table[ctx->symbol_count].name = strdup(name); // passed by value
        ctx->symbol_table[ctx->symbol_count].type = type;
        ctx->symbol_table[ctx->symbol_count].is_keyword = is_keyword;
        return ctx->symbol_count++;
    }
    
    // tablo dolu
    report_error(ctx, "Symbol table overflow");
    return -1;
}

// sembol ara
int lookup_symbol(LexContext* ctx, const char* name) {
    for (int i = 0; i < ctx->symbol_count; i++) {
        if (strcmp(ctx->symbol_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
// SONRAKİ TOKENİ GETİR
Token get_next_token(LexContext* ctx) {
    Token token;
    State state = STATE_START;
    State prev_state;
    CharClass char_class;
    SourceLocation token_start = ctx->location;

    // Clear lexeme buffer
    ctx->lexeme_length = 0;
    ctx->lexeme_buffer[0] = '\0';
    
    // karakterleri işler, her get_next_token'de lexeme sonuna kadar çalışır
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
                // return state karakteri geri yollar
                unget_char(ctx);
            }   
        } else if (state == STATE_EOF) {
            break;
        } else {
            // lexeme buffer'a karakteri ekle
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
    
    
    // lexeme sonlandır (null termination)
    ctx->lexeme_buffer[ctx->lexeme_length] = '\0';

    token.location = token_start;
    strncpy(token.lexeme, ctx->lexeme_buffer, MAX_LEXEME_LENGTH);
    
    // token türü belirle
    if (state == STATE_ERROR) {
        token.type = TOKEN_ERROR;
    } else if (state == STATE_EOF) {
        token.type = TOKEN_EOF;
    } else if (prev_state == STATE_OPERATOR) {
        token.type = TOKEN_OPERATOR;
    } else if (prev_state == STATE_EOL) {
        token.type = TOKEN_EOL;
    } else if (char_class == CHAR_OPENB) {
        token.type = TOKEN_OPENB;
    } else if (char_class == CHAR_CLOSEB) {
        token.type = TOKEN_CLOSEB;
    } else if (prev_state == STATE_IDENTIFIER) {
        // keyword kontrolü
        int sym_idx = lookup_symbol(ctx, token.lexeme);
        if (sym_idx != -1 && ctx->symbol_table[sym_idx].is_keyword) {
            token.type = TOKEN_KEYWORD;
        } else {
            token.type = TOKEN_IDENTIFIER;
            // sembol ekle
            if (sym_idx == -1) {
                sym_idx = add_to_symbol_table(ctx, token.lexeme, TOKEN_IDENTIFIER, false);
            }
            token.value.symbol_index = sym_idx;
        }
    } else if (prev_state == STATE_INTEGER) {
        token.type = TOKEN_INTEGER;
        token.value.int_value = atoll(token.lexeme);
    } else if (prev_state == STATE_STRING) {
        token.type = TOKEN_STRING;
    } else {
        token.type = TOKEN_ERROR;
    }
    
    return token;
}

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
        case TOKEN_KEYWORD: return "Keyword";
        case TOKEN_INTEGER: return "IntConstant";
        case TOKEN_OPERATOR: return "Operator";
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

int main(int argc, char *argv[]) {

	// INPUT OUTPUT DOSYA MANIPULASYONU 
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <input_filename>\n", argv[0]);
		return 1;
	}
	char *input_filename = argv[1]; // KOD DOSYASININ ADI
	char base_filename[256];
	char output_filename[256];
	char *dot_position;
	FILE *inputFile, *outputFile;
	dot_position = strrchr(input_filename, '.');

	size_t base_length = dot_position - input_filename;
	strncpy(base_filename, input_filename, base_length);
	base_filename[base_length] = 0; // NULL TERMINATION
	
	sprintf(output_filename, "%s.lx", base_filename); // lx uzantılı dosya ismi output_filename'de depolanır
	// I/O DOSYA MANIPULASYONU SONU

	inputFile = fopen(input_filename, "r");
	outputFile = fopen(output_filename, "w");

	LexContext ctx;
	init_lexer(&ctx, inputFile, input_filename);

	Token token;

	do {
        token = get_next_token(&ctx);
        print_token(token);
        fprintf(outputFile, "%s(%s)\n", token_type_str(token.type), token.lexeme);  
    } while (token.type != TOKEN_EOF);

	fclose(inputFile);
	fclose(outputFile);

	printf("Lexical analysis completed. Output written to %s\n", output_filename);

	return 0;
}
