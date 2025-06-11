#include <stdio.h>
#include "lexer.h"
#include "parser.h" // Include the new parser header
#include "interpreter.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// External declarations for global variables from parser.c
// These are now defined in parser.c and declared here as extern
extern TerminalSet firstSetsForNonTerminals[NUM_NON_TERMINALS_DEFINED];
extern TerminalSet followSetsForNonTerminals[NUM_NON_TERMINALS_DEFINED];
extern ActionEntry** action_table;
extern int** goto_table;
extern int num_states;
extern bool nullable_status[NUM_NON_TERMINALS_DEFINED];
extern ItemSetList canonical_collection; // Global canonical collection

// --- Helper Functions for Grammar Definition ---

// Creates a new GrammarSymbol for a terminal
GrammarSymbol* create_terminal(int id, const char* name) {
    GrammarSymbol* s = (GrammarSymbol*)malloc(sizeof(GrammarSymbol));
    if (!s) { fprintf(stderr, "Memory allocation failed for terminal symbol.\n"); exit(EXIT_FAILURE); }
    s->type = SYMBOL_TERMINAL;
    s->id = id;
    s->name = strdup(name);
    if (!s->name) { fprintf(stderr, "Memory allocation failed for terminal name.\\n"); free(s); exit(EXIT_FAILURE); }
    return s;
}

// Creates a new GrammarSymbol for a non-terminal
GrammarSymbol* create_non_terminal(int id, const char* name) {
    GrammarSymbol* s = (GrammarSymbol*)malloc(sizeof(GrammarSymbol));
    if (!s) { fprintf(stderr, "Memory allocation failed for non-terminal symbol.\n"); exit(EXIT_FAILURE); }
    s->type = SYMBOL_NONTERMINAL;
    s->id = id;
    s->name = strdup(name);
    if (!s->name) { fprintf(stderr, "Memory allocation failed for non-terminal name.\\n"); free(s); exit(EXIT_FAILURE); }
    return s;
}

