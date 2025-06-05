#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <stdbool.h>

#define MAX_TERMINALS 200
#define MAX_NON_TERMINALS 200

// Bitmask for sets of terminals (e.g., FIRST, FOLLOW sets)
typedef unsigned char TerminalSet[MAX_TERMINALS / 8 + (MAX_TERMINALS % 8 != 0)];

// External declarations for global variables used across parser files
extern TerminalSet firstSetsForNonTerminals[MAX_NON_TERMINALS];
extern TerminalSet followSetsForNonTerminals[MAX_NON_TERMINALS];
extern bool nullable_status[MAX_NON_TERMINALS];

extern int** goto_table;           // LR parsing GOTO table
extern int num_states;             // Total number of states in the LR automaton


// Unary operators supported in the language
typedef enum {
    UNOP_INCREMENT,
    UNOP_DECREMENT
} UnaryOperator;

// Binary operators supported in the language
typedef enum {
    BINOP_ADD,
    BINOP_SUBTRACT, // Added for consistency with arithmetic expressions
    BINOP_MULTIPLY,
    BINOP_ASSIGN,
    BINOP_PLUS_ASSIGN,
    BINOP_MINUS_ASSIGN
} BinaryOperator;

// Types of Abstract Syntax Tree (AST) nodes
typedef enum {
    AST_PROGRAM,
    AST_STATEMENT_LIST,
    AST_ASSIGNMENT,
    // AST_INCREMENT, // Removed if not directly used as a top-level AST node
    // AST_DECREMENT, // Removed if not directly used as a top-level AST node
    AST_WRITE_STATEMENT,
    AST_LOOP_STATEMENT,
    AST_CODE_BLOCK,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_STRING,
    // AST_OUTPUT_LIST, // Removed if simplified
    // AST_LIST_ELEMENT // Removed if simplified
} ASTNodeType;

// Enumeration for Grammar Symbol IDs (Terminals and Non-terminals)
enum SymbolIDs {
    // Non-terminals (start from 0)
    NT_PROGRAM = 0,
    NT_STATEMENT_LIST,
    NT_STATEMENT,
    // NT_DECLARATION, // Removed if not used in grammar
    NT_ASSIGNMENT,
    // NT_INCREMENT_STMT, // Specific non-terminal for increment statement
    // NT_DECREMENT_STMT, // Specific non-terminal for decrement statement
    NT_WRITE_STATEMENT,
    NT_LOOP_STATEMENT,
    NT_CODE_BLOCK,
    // NT_OUTPUT_LIST, // Removed if simplified
    // NT_LIST_ELEMENT, // Removed if simplified
    NT_E, // Expression
    NT_T, // Term
    NT_F, // Factor
    NT_S_PRIME, // Augmented start symbol for LR parsing

    // Terminals (start from a higher value to avoid conflict with non-terminals)
    T_SEMICOLON = 100,
    T_INTEGER,      // Corresponds to TOKEN_INTEGER from lexer
    T_WRITE,
    T_REPEAT,
    T_AND,
    T_TIMES,
    T_NEWLINE,
    T_IDENTIFIER,   // Corresponds to TOKEN_IDENTIFIER from lexer
    T_STRING,       // Corresponds to TOKEN_STRING from lexer
    T_ASSIGN,       // Corresponds to TOKEN_ASSIGN from lexer
    T_PLUS_ASSIGN,  // Corresponds to TOKEN_PLUS_ASSIGN from lexer
    T_MINUS_ASSIGN, // Corresponds to TOKEN_MINUS_ASSIGN from lexer
    T_LBRACE,       // Corresponds to TOKEN_OPENB from lexer
    T_RBRACE,       // Corresponds to TOKEN_CLOSEB from lexer
    T_PLUS,         // Corresponds to TOKEN_PLUS from lexer
    T_STAR,         // Corresponds to TOKEN_STAR from lexer
    T_LPAREN,       // Corresponds to TOKEN_LPAREN from lexer
    T_RPAREN,       // Corresponds to TOKEN_RPAREN from lexer
    T_EOF = 199     // End of File marker, highest terminal ID
};

// Type of grammar symbol (terminal or non-terminal)
typedef enum {
    SYMBOL_TERMINAL,
    SYMBOL_NONTERMINAL
} SymbolType;

// Action types for the LR parsing table
typedef enum {
    ACTION_SHIFT,
    ACTION_REDUCE,
    ACTION_ACCEPT,
    ACTION_ERROR
} ActionType;

// Entry structure for the LR parsing action table
typedef struct {
    ActionType type;
    int target_state_or_production_id; // For SHIFT: target state, for REDUCE: production ID
} ActionEntry;
extern ActionEntry** action_table; // LR parsing action table

// Structure to represent a grammar symbol (terminal or non-terminal)
typedef struct {
    SymbolType type;
    int id;   // Unique ID from SymbolIDs enum
    char* name; // String representation of the symbol
} GrammarSymbol;

