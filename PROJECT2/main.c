#include <stdio.h>
#include "lexer.h"
#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// External declarations for global variables from parser.c
extern TerminalSet firstSetsForNonTerminals[MAX_NON_TERMINALS];
extern TerminalSet followSetsForNonTerminals[MAX_NON_TERMINALS];
extern ActionEntry** action_table;
extern int** goto_table;
extern int num_states;
extern bool nullable_status[MAX_NON_TERMINALS];
extern ItemSetList canonical_collection;

// --- Helper Functions for Grammar Definition ---

// Creates a new GrammarSymbol for a terminal
GrammarSymbol* create_terminal(int id, const char* name) {
    GrammarSymbol* s = (GrammarSymbol*)malloc(sizeof(GrammarSymbol));
    if (!s) { fprintf(stderr, "Memory allocation failed for terminal symbol.\n"); exit(EXIT_FAILURE); }
    s->type = SYMBOL_TERMINAL;
    s->id = id;
    s->name = strdup(name);
    if (!s->name) { fprintf(stderr, "Memory allocation failed for terminal name.\n"); free(s); exit(EXIT_FAILURE); }
    return s;
}

// Creates a new GrammarSymbol for a non-terminal
GrammarSymbol* create_non_terminal(int id, const char* name) {
    GrammarSymbol* s = (GrammarSymbol*)malloc(sizeof(GrammarSymbol));
    if (!s) { fprintf(stderr, "Memory allocation failed for non-terminal symbol.\n"); exit(EXIT_FAILURE); }
    s->type = SYMBOL_NONTERMINAL;
    s->id = id;
    s->name = strdup(name);
    if (!s->name) { fprintf(stderr, "Memory allocation failed for non-terminal name.\n"); free(s); exit(EXIT_FAILURE); }
    return s;
}

// Creates a Production rule
Production create_production(GrammarSymbol* left, GrammarSymbol** right, int right_count, int id, ASTNode* (*semantic_action_func)(ASTNode**)) {
    Production p;
    p.left_symbol = left;
    p.right_symbols = (GrammarSymbol**)malloc(right_count * sizeof(GrammarSymbol*));
    if (!p.right_symbols && right_count > 0) {
        fprintf(stderr, "Memory allocation failed for production right symbols.\n");
        exit(EXIT_FAILURE);
    }
    if (right_count > 0) {
        memcpy(p.right_symbols, right, right_count * sizeof(GrammarSymbol*));
    }
    p.right_count = right_count;
    p.production_id = id;
    p.semantic_action = semantic_action_func;
    return p;
}

// Function to free grammar symbols and productions
void free_grammar_data(Grammar* grammar) {
    if (!grammar) return;

    // Free production right-hand side arrays
    for (int i = 0; i < grammar->production_count; ++i) {
        free(grammar->productions[i].right_symbols);
    }
    // Note: The `productions` array itself is typically stack-allocated in main
    // or freed by the caller if dynamically allocated.

    // Free terminal symbols
    for (int i = 0; i < grammar->terminal_count; ++i) {
        if (grammar->terminals[i]) {
            free(grammar->terminals[i]->name);
            free(grammar->terminals[i]);
        }
    }
    // Note: The `terminals` array itself is typically stack-allocated in main.

    // Free non-terminal symbols
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        if (grammar->non_terminals[i]) {
            free(grammar->non_terminals[i]->name);
            free(grammar->non_terminals[i]);
        }
    }
    // Note: The `non_terminals` array itself is typically stack-allocated in main.
}

// Function to free canonical collection
void free_canonical_collection(ItemSetList* list) {
    if (!list) return;
    for (int i = 0; i < list->count; ++i) {
        if (list->sets[i]) {
            item_set_free(list->sets[i]); // Frees items array within the ItemSet
            free(list->sets[i]); // Frees the ItemSet struct itself
        }
    }
    free(list->sets); // Frees the array of ItemSet pointers
    list->sets = NULL;
    list->count = 0;
    list->capacity = 0;
}