// Creates a Production rule
Production create_production(GrammarSymbol* left, GrammarSymbol** right, int right_count, int id, ASTNode* (*semantic_action_func)(ASTNode**)) {
    Production p;
    p.left_symbol = left;
    // Allocate memory for right_symbols only if there are symbols
    p.right_symbols = NULL; // Initialize to NULL
    if (right_count > 0) {
        p.right_symbols = (GrammarSymbol**)malloc(right_count * sizeof(GrammarSymbol*));
        if (!p.right_symbols) {
            fprintf(stderr, "Memory allocation failed for production right symbols.\\n");
            exit(EXIT_FAILURE);
        }
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

    // Free individual GrammarSymbol names and structures for terminals
    // Iterate up to the true_terminal_count used during grammar definition
    // Note: grammar->terminals is itself a dynamically allocated array of pointers
    if (grammar->terminals) {
        for (int i = 0; i < grammar->terminal_count; ++i) {
            if (grammar->terminals[i]) { // Check if symbol was actually created and assigned
                free(grammar->terminals[i]->name);
                free(grammar->terminals[i]);
                // Do NOT set grammar->terminals[i] to NULL here, as we're about to free the array itself.
            }
        }
        free(grammar->terminals);
        grammar->terminals = NULL;
    }


    // Free individual GrammarSymbol names and structures for non-terminals
    // Iterate up to the true_non_terminal_count (NUM_NON_TERMINALS_DEFINED)
    // Note: grammar->non_terminals is itself a dynamically allocated array of pointers
    if (grammar->non_terminals) {
        for (int i = 0; i < grammar->non_terminal_count; ++i) {
            if (grammar->non_terminals[i]) { // Check if symbol was actually created and assigned
                free(grammar->non_terminals[i]->name);
                free(grammar->non_terminals[i]);
                // Do NOT set grammar->non_terminals[i] to NULL here.
            }
        }
        free(grammar->non_terminals);
        grammar->non_terminals = NULL;
    }

    // Free production right-hand side arrays
    // The `productions` member of Grammar is now a pointer to an array
    // We assume this array itself (`productions_array` in main) is stack-allocated
    // and only its dynamically allocated `right_symbols` need freeing.
    if (grammar->productions) { // Check if productions pointer is valid
        for (int i = 0; i < grammar->production_count; ++i) {
            // Check if right_symbols was allocated for this production
            if (grammar->productions[i].right_symbols) {
                free(grammar->productions[i].right_symbols);
                grammar->productions[i].right_symbols = NULL; // Prevent double free
            }
        }
        // If `grammar->productions` was also dynamically allocated (e.g., `malloc` for `productions_array`),
        // then `free(grammar->productions);` would go here.
        // But since `productions_array` is local, it's not freed here.
    }
}


int main(int argc, char *argv[]) {

    // Add check for command line argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("DEBUG: AST_PROGRAM enum value: %d\n", AST_PROGRAM);

    // --- 1. Define Grammar Symbols ---
    // Non-terminals
    GrammarSymbol* s_prime = create_non_terminal(NT_S_PRIME, "S'"); // Augmented start symbol
    GrammarSymbol* program_nt = create_non_terminal(NT_PROGRAM, "Program");
    GrammarSymbol* stmt_list_nt = create_non_terminal(NT_STATEMENT_LIST, "StatementList");
	GrammarSymbol* declaration_nt = create_non_terminal(NT_DECLARATION, "Declaration");
	GrammarSymbol* decrement_nt = create_non_terminal(NT_DECREMENT, "Decrement");
	GrammarSymbol* increment_nt = create_non_terminal(NT_INCREMENT, "Increment");
    GrammarSymbol* statement_nt = create_non_terminal(NT_STATEMENT, "Statement");
    GrammarSymbol* assignment_nt = create_non_terminal(NT_ASSIGNMENT, "Assignment");
    GrammarSymbol* write_stmt_nt = create_non_terminal(NT_WRITE_STATEMENT, "WriteStatement");
	GrammarSymbol* output_list_nt = create_non_terminal(NT_OUTPUT_LIST, "OutputList");
	GrammarSymbol* list_element_nt = create_non_terminal(NT_LIST_ELEMENT, "ListElement");
    GrammarSymbol* loop_stmt_nt = create_non_terminal(NT_LOOP_STATEMENT, "LoopStatement");
    GrammarSymbol* code_block_nt = create_non_terminal(NT_CODE_BLOCK, "CodeBlock");
    GrammarSymbol* int_value_nt = create_non_terminal(NT_INT_VALUE, "Int_Value"); // NEW


    // Dynamically allocate and populate the non_terminals map, indexed by their ID
    // This array will hold pointers to the GrammarSymbol structs
    GrammarSymbol** all_non_terminals_map = (GrammarSymbol**)calloc(NUM_NON_TERMINALS_DEFINED, sizeof(GrammarSymbol*));
    if (!all_non_terminals_map) {
        fprintf(stderr, "Memory allocation failed for all_non_terminals_map.\n");
        exit(EXIT_FAILURE);
    }

    // Populate the map using their IDs as indices. Ensure IDs are within bounds.
    if (s_prime->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[s_prime->id] = s_prime;
    if (program_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[program_nt->id] = program_nt;
    if (stmt_list_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[stmt_list_nt->id] = stmt_list_nt;
    if (declaration_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[declaration_nt->id] = declaration_nt;
    if (decrement_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[decrement_nt->id] = decrement_nt;
    if (increment_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[increment_nt->id] = increment_nt;
    if (statement_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[statement_nt->id] = statement_nt;
    if (assignment_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[assignment_nt->id] = assignment_nt;
    if (write_stmt_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[write_stmt_nt->id] = write_stmt_nt;
    if (output_list_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[output_list_nt->id] = output_list_nt;
    if (list_element_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[list_element_nt->id] = list_element_nt;
    if (loop_stmt_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[loop_stmt_nt->id] = loop_stmt_nt;
    if (code_block_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[code_block_nt->id] = code_block_nt;
    if (int_value_nt->id < NUM_NON_TERMINALS_DEFINED) all_non_terminals_map[int_value_nt->id] = int_value_nt; // NEW


    int true_non_terminal_count = NUM_NON_TERMINALS_DEFINED;

    // Terminals (IDs should match TokenType from lexer.h for consistency)
    // Dynamically allocate this array so its memory can be freed via the Grammar struct
    GrammarSymbol** all_terminals_map = (GrammarSymbol**)calloc(NUM_TOKEN_TYPES, sizeof(GrammarSymbol*)); // Use NUM_TOKEN_TYPES
    if (!all_terminals_map) {
        fprintf(stderr, "Memory allocation failed for all_terminals_map.\n");
        exit(EXIT_FAILURE);
    }

    // Assign terminal symbols to their respective indices in the map
    all_terminals_map[TOKEN_EOF] = create_terminal(TOKEN_EOF, "$");
    all_terminals_map[TOKEN_IDENTIFIER] = create_terminal(TOKEN_IDENTIFIER, "IDENTIFIER");
    all_terminals_map[TOKEN_WRITE] = create_terminal(TOKEN_WRITE, "WRITE");
    all_terminals_map[TOKEN_AND] = create_terminal(TOKEN_AND, "AND");
    all_terminals_map[TOKEN_REPEAT] = create_terminal(TOKEN_REPEAT, "REPEAT");
    all_terminals_map[TOKEN_NEWLINE] = create_terminal(TOKEN_NEWLINE, "NEWLINE");
    all_terminals_map[TOKEN_TIMES] = create_terminal(TOKEN_TIMES, "TIMES");
    all_terminals_map[TOKEN_NUMBER] = create_terminal(TOKEN_NUMBER, "NUMBER");
    all_terminals_map[TOKEN_INTEGER] = create_terminal(TOKEN_INTEGER, "INTEGER");
    all_terminals_map[TOKEN_ASSIGN] = create_terminal(TOKEN_ASSIGN, ":=");
    all_terminals_map[TOKEN_PLUS_ASSIGN] = create_terminal(TOKEN_PLUS_ASSIGN, "+=");
    all_terminals_map[TOKEN_MINUS_ASSIGN] = create_terminal(TOKEN_MINUS_ASSIGN, "-=");
    all_terminals_map[TOKEN_OPENB] = create_terminal(TOKEN_OPENB, "{");
    all_terminals_map[TOKEN_CLOSEB] = create_terminal(TOKEN_CLOSEB, "}");
    all_terminals_map[TOKEN_STRING] = create_terminal(TOKEN_STRING, "STRING");
    all_terminals_map[TOKEN_EOL] = create_terminal(TOKEN_EOL, ";");
    all_terminals_map[TOKEN_LPAREN] = create_terminal(TOKEN_LPAREN, "(");
    all_terminals_map[TOKEN_RPAREN] = create_terminal(TOKEN_RPAREN, ")");
    all_terminals_map[TOKEN_ERROR] = create_terminal(TOKEN_ERROR, "ERROR"); // Although ERROR token, useful for mapping

    int true_terminal_count = NUM_TOKEN_TYPES; // Use NUM_TOKEN_TYPES for consistency

    // Production rules - this array will be copied, and its pointer assigned to grammar.productions
    Production productions_array[MAX_PRODUCTIONS];
    int prod_idx = 0;

    // --- 2. Define Productions with Semantic Actions ---
    // Make sure to use the semantic action functions from parser.c

// Augmented Grammar Start: S' -> Program EOF (always production 0)
GrammarSymbol* s_prime_rhs[] = {program_nt, all_terminals_map[TOKEN_EOF]};
productions_array[prod_idx] = create_production(s_prime, s_prime_rhs, 2, prod_idx, semantic_action_program); prod_idx++;

// R0: <program> -> <statement_list>
GrammarSymbol* program_rhs[] = {stmt_list_nt};
productions_array[prod_idx] = create_production(program_nt, program_rhs, 1, prod_idx, semantic_action_passthrough); prod_idx++;

// R1: <statement_list> -> <statement_list> <statement>
GrammarSymbol* stmt_list_multi_rhs[] = {stmt_list_nt, statement_nt};
productions_array[prod_idx] = create_production(stmt_list_nt, stmt_list_multi_rhs, 2, prod_idx, semantic_action_statement_list_multi); prod_idx++;

// R2: <statement_list> -> <statement>
GrammarSymbol* stmt_list_single_rhs[] = {statement_nt};
productions_array[prod_idx] = create_production(stmt_list_nt, stmt_list_single_rhs, 1, prod_idx, semantic_action_statement_list_single); prod_idx++;

// R3: <statement> -> <assignment> ;
GrammarSymbol* stmt_assign_rhs[] = {assignment_nt, all_terminals_map[TOKEN_EOL]};
productions_array[prod_idx] = create_production(statement_nt, stmt_assign_rhs, 2, prod_idx, semantic_action_statement_with_semicolon); prod_idx++;

// R4: <statement> -> <declaration> ;
GrammarSymbol* stmt_decl_rhs[] = {declaration_nt, all_terminals_map[TOKEN_EOL]};
productions_array[prod_idx] = create_production(statement_nt, stmt_decl_rhs, 2, prod_idx, semantic_action_statement_with_semicolon); prod_idx++;

// R5: <statement> -> <decrement> ;
GrammarSymbol* stmt_dec_rhs[] = {decrement_nt, all_terminals_map[TOKEN_EOL]};
productions_array[prod_idx] = create_production(statement_nt, stmt_dec_rhs, 2, prod_idx, semantic_action_statement_with_semicolon); prod_idx++;

// R6: <statement> -> <increment> ;
GrammarSymbol* stmt_inc_rhs[] = {increment_nt, all_terminals_map[TOKEN_EOL]};
productions_array[prod_idx] = create_production(statement_nt, stmt_inc_rhs, 2, prod_idx, semantic_action_statement_with_semicolon); prod_idx++;

// R7: <statement> -> <write_statement> ;
GrammarSymbol* stmt_write_rhs[] = {write_stmt_nt, all_terminals_map[TOKEN_EOL]};
productions_array[prod_idx] = create_production(statement_nt, stmt_write_rhs, 2, prod_idx, semantic_action_statement_with_semicolon); prod_idx++;

// R8: <statement> -> <loop_statement>
GrammarSymbol* stmt_loop_rhs[] = {loop_stmt_nt};
productions_array[prod_idx] = create_production(statement_nt, stmt_loop_rhs, 1, prod_idx, semantic_action_passthrough); prod_idx++;

// R9: <declaration> -> number IDENTIFIER
GrammarSymbol* decl_rhs[] = {all_terminals_map[TOKEN_NUMBER], all_terminals_map[TOKEN_IDENTIFIER]};
productions_array[prod_idx] = create_production(declaration_nt, decl_rhs, 2, prod_idx, semantic_action_declaration); prod_idx++;

// R10: <assignment> -> IDENTIFIER := <int_value> // Changed to int_value
GrammarSymbol* assign_rhs[] = {all_terminals_map[TOKEN_IDENTIFIER], all_terminals_map[TOKEN_ASSIGN], int_value_nt};
productions_array[prod_idx] = create_production(assignment_nt, assign_rhs, 3, prod_idx, semantic_action_assignment); prod_idx++;

// R11: <decrement> -> IDENTIFIER -= <int_value> // Changed to int_value
GrammarSymbol* dec_rhs[] = {all_terminals_map[TOKEN_IDENTIFIER], all_terminals_map[TOKEN_MINUS_ASSIGN], int_value_nt};
productions_array[prod_idx] = create_production(decrement_nt, dec_rhs, 3, prod_idx, semantic_action_decrement); prod_idx++;

// R12: <increment> -> IDENTIFIER += <int_value> // Changed to int_value
GrammarSymbol* inc_rhs[] = {all_terminals_map[TOKEN_IDENTIFIER], all_terminals_map[TOKEN_PLUS_ASSIGN], int_value_nt};
productions_array[prod_idx] = create_production(increment_nt, inc_rhs, 3, prod_idx, semantic_action_increment); prod_idx++;

// R13: <write_statement> -> write <output_list>
GrammarSymbol* write_stmt_rhs[] = {all_terminals_map[TOKEN_WRITE], output_list_nt};
productions_array[prod_idx] = create_production(write_stmt_nt, write_stmt_rhs, 2, prod_idx, semantic_action_write_statement); prod_idx++;

// R14: <loop_statement> -> repeat <int_value> times <statement>
GrammarSymbol* loop_stmt_single_rhs[] = {all_terminals_map[TOKEN_REPEAT], int_value_nt, all_terminals_map[TOKEN_TIMES], statement_nt};
productions_array[prod_idx] = create_production(loop_stmt_nt, loop_stmt_single_rhs, 4, prod_idx, semantic_action_loop_statement_single); prod_idx++;

// R15: <loop_statement> -> repeat <int_value> times <code_block>
GrammarSymbol* loop_stmt_block_rhs[] = {all_terminals_map[TOKEN_REPEAT], int_value_nt, all_terminals_map[TOKEN_TIMES], code_block_nt};
productions_array[prod_idx] = create_production(loop_stmt_nt, loop_stmt_block_rhs, 4, prod_idx, semantic_action_loop_statement_block); prod_idx++;

// R16: <code_block> -> { <statement_list> }
GrammarSymbol* code_block_rhs[] = {all_terminals_map[TOKEN_OPENB], stmt_list_nt, all_terminals_map[TOKEN_CLOSEB]};
productions_array[prod_idx] = create_production(code_block_nt, code_block_rhs, 3, prod_idx, semantic_action_code_block); prod_idx++;

// R17: <output_list> -> <output_list> and <list_element>
GrammarSymbol* output_list_multi_rhs[] = {output_list_nt, all_terminals_map[TOKEN_AND], list_element_nt};
productions_array[prod_idx] = create_production(output_list_nt, output_list_multi_rhs, 3, prod_idx, semantic_action_output_list_multi); prod_idx++;

// R18: <output_list> -> <list_element>
GrammarSymbol* output_list_single_rhs[] = {list_element_nt};
productions_array[prod_idx] = create_production(output_list_nt, output_list_single_rhs, 1, prod_idx, semantic_action_output_list_single); prod_idx++;

// NEW: Productions for <int_value>
// R_INT_VALUE_INTEGER: <int_value> -> INTEGER
GrammarSymbol* int_value_int_rhs[] = {all_terminals_map[TOKEN_INTEGER]};
productions_array[prod_idx] = create_production(int_value_nt, int_value_int_rhs, 1, prod_idx, semantic_action_int_value_from_integer); prod_idx++;

// R_INT_VALUE_IDENTIFIER: <int_value> -> IDENTIFIER
GrammarSymbol* int_value_id_rhs[] = {all_terminals_map[TOKEN_IDENTIFIER]};
productions_array[prod_idx] = create_production(int_value_nt, int_value_id_rhs, 1, prod_idx, semantic_action_int_value_from_identifier); prod_idx++;


// R19: <list_element> -> <int_value>
GrammarSymbol* list_elem_int_value_rhs[] = {int_value_nt};
productions_array[prod_idx] = create_production(list_element_nt, list_elem_int_value_rhs, 1, prod_idx, semantic_action_list_element); prod_idx++;

// R20: <list_element> -> STRING
GrammarSymbol* list_elem_string_rhs[] = {all_terminals_map[TOKEN_STRING]};
productions_array[prod_idx] = create_production(list_element_nt, list_elem_string_rhs, 1, prod_idx, semantic_action_list_element); prod_idx++;

// R21: <list_element> -> newline
GrammarSymbol* list_elem_newline_rhs[] = {all_terminals_map[TOKEN_NEWLINE]};
productions_array[prod_idx] = create_production(list_element_nt, list_elem_newline_rhs, 1, prod_idx, semantic_action_list_element); prod_idx++;


    Grammar grammar = {
        .productions = productions_array, // Assign the pointer to the local array
        .production_count = prod_idx, // Use the actual count of added productions
        .terminals = all_terminals_map, // Assign the pointer to the dynamically allocated array
        .terminal_count = true_terminal_count, // Set the count to NUM_TOKEN_TYPES
        .non_terminals = all_non_terminals_map, // Assign the pointer to the dynamically allocated array
        .non_terminal_count = true_non_terminal_count, // Set the count to NUM_NON_TERMINALS_DEFINED
        .start_symbol = s_prime // S' is the augmented start symbol
    };

    // --- Test Input ---
    char *input_filename = argv[1];
    FILE *inputFile = fopen(input_filename, "r");
    if (!inputFile) {
        fprintf(stderr, "Error: Could not open input file '%s'\n", input_filename);
        free_grammar_data(&grammar); // Free any grammar data already allocated
        return EXIT_FAILURE;
    }

    int num_test_tokens = 0;
    Token* tokens = lexer(inputFile, input_filename, &num_test_tokens);
    fclose(inputFile);

    if (!tokens || (num_test_tokens > 0 && tokens[num_test_tokens - 1].type == TOKEN_ERROR)) {
        fprintf(stderr, "Lexical analysis failed or encountered errors. Aborting parsing.\n");
        if (tokens) free(tokens); // Free tokens even if an error occurred during lexing
        free_grammar_data(&grammar); // Free any grammar data already allocated
        return EXIT_FAILURE;
    }
    if (num_test_tokens == 0) {
        fprintf(stderr, "Lexer returned no tokens. Aborting parsing.\n");
        free_grammar_data(&grammar);
        return EXIT_FAILURE;
    }


    printf("Total tokens lexed: %d\n", num_test_tokens);

    // --- 4. Compute FIRST and FOLLOW Sets ---
    printf("Computing FIRST and FOLLOW sets...\n");
    compute_nullable_set(&grammar, nullable_status);
    compute_first_sets(&grammar);
    compute_follow_sets(&grammar);
    printf("FIRST and FOLLOW sets computed.\n");

    // --- 5. Generate LR(1) Item Sets (Canonical Collection) ---
    printf("Generating LR(1) item sets...\n");
    create_lr1_sets(&grammar);
    printf("LR(1) item sets generated. Total states: %d\n", canonical_collection.count);

    // --- 6. Build Parsing Tables ---
    printf("Building parsing tables...\n");
    // Pass pointer to global canonical_collection
    build_parsing_tables(&grammar, &canonical_collection, nullable_status);
    printf("Parsing tables built.\n");

    // --- 7. Perform Parsing ---
    printf("\nAttempting to parse sample tokens...\n");
    ASTNode* root_ast = parse(&grammar, tokens, num_test_tokens);

// --- 8. Inspect AST and Interpret ---
    if (root_ast) {
        printf("\n--- Parsing Successful! Generated AST: ---\n");
        printf("DEBUG: root_ast type received in main: %d (expected AST_PROGRAM: %d)\n", root_ast->type, AST_PROGRAM);
        print_ast_node(root_ast, 0);

        // --- NEW: Perform Interpretation ---
        interpret_program(root_ast); // Call your interpreter with the root AST

        free_ast_node(root_ast); // Free the entire AST
    } else {
        fprintf(stderr, "\n--- Parsing Failed! ---\n");
    }

    // --- 9. Cleanup ---
    printf("\nCleaning up...\n");

    free(tokens); // Free the tokens array allocated by lexer

    free_parsing_tables(); // Free action and goto tables
    // canonical_collection is a global struct, its internal fixed-size arrays don't need explicit free.
    // However, if any element within ItemSet was dynamically allocated, that would need a separate free.
    free_grammar_data(&grammar); // Free grammar symbols and production RHS arrays and their containers

    return 0;
}
