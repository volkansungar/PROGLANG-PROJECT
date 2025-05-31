#include "lexer.h"

// AST Node
typedef struct ASTNode {
    ASTNodeType type;
    char* value;
    struct ASTNode** children;
    int child_count;
    SourceLocation location;
} ASTNode;

// Grammar symbols (terminals + non-terminals)
typedef enum {
    // Terminals (matching tokens)
    TERM_EOF = 0,
    TERM_IDENTIFIER,
    TERM_NUMBER_KW,     // "number" keyword
    TERM_WRITE_KW,      // "write" keyword
    TERM_REPEAT_KW,     // "repeat" keyword
    TERM_TIMES_KW,      // "times" keyword
    TERM_AND_KW,        // "and" keyword
    TERM_NEWLINE_KW,    // "newline" keyword
    TERM_INTEGER,
    TERM_ASSIGN_OP,     // ":="
    TERM_INCR_OP,       // "+="
    TERM_DECR_OP,       // "-="
    TERM_OPENB,         // "{"
    TERM_CLOSEB,        // "}"
    TERM_STRING,
    TERM_EOL,           // ";"
    
    // Non-terminals
    NT_PROGRAM,
    NT_STMT_LIST,
    NT_STATEMENT,
    NT_DECLARATION,
    NT_ASSIGNMENT,
    NT_INCREMENT,
    NT_DECREMENT,
    NT_OUTPUT,
    NT_LOOP,
    NT_CODE_BLOCK,
    NT_OUTPUT_LIST,
    NT_OUTPUT_ITEM,
    NT_INT_VALUE,
    
    NUM_SYMBOLS
} Symbol;