int main(int argc, char *argv[]) {

    // Add check for command line argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // --- 1. Define Grammar Symbols ---
    // Non-terminals
    GrammarSymbol* s_prime = create_non_terminal(NT_S_PRIME, "S'");
    GrammarSymbol* program_nt = create_non_terminal(NT_PROGRAM, "Program");
    GrammarSymbol* stmt_list_nt = create_non_terminal(NT_STATEMENT_LIST, "StatementList");
    GrammarSymbol* statement_nt = create_non_terminal(NT_STATEMENT, "Statement");
    GrammarSymbol* assignment_nt = create_non_terminal(NT_ASSIGNMENT, "Assignment");
    GrammarSymbol* write_stmt_nt = create_non_terminal(NT_WRITE_STATEMENT, "WriteStatement");
    GrammarSymbol* loop_stmt_nt = create_non_terminal(NT_LOOP_STATEMENT, "LoopStatement");
    GrammarSymbol* code_block_nt = create_non_terminal(NT_CODE_BLOCK, "CodeBlock");
    GrammarSymbol* e_nt = create_non_terminal(NT_E, "E"); // Expression
    GrammarSymbol* t_nt = create_non_terminal(NT_T, "T"); // Term
    GrammarSymbol* f_nt = create_non_terminal(NT_F, "F"); // Factor

    // Terminals (IDs should match TokenType from lexer.h for consistency)
    GrammarSymbol* semicolon_t = create_terminal(TOKEN_EOL, ";"); // Using TOKEN_EOL for semicolon
    GrammarSymbol* integer_t = create_terminal(TOKEN_INTEGER, "INTEGER"); // Using TOKEN_INTEGER
    GrammarSymbol* write_t = create_terminal(TOKEN_WRITE, "WRITE");
    GrammarSymbol* repeat_t = create_terminal(TOKEN_REPEAT, "REPEAT");
    GrammarSymbol* and_t = create_terminal(TOKEN_AND, "AND");
    GrammarSymbol* times_t = create_terminal(TOKEN_TIMES, "TIMES");
    GrammarSymbol* newline_t = create_terminal(TOKEN_NEWLINE, "NEWLINE");
    GrammarSymbol* identifier_t = create_terminal(TOKEN_IDENTIFIER, "IDENTIFIER");
    GrammarSymbol* string_t = create_terminal(TOKEN_STRING, "STRING");
    GrammarSymbol* assign_t = create_terminal(TOKEN_ASSIGN, ":="); // Changed to :=
    GrammarSymbol* plus_assign_t = create_terminal(TOKEN_PLUS_ASSIGN, "+=");
    GrammarSymbol* minus_assign_t = create_terminal(TOKEN_MINUS_ASSIGN, "-=");
    GrammarSymbol* lbrace_t = create_terminal(TOKEN_OPENB, "{");
    GrammarSymbol* rbrace_t = create_terminal(TOKEN_CLOSEB, "}");
    GrammarSymbol* plus_t = create_terminal(TOKEN_PLUS, "+");
    GrammarSymbol* star_t = create_terminal(TOKEN_STAR, "*");
    GrammarSymbol* lparen_t = create_terminal(TOKEN_LPAREN, "(");
    GrammarSymbol* rparen_t = create_terminal(TOKEN_RPAREN, ")");
    GrammarSymbol* eof_t = create_terminal(TOKEN_EOF, "$");

    // Array of all terminals
    GrammarSymbol* terminals[] = {
        semicolon_t, integer_t, write_t, repeat_t, and_t, times_t, newline_t,
        identifier_t, string_t, assign_t, plus_assign_t, minus_assign_t,
        lbrace_t, rbrace_t, plus_t, star_t, lparen_t, rparen_t, eof_t
    };
    int terminal_count = sizeof(terminals) / sizeof(terminals[0]);

    // Array of all non-terminals
    GrammarSymbol* non_terminals[] = {
        program_nt, stmt_list_nt, statement_nt, assignment_nt,
        write_stmt_nt, loop_stmt_nt, code_block_nt,
        e_nt, t_nt, f_nt, s_prime
    };
    int non_terminal_count = sizeof(non_terminals) / sizeof(non_terminals[0]);

    // --- 2. Define Productions ---
    // Increased size to accommodate new arithmetic productions
    Production productions[21]; // Adjust size as needed, production 20 added.

    // P0: S' -> Program (Augmented Start Symbol)
    GrammarSymbol* p0_rhs[] = {program_nt};
    productions[0] = create_production(s_prime, p0_rhs, 1, 0, semantic_action_program);

    // P1: Program -> StatementList
    GrammarSymbol* p1_rhs[] = {stmt_list_nt};
    productions[1] = create_production(program_nt, p1_rhs, 1, 1, semantic_action_passthrough);

    // P2: StatementList -> Statement SEMICOLON StatementList
    GrammarSymbol* p2_rhs[] = {statement_nt, semicolon_t, stmt_list_nt};
    productions[2] = create_production(stmt_list_nt, p2_rhs, 3, 2, semantic_action_statement_list_multi);

    // P3: StatementList -> Statement
    GrammarSymbol* p3_rhs[] = {statement_nt};
    productions[3] = create_production(stmt_list_nt, p3_rhs, 1, 3, semantic_action_statement_list_single);

    // P4: Statement -> Assignment
    GrammarSymbol* p4_rhs[] = {assignment_nt};
    productions[4] = create_production(statement_nt, p4_rhs, 1, 4, semantic_action_passthrough);
    // P5: Statement -> WriteStatement
    GrammarSymbol* p5_rhs[] = {write_stmt_nt};
    productions[5] = create_production(statement_nt, p5_rhs, 1, 5, semantic_action_passthrough);
    // P6: Statement -> LoopStatement
    GrammarSymbol* p6_rhs[] = {loop_stmt_nt};
    productions[6] = create_production(statement_nt, p6_rhs, 1, 6, semantic_action_passthrough);


    // P7: Assignment -> IDENTIFIER ASSIGN E
    GrammarSymbol* p7_rhs[] = {identifier_t, assign_t, e_nt};
    productions[7] = create_production(assignment_nt, p7_rhs, 3, 7, semantic_action_assignment);
    // P8: Assignment -> IDENTIFIER PLUS_ASSIGN E
    GrammarSymbol* p8_rhs[] = {identifier_t, plus_assign_t, e_nt};
    productions[8] = create_production(assignment_nt, p8_rhs, 3, 8, semantic_action_plus_assign);
    // P9: Assignment -> IDENTIFIER MINUS_ASSIGN E
    GrammarSymbol* p9_rhs[] = {identifier_t, minus_assign_t, e_nt};
    productions[9] = create_production(assignment_nt, p9_rhs, 3, 9, semantic_action_minus_assign);

    // P10: E -> E PLUS T (Added for arithmetic)
    GrammarSymbol* p10_rhs[] = {e_nt, plus_t, t_nt};
    productions[10] = create_production(e_nt, p10_rhs, 3, 10, semantic_action_binary_add);

    // P11: E -> T
    GrammarSymbol* p11_rhs[] = {t_nt};
    productions[11] = create_production(e_nt, p11_rhs, 1, 11, semantic_action_passthrough);

    // P12: T -> T STAR F (Added for arithmetic)
    GrammarSymbol* p12_rhs[] = {t_nt, star_t, f_nt};
    productions[12] = create_production(t_nt, p12_rhs, 3, 12, semantic_action_binary_multiply);

    // P13: T -> F
    GrammarSymbol* p13_rhs[] = {f_nt};
    productions[13] = create_production(t_nt, p13_rhs, 1, 13, semantic_action_passthrough);

    // P14: F -> IDENTIFIER
    GrammarSymbol* p14_rhs[] = {identifier_t};
    productions[14] = create_production(f_nt, p14_rhs, 1, 14, semantic_action_id);

    // P15: F -> INTEGER (Using INTEGER for number literals)
    GrammarSymbol* p15_rhs[] = {integer_t};
    productions[15] = create_production(f_nt, p15_rhs, 1, 15, semantic_action_number);

    // P16: F -> LPAREN E RPAREN
    GrammarSymbol* p16_rhs[] = {lparen_t, e_nt, rparen_t};
    productions[16] = create_production(f_nt, p16_rhs, 3, 16, semantic_action_paren_expr);

    // P17: WriteStatement -> WRITE E
    GrammarSymbol* p17_rhs[] = {write_t, e_nt};
    productions[17] = create_production(write_stmt_nt, p17_rhs, 2, 17, semantic_action_write_statement);

    // P18: LoopStatement -> REPEAT E TIMES CodeBlock
    GrammarSymbol* p18_rhs[] = {repeat_t, e_nt, times_t, code_block_nt};
    productions[18] = create_production(loop_stmt_nt, p18_rhs, 4, 18, semantic_action_loop_statement);

    // P19: CodeBlock -> LBRACE StatementList RBRACE
    GrammarSymbol* p19_rhs[] = {lbrace_t, stmt_list_nt, rbrace_t};
    productions[19] = create_production(code_block_nt, p19_rhs, 3, 19, semantic_action_passthrough); // Semantic action will create code_block node

    // P20: F -> STRING (Added for string literals)
    GrammarSymbol* p20_rhs[] = {string_t};
    productions[20] = create_production(f_nt, p20_rhs, 1, 20, semantic_action_string);


    Grammar grammar = {
        .productions = productions,
        .production_count = sizeof(productions) / sizeof(productions[0]),
        .terminals = terminals,
        .terminal_count = terminal_count,
        .non_terminals = non_terminals,
        .non_terminal_count = non_terminal_count,
        .start_symbol = s_prime // S' is the augmented start symbol
    };

    // --- Test Input ---
	char *input_filename = argv[1]; // NAME OF THE CODE FILE
	FILE *inputFile = fopen(input_filename, "r");
    if (!inputFile) {
        fprintf(stderr, "Error: Could not open input file '%s'\n", input_filename);
        // Free dynamically allocated grammar symbols before exiting
        free_grammar_data(&grammar);
        return EXIT_FAILURE;
    }

    int num_test_tokens = 0; // Initialize to 0
    // Pass the address of num_test_tokens to lexer to get the actual count
    Token* tokens = lexer(inputFile, input_filename, &num_test_tokens);
    fclose(inputFile); // Close the input file after lexing

    if (!tokens) {
        fprintf(stderr, "Lexical analysis failed or no tokens generated.\n");
        // Free dynamically allocated grammar symbols before exiting
        free_grammar_data(&grammar);
        return EXIT_FAILURE;
    }

    // Now, num_test_tokens holds the actual number of tokens
    printf("Total tokens lexed: %d\n", num_test_tokens);
    fflush(stdout);

    // --- 4. Compute FIRST and FOLLOW Sets ---
    printf("Computing FIRST and FOLLOW sets...\n");
    fflush(stdout);
    compute_nullable_set(&grammar, nullable_status);
    compute_first_sets(&grammar);
    compute_follow_sets(&grammar);
    printf("FIRST and FOLLOW sets computed.\n");
    fflush(stdout);

    // --- 5. Generate LR(1) Item Sets (Canonical Collection) ---
    printf("Generating LR(1) item sets...\n");
    fflush(stdout);
    create_lr1_sets(&grammar);
    printf("LR(1) item sets generated. Total states: %d\n", canonical_collection.count);
    fflush(stdout);

    // --- 6. Build Parsing Tables ---
    printf("Building parsing tables...\n");
    fflush(stdout);
    build_parsing_tables(&grammar, &canonical_collection, nullable_status);
    printf("Parsing tables built.\n");
    fflush(stdout);

    // --- 7. Perform Parsing ---
    printf("\nAttempting to parse sample tokens...\n");
    fflush(stdout);
    ASTNode* root_ast = parse(&grammar, tokens, num_test_tokens);

    // --- 8. Inspect AST ---
    if (root_ast) {
        printf("\n--- Parsing Successful! Generated AST: ---\n");
        print_ast_node(root_ast, 0);
        free_ast_node(root_ast); // Free the entire AST
    } else {
        fprintf(stderr, "\n--- Parsing Failed! ---\n");
    }

    // --- 9. Cleanup ---
    printf("\nCleaning up...\n");
    fflush(stdout);

    free(tokens); // Free the tokens array allocated by lexer

    free_parsing_tables(); // Free action and goto tables
    free_canonical_collection(&canonical_collection); // Free LR(1) item sets
    free_grammar_data(&grammar); // Free grammar symbols and production RHS arrays

    return 0;
}
