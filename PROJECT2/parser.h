#ifndef PARSER_H
#define PARSER_H

#include "lexer.h" // For TokenType and Token struct
#include <stdbool.h>
#include <stdlib.h> // For size_t
#include "bigint.h"

// Forward declarations for AST nodes
struct ASTNode;
typedef struct ASTNode ASTNode;

// Maximum number of grammar productions (adjust as needed for your grammar)
#define MAX_PRODUCTIONS 50
// Maximum number of non-terminals (adjust based on your grammar)
#define MAX_NON_TERMINALS 30
// Maximum number of LR(1) states/item sets
#define MAX_STATES 500 // Can grow quite large for complex grammars
// Maximum number of symbols (terminals + non-terminals)
// Ensure this is large enough to cover all TokenType values plus all NonTerminalType values
#define MAX_SYMBOLS_TOTAL (TOKEN_ERROR + 1 + NUM_NON_TERMINALS_DEFINED) // Max terminal ID + 1, plus max non-terminal ID



// Enumeration for Non-Terminal IDs
// Start from a value higher than any TokenType to avoid clashes
typedef enum {
    NT_PROGRAM = 1000, // Make sure these don't overlap with TOKEN_ enums
    NT_S_PRIME,        // Augmented start symbol S' -> Program EOF
    NT_STATEMENT_LIST,
    NT_STATEMENT,
    NT_DECLARATION,
    NT_ASSIGNMENT,
    NT_INCREMENT,
    NT_DECREMENT,
    NT_WRITE_STATEMENT,
    NT_OUTPUT_LIST,
    NT_LIST_ELEMENT,
    NT_LOOP_STATEMENT,
    NT_CODE_BLOCK,
    NT_INT_VALUE, // NEW: Non-terminal for integer values (constants or variables)
    NUM_NON_TERMINALS_DEFINED // Keep this as the last entry, indicates total defined non-terminals
} NonTerminalType;


// Structure for a grammar symbol (terminal or non-terminal)
typedef enum {
    SYMBOL_TERMINAL,
    SYMBOL_NONTERMINAL
} SymbolType;

typedef struct GrammarSymbol {
    SymbolType type;
    int id;           // TokenType for terminals, NonTerminalType for non-terminals
    char* name;       // String representation of the symbol (e.g., "ID", "Program")
} GrammarSymbol;

// Abstract Syntax Tree (AST) Node Types
// Explicitly assign values to prevent overlap with TokenType and NonTerminalType
typedef enum {
    AST_PROGRAM = 2000, // Start AST node types from a distinct high value
    AST_STATEMENT_LIST,
    AST_STATEMENT,
    AST_DECLARATION,
    AST_ASSIGNMENT,
    AST_INCREMENT,
    AST_DECREMENT,
    AST_WRITE_STATEMENT,
    AST_OUTPUT_LIST,
    AST_LIST_ELEMENT,
    AST_LOOP_STATEMENT,
    AST_CODE_BLOCK,
    AST_IDENTIFIER,
    AST_INTEGER_LITERAL, // This node will now hold a BigInt directly
    AST_STRING_LITERAL,
    AST_NEWLINE,
    AST_INT_VALUE, // AST node for expressions (representing integer or identifier value)
    AST_KEYWORD,   // Generic keyword/punctuation node for AST
    AST_ERROR_NODE_TYPE // Renamed from AST_ERROR to avoid potential direct name clashes
} ASTNodeType;


// Structure for an AST Node
struct ASTNode {
    ASTNodeType type;
    SourceLocation location; // Location from the token that formed this node

    // Generic child nodes for compound structures
    ASTNode** children;
    int num_children;
    int children_capacity;

    // Specific data for different node types
    union {
        // For AST_IDENTIFIER
        struct {
            char* name; // Identifier name (e.g., "myVar")
            int symbol_table_index; // Index in the symbol table, if applicable
        } identifier;

        // For AST_INTEGER_LITERAL
        BigInt integer; // Store BigInt by value, not pointer

        // For AST_STRING_LITERAL
        char* string_value;

        // For AST_LOOP_STATEMENT (e.g., repeat N times { ... })
        struct {
            ASTNode* count_expr; // The N in 'repeat N times' (should be integer literal or identifier)
            ASTNode* body;       // The statement or code block to repeat
        } loop;

        // For AST_KEYWORD (optional: store keyword lexeme if needed for debugging/display)
        char* keyword_lexeme; // Store the actual keyword string (e.g., "write", ";")

        // Add more unions for other node-specific data
    } data;
};

// Function pointer for semantic actions
typedef ASTNode* (*SemanticAction)(ASTNode** children);

// Structure for a production rule
typedef struct Production {
    GrammarSymbol* left_symbol;
    GrammarSymbol** right_symbols; // Pointers to grammar symbols on the RHS
    int right_count;
    int production_id; // Unique ID for this production (0-indexed)
    SemanticAction semantic_action; // Pointer to the semantic action function
} Production;