// Abstract Syntax Tree (AST) Node Structure
typedef struct ASTNode {
    ASTNodeType type;
    union {
        struct {
            BinaryOperator operator;
            struct ASTNode* left;
            struct ASTNode* right;
        } binary_op;

        struct {
            UnaryOperator operator;
            struct ASTNode* operand;
        } unary_op;

        struct {
            char* name;
        } identifier;

        struct {
            long long value; // Changed to long long to match lexer's int_value
        } number;

        struct {
            char* value;
        } string;

        struct {
            struct ASTNode* target; // Identifier node
            struct ASTNode* value;  // Expression node
        } assignment;

        struct {
            struct ASTNode* expression; // The expression to be written
        } write_stmt;

        struct {
            struct ASTNode* count; // Expression for loop count
            struct ASTNode* body;  // Code block for loop body
        } loop_stmt;

        struct {
            struct ASTNode** statements;
            int statement_count;
        } statement_list;

        struct {
            struct ASTNode* statement_list;
        } program;

        struct {
            struct ASTNode** statements; // Array of statements in the block
            int statement_count;
        } code_block;

    } data;

    // Common fields for all AST nodes
    struct ASTNode** children; // Array of pointers to child AST nodes
    int child_count;           // Number of children
    SourceLocation location;   // Source code location of the node
} ASTNode;

// Structure to represent a grammar production rule
typedef struct Production {
    GrammarSymbol* left_symbol;
    GrammarSymbol** right_symbols;
    int right_count;
    int production_id;
    // Pointer to the semantic action function for this production
    ASTNode* (*semantic_action)(ASTNode** children);
} Production;

// Structure to represent the entire grammar
typedef struct Grammar {
    Production *productions;
    int production_count;
    GrammarSymbol** terminals;
    int terminal_count;
    GrammarSymbol** non_terminals;
    int non_terminal_count;
    GrammarSymbol* start_symbol; // The augmented start symbol (S')
} Grammar;

// Structure for an LR(1) item
typedef struct LRItem {
    int production_id;
    int dot_position;
    TerminalSet lookahead_set; // Set of terminals that can follow this item
} LRItem;

// Structure for a set of LR(1) items (a state in the LR automaton)
typedef struct ItemSet {
    LRItem* items;
    int item_count;
    int state_id; // Unique ID for this item set (state)
    int capacity; // Current capacity of the items array
} ItemSet;

// Structure for a list of ItemSets (the canonical collection)
typedef struct ItemSetList {
    ItemSet** sets;
    int count;
    int capacity;
} ItemSetList;
extern ItemSetList canonical_collection; // Canonical collection of LR(1) item sets

// --- AST Node Creation Function Prototypes ---
ASTNode* create_ast_node(ASTNodeType type, SourceLocation location); // General creation helper
ASTNode* create_program_node(ASTNode* statement_list, SourceLocation location);
ASTNode* create_statement_list_node(ASTNode** statements, int count, SourceLocation location);
ASTNode* create_assignment_node(ASTNode* target, ASTNode* value, SourceLocation location);
ASTNode* create_write_statement_node(ASTNode* expression, SourceLocation location); // Changed to expression
ASTNode* create_loop_statement_node(ASTNode* count, ASTNode* body, SourceLocation location);
ASTNode* create_code_block_node(ASTNode* statement_list, SourceLocation location);
ASTNode* create_binary_op_node(BinaryOperator op_type, ASTNode* left, ASTNode* right, SourceLocation location);
ASTNode* create_unary_op_node(UnaryOperator op_type, ASTNode* operand, SourceLocation location);
ASTNode* create_identifier_node(const char* name, SourceLocation location);
ASTNode* create_number_node(long long value, SourceLocation location); // Changed value type
ASTNode* create_string_node(const char* value, SourceLocation location);

// --- Parser Utility Function Prototypes ---
void compute_nullable_set(Grammar* grammar, bool* nullable);
void compute_first_sets(Grammar* grammar);
void compute_follow_sets(Grammar* grammar);
void create_lr1_sets(Grammar* grammar);
void build_parsing_tables(Grammar* grammar, ItemSetList* canonical_collection, bool* nullable_status);
ASTNode* parse(Grammar* grammar, Token* tokens, int num_tokens);
void print_ast_node(ASTNode* node, int depth);
void free_ast_node(ASTNode* node);
void free_parsing_tables();
void free_canonical_collection(ItemSetList* list);
void item_set_free(ItemSet* set); // Frees items within an ItemSet

// --- Semantic Action Function Prototypes ---
// These functions build AST nodes from parsed grammar productions
ASTNode* semantic_action_passthrough(ASTNode** children);
ASTNode* semantic_action_program(ASTNode** children);
ASTNode* semantic_action_statement_list_single(ASTNode** children);
ASTNode* semantic_action_statement_list_multi(ASTNode** children);
ASTNode* semantic_action_id(ASTNode** children);
ASTNode* semantic_action_number(ASTNode** children);
ASTNode* semantic_action_string(ASTNode** children);
ASTNode* semantic_action_paren_expr(ASTNode** children);
ASTNode* semantic_action_assignment(ASTNode** children);
ASTNode* semantic_action_plus_assign(ASTNode** children);
ASTNode* semantic_action_minus_assign(ASTNode** children);
ASTNode* semantic_action_write_statement(ASTNode** children);
ASTNode* semantic_action_loop_statement(ASTNode** children);
ASTNode* semantic_action_binary_add(ASTNode** children);      // For E -> E + T
ASTNode* semantic_action_binary_multiply(ASTNode** children); // For T -> T * F

#endif // PARSER_H