// Structure for the entire grammar
typedef struct Grammar {
    Production* productions;      // Pointer to an array of productions
    int production_count;
    GrammarSymbol** terminals;    // Pointer to an array of terminal symbols (indexed by TokenType)
    int terminal_count;           // Represents max_token_type_id + 1
    GrammarSymbol** non_terminals; // Pointer to an array of non-terminal symbols (indexed by NonTerminalType)
    int non_terminal_count;       // Represents max_non_terminal_type_id + 1
    GrammarSymbol* start_symbol; // Augmented start symbol (S')
} Grammar;


// Bitset for terminals (for FIRST/FOLLOW sets)
typedef unsigned long long TerminalSet; // Enough for up to 64 terminals

// LR(1) Item structure
typedef struct Item {
    int production_idx; // Index of the production rule (e.g., A -> alpha . beta, production_idx is for A -> alpha beta)
    int dot_pos;        // Position of the dot in the right-hand side (0-indexed)
    TokenType lookahead; // The lookahead terminal for LR(1)
} Item;

// LR(1) Item Set structure
typedef struct ItemSet {
    Item items[MAX_PRODUCTIONS * 4]; // Increased heuristic for max items in a set
    int count;
    int id; // Unique ID for this item set (state number)
} ItemSet;

// List of all LR(1) Item Sets (Canonical Collection)
typedef struct ItemSetList {
    ItemSet sets[MAX_STATES];
    int count;
} ItemSetList;

// Parsing table action types
typedef enum {
    ACTION_SHIFT,
    ACTION_REDUCE,
    ACTION_ACCEPT,
    ACTION_ERROR
} ActionType;

// Parsing table entry
typedef struct ActionEntry {
    ActionType type;
    int target_state_or_production_id; // State for SHIFT, Production ID for REDUCE
} ActionEntry;

typedef struct {
    int state;
    ASTNode* ast_node; // AST node associated with this symbol
} StackEntry;


// --- Global Variables (Declared in parser.c, externed here) ---
// These are now declared as global variables to be accessed across files
extern TerminalSet firstSetsForNonTerminals[NUM_NON_TERMINALS_DEFINED]; // Corrected array size
extern TerminalSet followSetsForNonTerminals[NUM_NON_TERMINALS_DEFINED]; // Corrected array size
extern ActionEntry** action_table; // [state][terminal_id]
extern int** goto_table;           // [state][non_terminal_id]
extern int num_states;
extern bool nullable_status[NUM_NON_TERMINALS_DEFINED]; // Corrected array size
extern ItemSetList canonical_collection; // Global canonical collection

// --- Function Declarations for Parser ---

// AST Node Creation and Management
ASTNode* create_ast_node(ASTNodeType type, SourceLocation loc);
void add_child_to_ast_node(ASTNode* parent, ASTNode* child);
ASTNode* create_ast_leaf_from_token(const Token* token);
void print_ast_node(const ASTNode* node, int indent);
void free_ast_node(ASTNode* node);

// Semantic Action Functions (forward declarations)
ASTNode* semantic_action_passthrough(ASTNode** children);
ASTNode* semantic_action_program(ASTNode** children);
ASTNode* semantic_action_statement_list_multi(ASTNode** children);
ASTNode* semantic_action_statement_list_single(ASTNode** children);
ASTNode* semantic_action_statement_with_semicolon(ASTNode** children);
ASTNode* semantic_action_declaration(ASTNode** children);
ASTNode* semantic_action_assignment(ASTNode** children);
ASTNode* semantic_action_increment(ASTNode** children);
ASTNode* semantic_action_decrement(ASTNode** children);
ASTNode* semantic_action_write_statement(ASTNode** children);
ASTNode* semantic_action_output_list_multi(ASTNode** children);
ASTNode* semantic_action_output_list_single(ASTNode** children);
ASTNode* semantic_action_list_element(ASTNode** children);
ASTNode* semantic_action_loop_statement_single(ASTNode** children);
ASTNode* semantic_action_loop_statement_block(ASTNode** children);
ASTNode* semantic_action_code_block(ASTNode** children);
ASTNode* semantic_action_int_value_from_integer(ASTNode** children); // NEW
ASTNode* semantic_action_int_value_from_identifier(ASTNode** children); // NEW

// Core Parser Functions
void compute_nullable_set(const Grammar* grammar, bool nullable[NUM_NON_TERMINALS_DEFINED]);
void compute_first_sets(const Grammar* grammar);
void compute_follow_sets(const Grammar* grammar);
void create_lr1_sets(const Grammar* grammar);
void build_parsing_tables(const Grammar* grammar, const ItemSetList* canonical_collection_ptr, bool nullable[NUM_NON_TERMINALS_DEFINED]);
ASTNode* parse(const Grammar* grammar, Token* tokens, int num_tokens);
void free_parsing_tables();

#endif // PARSER_H
