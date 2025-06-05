#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// --- Global Variables (definitions from parser.h externs) ---
TerminalSet firstSetsForNonTerminals[NUM_NON_TERMINALS_DEFINED];
TerminalSet followSetsForNonTerminals[NUM_NON_TERMINALS_DEFINED];
ActionEntry** action_table = NULL;
int** goto_table = NULL;
int num_states = 0;
bool nullable_status[NUM_NON_TERMINALS_DEFINED];
ItemSetList canonical_collection; // Global canonical collection (not a pointer)


// --- Helper Functions for TerminalSet Operations ---

// Initializes a TerminalSet to empty
void init_terminal_set(TerminalSet* set) {
    *set = 0ULL;
}

// Adds a terminal to a TerminalSet
void add_terminal_to_set(TerminalSet* set, TokenType terminal_id) {
    if (terminal_id >= 0 && terminal_id < 64) { // Assuming TokenType values are within 0-63
        *set |= (1ULL << terminal_id);
    }
}

// Checks if a terminal is in a TerminalSet
bool is_terminal_in_set(TerminalSet set, TokenType terminal_id) {
    if (terminal_id >= 0 && terminal_id < 64) {
        return (set & (1ULL << terminal_id)) != 0;
    }
    return false;
}

// Union of two TerminalSets, returns true if set1 changed
bool union_terminal_sets(TerminalSet* set1, TerminalSet set2) {
    TerminalSet original_set1 = *set1;
    *set1 |= set2;
    return *set1 != original_set1;
}

// Converts a TerminalSet to a readable string (for debugging)
void print_terminal_set(TerminalSet set, const Grammar* grammar) {
    printf("{ ");
    for (int i = 0; i < grammar->terminal_count; ++i) { // Iterate through all potential terminal IDs
        // Ensure that grammar->terminals[i] is a valid symbol before accessing
        if (i < TOKEN_ERROR + 1 && grammar->terminals[i] != NULL && is_terminal_in_set(set, grammar->terminals[i]->id)) {
            printf("%s ", grammar->terminals[i]->name);
        }
    }
    printf("}");
}


// --- LR(1) Item and ItemSet Management Functions ---

// Compares two items for equality
bool item_equals(const Item* item1, const Item* item2) {
    return item1->production_idx == item2->production_idx &&
           item1->dot_pos == item2->dot_pos &&
           item1->lookahead == item2->lookahead;
}

// Checks if an item set contains a specific item
bool item_set_contains_item(const ItemSet* set, const Item* item) {
    for (int i = 0; i < set->count; ++i) {
        if (item_equals(&set->items[i], item)) {
            return true;
        }
    }
    return false;
}

// Adds an item to an item set if it's not already present. Returns true if added.
bool add_item_to_set(ItemSet* set, const Item* item) {
    if (set->count >= (sizeof(set->items) / sizeof(set->items[0]))) { // Check against actual array size
        fprintf(stderr, "Error: ItemSet capacity exceeded. Max: %lu\n", (sizeof(set->items) / sizeof(set->items[0])));
        exit(EXIT_FAILURE);
    }
    if (!item_set_contains_item(set, item)) {
        set->items[set->count++] = *item;
        return true;
    }
    return false;
}

// Compares two item sets for equality (order of items doesn't matter)
bool item_set_equals(const ItemSet* set1, const ItemSet* set2) {
    if (set1->count != set2->count) {
        return false;
    }
    for (int i = 0; i < set1->count; ++i) {
        if (!item_set_contains_item(set2, &set1->items[i])) {
            return false;
        }
    }
    return true;
}

// Finds an existing item set in the canonical collection
int find_item_set(const ItemSetList* collection, const ItemSet* set) {
    for (int i = 0; i < collection->count; ++i) {
        if (item_set_equals(&collection->sets[i], set)) {
            return i;
        }
    }
    return -1;
}

// Adds a new item set to the canonical collection
int add_item_set(ItemSetList* collection, const ItemSet* set) {
    if (collection->count >= MAX_STATES) {
        fprintf(stderr, "Error: Canonical collection capacity exceeded. Max: %d\n", MAX_STATES);
        exit(EXIT_FAILURE);
    }
    int existing_idx = find_item_set(collection, set);
    if (existing_idx != -1) {
        return existing_idx; // Return existing state ID
    }

    collection->sets[collection->count] = *set; // Copy the set
    collection->sets[collection->count].id = collection->count;
    return collection->count++; // Return new state ID
}


// --- AST Node Creation and Management ---

ASTNode* create_ast_node(ASTNodeType type, SourceLocation loc) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Memory allocation failed for ASTNode.\n");
        exit(EXIT_FAILURE);
    }
    node->type = type;
    node->location = loc;
    node->children = NULL;
    node->num_children = 0;
    node->children_capacity = 0;
    // Initialize union members to safe defaults (e.g., NULL for pointers, 0 for values)
    memset(&node->data, 0, sizeof(node->data));
    return node;
}

void add_child_to_ast_node(ASTNode* parent, ASTNode* child) {
    if (!parent || !child) return;

    if (parent->num_children >= parent->children_capacity) {
        int new_capacity = parent->children_capacity == 0 ? 2 : parent->children_capacity * 2;
        ASTNode** new_children = (ASTNode**)realloc(parent->children, new_capacity * sizeof(ASTNode*));
        if (!new_children) {
            fprintf(stderr, "Memory re-allocation failed for AST children.\n");
            exit(EXIT_FAILURE);
        }
        parent->children = new_children;
        parent->children_capacity = new_capacity;
    }
    parent->children[parent->num_children++] = child;
}

// Creates a leaf node (identifier, integer, string, newline, keyword) directly from a token
ASTNode* create_ast_leaf_from_token(const Token* token) {
    ASTNode* node = NULL;
    if (!token) return NULL;

    switch (token->type) {
        case TOKEN_IDENTIFIER:
            node = create_ast_node(AST_IDENTIFIER, token->location);
            node->data.identifier.name = strdup(token->lexeme);
            node->data.identifier.symbol_table_index = token->value.symbol_index; // Assuming this is set by lexer/symbol table
            break;
        case TOKEN_INTEGER:
            node = create_ast_node(AST_INTEGER_LITERAL, token->location);
            node->data.int_value = token->value.int_value;
            break;
        case TOKEN_STRING:
            node = create_ast_node(AST_STRING_LITERAL, token->location);
            // Copy string content without quotes
            if (strlen(token->lexeme) >= 2) {
                node->data.string_value = strndup(token->lexeme + 1, strlen(token->lexeme) - 2);
            } else {
                node->data.string_value = strdup(""); // Empty string if invalid
            }
            break;
        case TOKEN_NEWLINE: // Keep NEWLINE separate as it's a specific output action
            node = create_ast_node(AST_NEWLINE, token->location);
            break;
        // For other terminals which are keywords/punctuation that we might want to represent in AST for location/debugging
        case TOKEN_NUMBER:
        case TOKEN_WRITE:
        case TOKEN_REPEAT:
        case TOKEN_AND:
        case TOKEN_TIMES:
        case TOKEN_ASSIGN:
        case TOKEN_PLUS_ASSIGN:
        case TOKEN_MINUS_ASSIGN:
        case TOKEN_OPENB:
        case TOKEN_CLOSEB:
        case TOKEN_EOL:
        case TOKEN_PLUS:
        case TOKEN_STAR:
        case TOKEN_LPAREN:
        case TOKEN_RPAREN:
        case TOKEN_EOF:
            node = create_ast_node(AST_KEYWORD, token->location);
            node->data.keyword_lexeme = strdup(token->lexeme); // Store the actual keyword string
            break;
        case TOKEN_ERROR:
        default:
            return NULL; // Should ideally not be reached if lexer is robust
    }
    return node;
}


// Recursive function to print the AST
void print_ast_node(const ASTNode* node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; ++i) {
        printf("  "); // 2 spaces per indent level
    }

    // Print node type
    switch (node->type) {
        case AST_PROGRAM: printf("Program\n"); break;
        case AST_STATEMENT_LIST: printf("StatementList\n"); break;
        case AST_DECLARATION: printf("Declaration\n"); break;
        case AST_ASSIGNMENT: printf("Assignment\n"); break;
        case AST_INCREMENT: printf("Increment\n"); break;
        case AST_DECREMENT: printf("Decrement\n"); break;
        case AST_WRITE_STATEMENT: printf("WriteStatement\n"); break;
        case AST_OUTPUT_LIST: printf("OutputList\n"); break;
        case AST_LIST_ELEMENT: printf("ListElement\n"); break;
        case AST_LOOP_STATEMENT: printf("LoopStatement\n"); break;
        case AST_CODE_BLOCK: printf("CodeBlock\n"); break;
        case AST_IDENTIFIER: printf("Identifier: %s\n", node->data.identifier.name); break;
        case AST_INTEGER_LITERAL: printf("Integer: %lld\n", node->data.int_value); break;
        case AST_STRING_LITERAL: printf("String: \"%s\"\n", node->data.string_value); break;
        case AST_NEWLINE: printf("Newline\n"); break;
        case AST_INT_VALUE: printf("Int_Value\n"); break; // NEW
        case AST_KEYWORD: printf("Keyword: %s\n", node->data.keyword_lexeme); break; // NEW
        case AST_ERROR: printf("ERROR_NODE\n"); break;
        default: printf("UNKNOWN_AST_NODE_TYPE (%d)\n", node->type); break;
    }

    // Recursively print children
    for (int i = 0; i < node->num_children; ++i) {
        print_ast_node(node->children[i], indent + 1);
    }
}

// Recursive function to free AST nodes
void free_ast_node(ASTNode* node) {
    if (!node) return;

    // Free children first
    for (int i = 0; i < node->num_children; ++i) {
        free_ast_node(node->children[i]);
    }
    if (node->children) {
        free(node->children);
    }

    // Free data specific to node type
    switch (node->type) {
        case AST_IDENTIFIER:
            if (node->data.identifier.name) {
                free(node->data.identifier.name);
            }
            break;
        case AST_STRING_LITERAL:
            if (node->data.string_value) {
                free(node->data.string_value);
            }
            break;
        case AST_KEYWORD:
            if (node->data.keyword_lexeme) {
                free(node->data.keyword_lexeme);
            }
            break;
        default:
            // No specific dynamically allocated data for other types
            break;
    }

    // Free the node itself
    free(node);
}

// --- Semantic Action Functions ---
// Each semantic action receives an array of ASTNode pointers corresponding
// to the symbols on the right-hand side of the production rule.
// It should return a new ASTNode representing the left-hand side of the production.

// Generic passthrough action (e.g., A -> B, just return B's AST node)
ASTNode* semantic_action_passthrough(ASTNode** children) {
    // For rules like S -> StatementList, or Statement -> LoopStatement
    // We just return the AST node of the child.
    if (children && children[0]) {
        return children[0];
    }
    return NULL; // Should not happen for valid grammars
}

// R0: S' -> Program EOF
ASTNode* semantic_action_program(ASTNode** children) {
    // children[0] is Program, children[1] is EOF. We only need the Program.
    ASTNode* program_node = create_ast_node(AST_PROGRAM, children[0]->location);
    add_child_to_ast_node(program_node, children[0]); // Program node
    return program_node;
}

// R1: <statement_list> -> <statement_list> <statement>
ASTNode* semantic_action_statement_list_multi(ASTNode** children) {
    ASTNode* stmt_list = children[0]; // Existing StatementList
    ASTNode* statement = children[1]; // New Statement

    // Add the new statement to the existing list
    add_child_to_ast_node(stmt_list, statement);
    return stmt_list;
}

// R2: <statement_list> -> <statement>
ASTNode* semantic_action_statement_list_single(ASTNode** children) {
    ASTNode* stmt_list = create_ast_node(AST_STATEMENT_LIST, children[0]->location);
    add_child_to_ast_node(stmt_list, children[0]); // The single Statement
    return stmt_list;
}

// R3-R7: <statement> -> ... ; (for assignment, declaration, inc, dec, write)
ASTNode* semantic_action_statement_with_semicolon(ASTNode** children) {
    // The first child is the actual statement AST node (e.g., Assignment, Declaration)
    // The second child is the semicolon token's AST node, which we typically discard.
    return children[0]; // Return the AST for the statement itself
}

// R9: <declaration> -> number IDENTIFIER
ASTNode* semantic_action_declaration(ASTNode** children) {
    // children[0] is 'number' (keyword), children[1] is IDENTIFIER
    // Get location from the IDENTIFIER as 'number' is just a keyword.
    ASTNode* declaration_node = create_ast_node(AST_DECLARATION, children[1]->location);
    add_child_to_ast_node(declaration_node, children[1]); // IDENTIFIER node
    return declaration_node;
}

// R10: <assignment> -> IDENTIFIER := <int_value>
ASTNode* semantic_action_assignment(ASTNode** children) {
    // children[0] is IDENTIFIER, children[1] is ':=', children[2] is <int_value>
    ASTNode* assignment_node = create_ast_node(AST_ASSIGNMENT, children[0]->location); // Location of IDENTIFIER
    add_child_to_ast_node(assignment_node, children[0]); // IDENTIFIER node (lhs)
    add_child_to_ast_node(assignment_node, children[2]); // <int_value> node (rhs)
    return assignment_node;
}

// R11: <decrement> -> IDENTIFIER -= <int_value>
ASTNode* semantic_action_decrement(ASTNode** children) {
    // children[0] is IDENTIFIER, children[1] is '-=', children[2] is <int_value>
    ASTNode* decrement_node = create_ast_node(AST_DECREMENT, children[0]->location);
    add_child_to_ast_node(decrement_node, children[0]); // IDENTIFIER node
    add_child_to_ast_node(decrement_node, children[2]); // <int_value> node
    return decrement_node;
}

// R12: <increment> -> IDENTIFIER += <int_value>
ASTNode* semantic_action_increment(ASTNode** children) {
    // children[0] is IDENTIFIER, children[1] is '+=', children[2] is <int_value>
    ASTNode* increment_node = create_ast_node(AST_INCREMENT, children[0]->location);
    add_child_to_ast_node(increment_node, children[0]); // IDENTIFIER node
    add_child_to_ast_node(increment_node, children[2]); // <int_value> node
    return increment_node;
}

// R13: <write_statement> -> write <output_list>
ASTNode* semantic_action_write_statement(ASTNode** children) {
    // children[0] is 'write' keyword (now an AST_KEYWORD node), children[1] is OutputList
    // Use location from the 'write' keyword node
    ASTNode* write_node = create_ast_node(AST_WRITE_STATEMENT, children[0]->location);
    add_child_to_ast_node(write_node, children[1]); // OutputList node
    return write_node;
}

// R14: <loop_statement> -> repeat <int_value> times <statement>
ASTNode* semantic_action_loop_statement_single(ASTNode** children) {
    // children[0] is 'repeat' (AST_KEYWORD), children[1] is <int_value>, children[2] is 'times' (AST_KEYWORD), children[3] is Statement
    ASTNode* loop_node = create_ast_node(AST_LOOP_STATEMENT, children[0]->location); // Location of 'repeat'
    loop_node->data.loop.count_expr = children[1]; // <int_value> node
    loop_node->data.loop.body = children[3];       // Statement node
    return loop_node;
}

// R15: <loop_statement> -> repeat <int_value> times <code_block>
ASTNode* semantic_action_loop_statement_block(ASTNode** children) {
    // children[0] is 'repeat' (AST_KEYWORD), children[1] is <int_value>, children[2] is 'times' (AST_KEYWORD), children[3] is CodeBlock
    ASTNode* loop_node = create_ast_node(AST_LOOP_STATEMENT, children[0]->location); // Location of 'repeat'
    loop_node->data.loop.count_expr = children[1]; // <int_value> node
    loop_node->data.loop.body = children[3];       // CodeBlock node
    return loop_node;
}

// R16: <code_block> -> { <statement_list> }
ASTNode* semantic_action_code_block(ASTNode** children) {
    // children[0] is '{' (AST_KEYWORD), children[1] is StatementList, children[2] is '}' (AST_KEYWORD)
    ASTNode* code_block_node = create_ast_node(AST_CODE_BLOCK, children[0]->location); // Location of '{'
    add_child_to_ast_node(code_block_node, children[1]); // StatementList node
    return code_block_node;
}

// R17: <output_list> -> <output_list> and <list_element>
ASTNode* semantic_action_output_list_multi(ASTNode** children) {
    // children[0] is existing OutputList, children[1] is 'and' (AST_KEYWORD), children[2] is new ListElement
    ASTNode* output_list = children[0];    // Existing OutputList
    ASTNode* list_element = children[2]; // New ListElement (skip 'and' keyword AST node)

    add_child_to_ast_node(output_list, list_element);
    return output_list;
}

// R18: <output_list> -> <list_element>
ASTNode* semantic_action_output_list_single(ASTNode** children) {
    // children[0] is the single ListElement
    ASTNode* output_list = create_ast_node(AST_OUTPUT_LIST, children[0]->location);
    add_child_to_ast_node(output_list, children[0]); // The single ListElement
    return output_list;
}

// NEW: <int_value> -> INTEGER
ASTNode* semantic_action_int_value_from_integer(ASTNode** children) {
    // children[0] is AST_INTEGER_LITERAL
    ASTNode* int_value_node = create_ast_node(AST_INT_VALUE, children[0]->location);
    add_child_to_ast_node(int_value_node, children[0]); // Add the integer literal as a child
    return int_value_node;
}

// NEW: <int_value> -> IDENTIFIER
ASTNode* semantic_action_int_value_from_identifier(ASTNode** children) {
    // children[0] is AST_IDENTIFIER
    ASTNode* int_value_node = create_ast_node(AST_INT_VALUE, children[0]->location);
    add_child_to_ast_node(int_value_node, children[0]); // Add the identifier as a child
    return int_value_node;
}


// R19: <list_element> -> <int_value>
// R20: <list_element> -> STRING
// R21: <list_element> -> NEWLINE
ASTNode* semantic_action_list_element(ASTNode** children) {
    // The child can be AST_INT_VALUE, AST_STRING_LITERAL, or AST_NEWLINE.
    // We create a generic LIST_ELEMENT node and add the actual element as a child.
    ASTNode* list_element_node = create_ast_node(AST_LIST_ELEMENT, children[0]->location);
    add_child_to_ast_node(list_element_node, children[0]);
    return list_element_node;
}


// --- FIRST and FOLLOW Set Computation ---

// Computes the nullable status for all non-terminals
void compute_nullable_set(const Grammar* grammar, bool nullable[NUM_NON_TERMINALS_DEFINED]) {
    // Initialize all non-terminals to not nullable
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        if (grammar->non_terminals[i] != NULL) { // Only set status for defined non-terminals
            nullable[grammar->non_terminals[i]->id] = false;
        }
    }

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < grammar->production_count; ++i) {
            const Production* p = &grammar->productions[i]; // Use const Production*
            int left_nt_id = p->left_symbol->id;

            if (left_nt_id >= NUM_NON_TERMINALS_DEFINED) {
                fprintf(stderr, "Error: Non-terminal ID %d out of bounds for nullable array in production %d.\n", left_nt_id, p->production_id);
                exit(EXIT_FAILURE);
            }
            if (nullable[left_nt_id]) continue; // Already known to be nullable

            bool rhs_nullable = true;
            for (int j = 0; j < p->right_count; ++j) {
                const GrammarSymbol* s = p->right_symbols[j]; // Use const GrammarSymbol*
                if (s->type == SYMBOL_TERMINAL) {
                    rhs_nullable = false; // A terminal means RHS is not nullable
                    break;
                } else { // SYMBOL_NONTERMINAL
                    // Ensure the ID is within the bounds of nullable array
                    if (s->id >= NUM_NON_TERMINALS_DEFINED) {
                         fprintf(stderr, "Error: Non-terminal ID %d out of bounds for nullable array during check in production %d, symbol %d.\n", s->id, p->production_id, j);
                         exit(EXIT_FAILURE);
                    }
                    if (!nullable[s->id]) {
                        rhs_nullable = false; // A non-nullable non-terminal means RHS is not nullable
                        break;
                    }
                }
            }

            if (rhs_nullable) {
                nullable[left_nt_id] = true;
                changed = true;
            }
        }
    } while (changed);
}

// Computes the FIRST set for all non-terminals
void compute_first_sets(const Grammar* grammar) {
    // Initialize all FIRST sets to empty
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        if (grammar->non_terminals[i] != NULL) {
            if (grammar->non_terminals[i]->id >= NUM_NON_TERMINALS_DEFINED) {
                fprintf(stderr, "Error: Non-terminal ID %d out of bounds for firstSetsForNonTerminals array initialization.\n", grammar->non_terminals[i]->id);
                exit(EXIT_FAILURE);
            }
            init_terminal_set(&firstSetsForNonTerminals[grammar->non_terminals[i]->id]);
        }
    }

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < grammar->production_count; ++i) {
            const Production* p = &grammar->productions[i]; // Use const Production*
            int left_nt_id = p->left_symbol->id;

            if (left_nt_id >= NUM_NON_TERMINALS_DEFINED) {
                fprintf(stderr, "Error: Left-hand non-terminal ID %d out of bounds in production %d for FIRST set computation.\n", left_nt_id, p->production_id);
                exit(EXIT_FAILURE);
            }

            // For each symbol X on the RHS (X_1 X_2 ... X_k)
            for (int j = 0; j < p->right_count; ++j) {
                const GrammarSymbol* current_rhs_symbol = p->right_symbols[j]; // Use const GrammarSymbol*

                if (current_rhs_symbol->type == SYMBOL_TERMINAL) {
                    // If X_j is a terminal, add FIRST(X_j) (which is just X_j) to FIRST(LHS)
                    if (current_rhs_symbol->id >= 64) { // Check if terminal ID fits bitset
                        fprintf(stderr, "Error: Terminal ID %d out of bounds for TerminalSet bitmask in production %d, symbol %d.\n", current_rhs_symbol->id, p->production_id, j);
                        exit(EXIT_FAILURE);
                    }
                    if (union_terminal_sets(&firstSetsForNonTerminals[left_nt_id], 1ULL << current_rhs_symbol->id)) {
                        changed = true;
                    }
                    break; // No further symbols on RHS matter for FIRST if a terminal is encountered
                } else { // SYMBOL_NONTERMINAL
                    // Add FIRST(X_j) to FIRST(LHS)
                    if (current_rhs_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                        fprintf(stderr, "Error: RHS non-terminal ID %d out of bounds for FIRST set computation in production %d, symbol %d.\n", current_rhs_symbol->id, p->production_id, j);
                        exit(EXIT_FAILURE);
                    }
                    if (union_terminal_sets(&firstSetsForNonTerminals[left_nt_id], firstSetsForNonTerminals[current_rhs_symbol->id])) {
                        changed = true;
                    }
                    // If X_j is nullable, continue to the next symbol on RHS
                    // Ensure ID is within bounds
                    if (current_rhs_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                        fprintf(stderr, "Error: RHS non-terminal ID %d out of bounds for nullable_status check in production %d, symbol %d.\n", current_rhs_symbol->id, p->production_id, j);
                        exit(EXIT_FAILURE);
                    }
                    if (!nullable_status[current_rhs_symbol->id]) {
                        break; // Not nullable, so no further symbols matter for FIRST
                    }
                }
            }
        }
    } while (changed);

    // Debugging print: Print computed FIRST sets
    printf("\n--- Computed FIRST Sets ---\n");
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        if (grammar->non_terminals[i] != NULL) {
            printf("FIRST(%s): ", grammar->non_terminals[i]->name);
            if (grammar->non_terminals[i]->id < NUM_NON_TERMINALS_DEFINED) { // Ensure bounds
                print_terminal_set(firstSetsForNonTerminals[grammar->non_terminals[i]->id], grammar);
            } else {
                printf("Error: Non-terminal ID out of bounds for printing FIRST set.\n");
            }
            printf("\n");
        }
    }
    printf("---------------------------\n");
}

// Computes the FOLLOW set for all non-terminals
void compute_follow_sets(const Grammar* grammar) {
    // Initialize all FOLLOW sets to empty
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        if (grammar->non_terminals[i] != NULL) {
            if (grammar->non_terminals[i]->id >= NUM_NON_TERMINALS_DEFINED) {
                fprintf(stderr, "Error: Non-terminal ID %d out of bounds for followSetsForNonTerminals array initialization.\n", grammar->non_terminals[i]->id);
                exit(EXIT_FAILURE);
            }
            init_terminal_set(&followSetsForNonTerminals[grammar->non_terminals[i]->id]);
        }
    }

    // Add EOF to the FOLLOW set of the augmented start symbol
    if (grammar->start_symbol->id >= NUM_NON_TERMINALS_DEFINED || TOKEN_EOF >= 64) {
        fprintf(stderr, "Error: Start symbol ID or TOKEN_EOF out of bounds for FOLLOW set initialization.\n");
        exit(EXIT_FAILURE);
    }
    add_terminal_to_set(&followSetsForNonTerminals[grammar->start_symbol->id], TOKEN_EOF);

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < grammar->production_count; ++i) {
            const Production* p = &grammar->productions[i]; // Use const Production*
            int left_nt_id = p->left_symbol->id;

            if (left_nt_id >= NUM_NON_TERMINALS_DEFINED) {
                fprintf(stderr, "Error: Left-hand non-terminal ID %d out of bounds in production %d for FOLLOW set computation.\n", left_nt_id, p->production_id);
                exit(EXIT_FAILURE);
            }

            // For each non-terminal B on the RHS: A -> alpha B beta
            for (int j = 0; j < p->right_count; ++j) {
                const GrammarSymbol* current_rhs_symbol = p->right_symbols[j]; // Use const GrammarSymbol*

                if (current_rhs_symbol->type == SYMBOL_NONTERMINAL) {
                    // Case 1: A -> alpha B beta. Add FIRST(beta) to FOLLOW(B)
                    TerminalSet first_beta_set;
                    init_terminal_set(&first_beta_set);
                    bool beta_is_nullable = true; // Tracks if the rest of beta is nullable

                    for (int k = j + 1; k < p->right_count; ++k) {
                        const GrammarSymbol* beta_symbol = p->right_symbols[k]; // Use const GrammarSymbol*
                        if (beta_symbol->type == SYMBOL_TERMINAL) {
                            if (beta_symbol->id >= 64) {
                                fprintf(stderr, "Error: Terminal ID %d out of bounds for FIRST(beta) bitmask in production %d, symbol %d.\n", beta_symbol->id, p->production_id, k);
                                exit(EXIT_FAILURE);
                            }
                            add_terminal_to_set(&first_beta_set, beta_symbol->id);
                            beta_is_nullable = false;
                            break; // Terminal found, stop
                        } else { // SYMBOL_NONTERMINAL
                            if (beta_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                                fprintf(stderr, "Error: RHS non-terminal ID %d out of bounds for FIRST(beta) in production %d, symbol %d.\n", beta_symbol->id, p->production_id, k);
                                exit(EXIT_FAILURE);
                            }
                            union_terminal_sets(&first_beta_set, firstSetsForNonTerminals[beta_symbol->id]);
                            // Ensure ID is within bounds
                            if (beta_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                                fprintf(stderr, "Error: RHS non-terminal ID %d out of bounds for nullable_status check in production %d, symbol %d.\n", beta_symbol->id, p->production_id, k);
                                exit(EXIT_FAILURE);
                            }
                            if (!nullable_status[beta_symbol->id]) {
                                beta_is_nullable = false;
                                break;
                            }
                        }
                    }
                    if (current_rhs_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                        fprintf(stderr, "Error: Current RHS non-terminal ID %d out of bounds for FOLLOW set update in production %d.\n", current_rhs_symbol->id, p->production_id);
                        exit(EXIT_FAILURE);
                    }
                    if (union_terminal_sets(&followSetsForNonTerminals[current_rhs_symbol->id], first_beta_set)) {
                        changed = true;
                    }

                    // Case 2: A -> alpha B (or A -> alpha B beta where beta is nullable).
                    // Add FOLLOW(A) to FOLLOW(B).
                    if (beta_is_nullable) {
                        // Ensure ID is within bounds
                        if (current_rhs_symbol->id < NUM_NON_TERMINALS_DEFINED &&
                            left_nt_id < NUM_NON_TERMINALS_DEFINED) {
                            if (union_terminal_sets(&followSetsForNonTerminals[current_rhs_symbol->id], followSetsForNonTerminals[left_nt_id])) {
                                changed = true;
                            }
                        } else {
                            fprintf(stderr, "Warning: Non-terminal ID out of bounds during FOLLOW set propagation.\n");
                        }
                    }
                }
            }
        }
    } while (changed);

    // Debugging print: Print computed FOLLOW sets
    printf("\n--- Computed FOLLOW Sets ---\n");
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        if (grammar->non_terminals[i] != NULL) {
            printf("FOLLOW(%s): ", grammar->non_terminals[i]->name);
            if (grammar->non_terminals[i]->id < NUM_NON_TERMINALS_DEFINED) { // Ensure bounds
                print_terminal_set(followSetsForNonTerminals[grammar->non_terminals[i]->id], grammar);
            } else {
                printf("Error: Non-terminal ID out of bounds for printing FOLLOW set.\n");
            }
            printf("\n");
        }
    }
    printf("----------------------------\n");
}


// --- LR(1) Item Set Generation (Canonical Collection) ---

// Computes the closure of an item set
void closure(ItemSet* I, const Grammar* grammar) {
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < I->count; ++i) {
            Item current_item = I->items[i];
            const Production* p = &grammar->productions[current_item.production_idx]; // Use const Production*

            // If the dot is not at the end of the RHS
            if (current_item.dot_pos < p->right_count) {
                const GrammarSymbol* B = p->right_symbols[current_item.dot_pos]; // Use const GrammarSymbol*

                // If B is a non-terminal
                if (B->type == SYMBOL_NONTERMINAL) {
                    // Compute FIRST(beta a)
                    TerminalSet first_beta_alpha;
                    init_terminal_set(&first_beta_alpha);
                    bool beta_is_nullable = true;

                    for (int k = current_item.dot_pos + 1; k < p->right_count; ++k) {
                        const GrammarSymbol* beta_symbol = p->right_symbols[k]; // Use const GrammarSymbol*
                        if (beta_symbol->type == SYMBOL_TERMINAL) {
                            if (beta_symbol->id >= 64) {
                                fprintf(stderr, "Error: Terminal ID %d out of bounds for FIRST(beta) bitmask in closure, production %d, symbol %d.\n", beta_symbol->id, p->production_id, k);
                                exit(EXIT_FAILURE);
                            }
                            add_terminal_to_set(&first_beta_alpha, beta_symbol->id);
                            beta_is_nullable = false;
                            break;
                        } else { // SYMBOL_NONTERMINAL
                            if (beta_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                                fprintf(stderr, "Error: RHS non-terminal ID %d out of bounds for FIRST set lookup in closure, production %d, symbol %d.\n", beta_symbol->id, p->production_id, k);
                                exit(EXIT_FAILURE);
                            }
                            union_terminal_sets(&first_beta_alpha, firstSetsForNonTerminals[beta_symbol->id]);
                            // Ensure ID is within bounds
                            if (beta_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                                fprintf(stderr, "Error: RHS non-terminal ID %d out of bounds for nullable_status check in closure, production %d, symbol %d.\n", beta_symbol->id, p->production_id, k);
                                exit(EXIT_FAILURE);
                            }
                            if (!nullable_status[beta_symbol->id]) {
                                beta_is_nullable = false;
                                break;
                            }
                        }
                    }
                    if (beta_is_nullable) {
                        if (current_item.lookahead >= 64) {
                            fprintf(stderr, "Error: Lookahead terminal ID %d out of bounds for bitmask in closure.\n", current_item.lookahead);
                            exit(EXIT_FAILURE);
                        }
                        add_terminal_to_set(&first_beta_alpha, current_item.lookahead);
                    }


                    // For each production B -> gamma and each terminal b in FIRST(beta a)
                    for (int prod_idx = 0; prod_idx < grammar->production_count; ++prod_idx) {
                        const Production* B_prod = &grammar->productions[prod_idx]; // Use const Production*
                        if (B_prod->left_symbol->id == B->id) { // This production starts with B
                            // Iterate through all possible terminal IDs
                            for (TokenType b_id = 0; b_id < grammar->terminal_count; ++b_id) {
                                // Only add if terminal exists and is in the computed set
                                // Also ensure b_id is within the 64-bit limit for TerminalSet
                                if (b_id < 64 && grammar->terminals[b_id] != NULL && is_terminal_in_set(first_beta_alpha, b_id)) {
                                    Item new_item = { .production_idx = prod_idx, .dot_pos = 0, .lookahead = b_id };
                                    if (add_item_to_set(I, &new_item)) {
                                        changed = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } while (changed);
}

// Computes the GOTO function for an item set and a grammar symbol X
ItemSet go_to(const ItemSet* I, const GrammarSymbol* X, const Grammar* grammar) { // X is const
    ItemSet J = { .count = 0, .id = -1 }; // Initialize J

    for (int i = 0; i < I->count; ++i) {
        Item current_item = I->items[i];
        const Production* p = &grammar->productions[current_item.production_idx]; // Use const Production*

        // If dot is before X
        if (current_item.dot_pos < p->right_count && p->right_symbols[current_item.dot_pos]->id == X->id) {
            Item new_item = {
                .production_idx = current_item.production_idx,
                .dot_pos = current_item.dot_pos + 1,
                .lookahead = current_item.lookahead
            };
            add_item_to_set(&J, &new_item);
        }
    }
    closure(&J, grammar); // Compute the closure of the new set
    return J;
}

// Builds the canonical collection of LR(1) item sets
void create_lr1_sets(const Grammar* grammar) {
    canonical_collection.count = 0;

    // Create initial item set I0: closure({S' -> . Program $})
    // S' -> Program EOF is assumed to be production 0 in the grammar definition
    Item initial_item = { .production_idx = 0, .dot_pos = 0, .lookahead = TOKEN_EOF };
    ItemSet I0 = { .count = 0, .id = 0 };
    add_item_to_set(&I0, &initial_item);
    closure(&I0, grammar);
    add_item_set(&canonical_collection, &I0); // Add I0 to the collection

    // Debugging: Print I0 contents
    printf("\n--- I0 (State 0) Contents ---\n");
    for (int k = 0; k < canonical_collection.sets[0].count; ++k) {
        const Item* item = &canonical_collection.sets[0].items[k];
        const Production* p = &grammar->productions[item->production_idx];
        printf("  %s -> ", p->left_symbol->name);
        for (int m = 0; m < p->right_count; ++m) {
            if (m == item->dot_pos) printf(".");
            printf("%s ", p->right_symbols[m]->name);
        }
        if (item->dot_pos == p->right_count) printf(".");
        printf(", %s\n", token_type_str(item->lookahead));
    }
    printf("------------------------------\n");


    int i = 0;
    while (i < canonical_collection.count) {
        const ItemSet current_I = canonical_collection.sets[i]; // current_I is const

        // Collect all unique grammar symbols (terminals and non-terminals) that can follow the dot
        // This is a more robust way to iterate over all possible next symbols
        GrammarSymbol* reachable_symbols[MAX_SYMBOLS_TOTAL] = {NULL}; // Store unique symbols
        int reachable_symbols_count = 0;

        for (int k = 0; k < current_I.count; ++k) {
            const Item item = current_I.items[k];
            const Production* p = &grammar->productions[item.production_idx];

            if (item.dot_pos < p->right_count) {
                const GrammarSymbol* next_symbol = p->right_symbols[item.dot_pos];
                bool found = false;
                for(int s = 0; s < reachable_symbols_count; ++s) {
                    if (reachable_symbols[s] == next_symbol) {
                        found = true;
                        break;
                    }
                }
                if (!found && reachable_symbols_count < MAX_SYMBOLS_TOTAL) {
                    reachable_symbols[reachable_symbols_count++] = (GrammarSymbol*)next_symbol; // Cast away const temporarily for array
                } else if (!found) {
                    fprintf(stderr, "Error: reachable_symbols_count exceeded MAX_SYMBOLS_TOTAL.\n");
                    exit(EXIT_FAILURE);
                }
            }
        }


        for (int j = 0; j < reachable_symbols_count; ++j) {
            const GrammarSymbol* X = reachable_symbols[j];
            // Fix: Use &current_I instead of undeclared I
            ItemSet J = go_to(&current_I, X, grammar);

            if (J.count > 0) { // If GOTO(I, X) is not empty
                // Fix: Use &canonical_collection instead of undeclared canonical_collection_ptr
                int existing_J_idx = find_item_set(&canonical_collection, &J);
                if (existing_J_idx == -1) {
                    // Add new item set to collection
                    add_item_set(&canonical_collection, &J);
                }
            }
        }
        i++; // Move to the next item set in the collection
    }
}


// --- Parsing Table Construction ---

void build_parsing_tables(const Grammar* grammar, const ItemSetList* canonical_collection_ptr, bool nullable[NUM_NON_TERMINALS_DEFINED]) {
    num_states = canonical_collection_ptr->count;

    // Allocate action table: [state][terminal_id]
    action_table = (ActionEntry**)malloc(num_states * sizeof(ActionEntry*));
    if (!action_table) {
        fprintf(stderr, "Memory allocation failed for action_table.\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < num_states; ++i) {
        // Allocate based on true_terminal_count from grammar for precise indexing
        action_table[i] = (ActionEntry*)malloc(grammar->terminal_count * sizeof(ActionEntry));
        if (!action_table[i]) {
            fprintf(stderr, "Memory allocation failed for action_table row %d.\n", i);
            exit(EXIT_FAILURE);
        }
        // Initialize all actions to ERROR
        for (int j = 0; j < grammar->terminal_count; ++j) {
            action_table[i][j].type = ACTION_ERROR;
            action_table[i][j].target_state_or_production_id = -1;
        }
    }

    // Allocate goto table: [state][non_terminal_id]
    // Use NUM_NON_TERMINALS_DEFINED from parser.h for consistent indexing
    goto_table = (int**)malloc(num_states * sizeof(int*));
    if (!goto_table) {
        fprintf(stderr, "Memory allocation failed for goto_table.\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < num_states; ++i) {
        goto_table[i] = (int*)malloc(NUM_NON_TERMINALS_DEFINED * sizeof(int));
        if (!goto_table[i]) {
            fprintf(stderr, "Memory allocation failed for goto_table row %d.\n", i);
            exit(EXIT_FAILURE);
        }
        // Initialize all goto entries to -1 (error/undefined)
        for (int j = 0; j < NUM_NON_TERMINALS_DEFINED; ++j) {
            goto_table[i][j] = -1;
        }
    }

    // Populate tables based on LR(1) item sets
    for (int i = 0; i < num_states; ++i) {
        const ItemSet* I = &canonical_collection_ptr->sets[i];

        // First, handle all SHIFT actions for the current state by iterating unique shift symbols
        GrammarSymbol* shift_symbols[MAX_SYMBOLS_TOTAL] = {NULL}; // Store unique symbols that can be shifted
        int shift_symbols_count = 0;
        // This boolean array tracks if a terminal ID has already been added to shift_symbols for the current state
        bool symbol_added_for_shift[TOKEN_ERROR + 1] = {false};

        for (int j = 0; j < I->count; ++j) {
            Item current_item = I->items[j];
            const Production* p = &grammar->productions[current_item.production_idx];

            if (current_item.dot_pos < p->right_count) {
                const GrammarSymbol* next_symbol = p->right_symbols[current_item.dot_pos];

                if (next_symbol->type == SYMBOL_TERMINAL) {
                    // Only add if not already added for this state
                    if (next_symbol->id < (TOKEN_ERROR + 1) && !symbol_added_for_shift[next_symbol->id]) {
                        if (shift_symbols_count >= MAX_SYMBOLS_TOTAL) {
                            fprintf(stderr, "Error: shift_symbols_count exceeded MAX_SYMBOLS_TOTAL.\n");
                            exit(EXIT_FAILURE);
                        }
                        shift_symbols[shift_symbols_count++] = (GrammarSymbol*)next_symbol;
                        symbol_added_for_shift[next_symbol->id] = true;
                    }
                }
            }
        }

        // Now process shifts for each unique shift symbol found
        for (int s = 0; s < shift_symbols_count; ++s) {
            const GrammarSymbol* X = shift_symbols[s]; // X is the terminal to shift on
            ItemSet J = go_to(I, X, grammar); // Compute the GOTO set
            int target_state = find_item_set(canonical_collection_ptr, &J); // Find its ID in the collection

            if (target_state != -1) {
                if (X->id >= grammar->terminal_count) {
                    fprintf(stderr, "Error: Terminal ID %d out of bounds for action_table access in state %d.\n", X->id, i);
                    exit(EXIT_FAILURE);
                }

                // If the action table entry is not empty, check for true conflicts (Shift/Reduce or Reduce/Reduce).
                // If it's already set to SHIFT to the *same* state, it's not a conflict, just a redundant setting.
                if (action_table[i][X->id].type != ACTION_ERROR &&
                    !(action_table[i][X->id].type == ACTION_SHIFT &&
                      action_table[i][X->id].target_state_or_production_id == target_state))
                {
                    fprintf(stderr, "Conflict detected in state %d on terminal %s!\n", i, X->name);
                    fprintf(stderr, "  Existing action type: %d (Target: %d), New action type: %d (Target: %d)\n",
                            action_table[i][X->id].type, action_table[i][X->id].target_state_or_production_id,
                            ACTION_SHIFT, target_state);
                    exit(EXIT_FAILURE);
                }
                action_table[i][X->id].type = ACTION_SHIFT;
                action_table[i][X->id].target_state_or_production_id = target_state;
            }
        }

        // Then, handle all REDUCE/ACCEPT actions for the current state
        for (int j = 0; j < I->count; ++j) {
            Item current_item = I->items[j];
            const Production* p = &grammar->productions[current_item.production_idx];

            if (current_item.dot_pos == p->right_count) { // Reduction item (dot at the end)
                // If S' -> Program . EOF, then ACTION_ACCEPT
                if (p->left_symbol->id == grammar->start_symbol->id && current_item.lookahead == TOKEN_EOF) {
                    if (TOKEN_EOF >= grammar->terminal_count) {
                        fprintf(stderr, "Error: TOKEN_EOF (%d) out of bounds for action_table access in state %d.\n", TOKEN_EOF, i);
                        exit(EXIT_FAILURE);
                    }
                    // Check for conflicts with existing actions (e.g., Shift/Accept, Reduce/Accept)
                    if (action_table[i][TOKEN_EOF].type != ACTION_ERROR && action_table[i][TOKEN_EOF].type != ACTION_ACCEPT) {
                        fprintf(stderr, "Conflict detected (Accept/Reduce or Shift/Accept) in state %d on EOF!\n", i);
                        fprintf(stderr, "  Existing action type: %d, New action type: %d\n", action_table[i][TOKEN_EOF].type, ACTION_ACCEPT);
                        exit(EXIT_FAILURE);
                    }
                    action_table[i][TOKEN_EOF].type = ACTION_ACCEPT;
                } else {
                    // REDUCE action for current_item.lookahead
                    if (current_item.lookahead >= grammar->terminal_count) {
                        fprintf(stderr, "Error: Lookahead terminal ID %d out of bounds for action_table access in state %d.\n", current_item.lookahead, i);
                        exit(EXIT_FAILURE);
                    }
                    // Check for Shift/Reduce or Reduce/Reduce conflicts
                    if (action_table[i][current_item.lookahead].type != ACTION_ERROR) {
                        // This is a true conflict, as a reduce action would clash with an existing shift or another reduce
                        fprintf(stderr, "Conflict detected (Shift/Reduce or Reduce/Reduce) in state %d on terminal %s!\n", i, token_type_str(current_item.lookahead));
                        fprintf(stderr, "  Existing action type: %d (Target: %d), New action type: %d (Production: %d)\n",
                                action_table[i][current_item.lookahead].type, action_table[i][current_item.lookahead].target_state_or_production_id,
                                ACTION_REDUCE, p->production_id);
                        exit(EXIT_FAILURE);
                    }
                    action_table[i][current_item.lookahead].type = ACTION_REDUCE;
                    action_table[i][current_item.lookahead].target_state_or_production_id = p->production_id;
                }
            }
        }

        // Populate GoTo table: For each non-terminal A
        for (int nt_idx = 0; nt_idx < NUM_NON_TERMINALS_DEFINED; ++nt_idx) {
            const GrammarSymbol* A = grammar->non_terminals[nt_idx];
            if (A == NULL) continue; // Skip if this NT_ID is not used (sparse array)

            ItemSet J = go_to(I, A, grammar); // Compute GOTO set for this non-terminal
            if (J.count > 0) {
                int target_state = find_item_set(canonical_collection_ptr, &J);
                if (target_state != -1) {
                    if (A->id >= NUM_NON_TERMINALS_DEFINED) {
                        fprintf(stderr, "Error: Non-terminal ID %d out of bounds for goto_table access in state %d.\n", A->id, i);
                        exit(EXIT_FAILURE);
                    }
                    // GOTO table entries should be unique by construction in LR parsers.
                    // If a conflict occurs here, it indicates a serious internal logic error.
                    if (goto_table[i][A->id] != -1 && goto_table[i][A->id] != target_state) {
                         fprintf(stderr, "Internal Error: GOTO table conflict detected for state %d on non-terminal %s (existing: %d, new: %d)!\n", i, A->name, goto_table[i][A->id], target_state);
                         exit(EXIT_FAILURE);
                    }
                    goto_table[i][A->id] = target_state;
                }
            }
        }
    }

    // Debugging: Print action table entry for State 0 and TOKEN_IDENTIFIER
    printf("\n--- Debugging Action Table State 0, Token IDENTIFIER ---\n");
    if (0 < num_states && TOKEN_IDENTIFIER < grammar->terminal_count) {
        ActionEntry dbg_action = action_table[0][TOKEN_IDENTIFIER];
        printf("Action[0][IDENTIFIER]: Type = %d (SHIFT=%d, REDUCE=%d, ACCEPT=%d, ERROR=%d), Target = %d\n",
               dbg_action.type, ACTION_SHIFT, ACTION_REDUCE, ACTION_ACCEPT, ACTION_ERROR, dbg_action.target_state_or_production_id);
    } else {
        printf("State 0 or TOKEN_IDENTIFIER out of bounds for action table lookup.\n");
    }
    printf("----------------------------------------------------------\n");
}

// Frees the memory allocated for the parsing tables
void free_parsing_tables() {
    if (action_table) {
        for (int i = 0; i < num_states; ++i) {
            if (action_table[i]) {
                free(action_table[i]);
            }
        }
        free(action_table);
        action_table = NULL;
    }
    if (goto_table) {
        for (int i = 0; i < num_states; ++i) {
            if (goto_table[i]) {
                free(goto_table[i]);
            }
        }
        free(goto_table);
        goto_table = NULL;
    }
    // canonical_collection is a global struct, its internal fixed-size arrays don't need free.
}


// --- Main Parsing Function ---
ASTNode* parse(const Grammar* grammar, Token* tokens, int num_tokens) {
    // Parser stack: Stores state numbers and AST nodes
    typedef struct {
        int state;
        ASTNode* ast_node; // AST node associated with this symbol
    } StackEntry;

    StackEntry parse_stack[MAX_STATES + MAX_PRODUCTIONS]; // Max states + max symbols on RHS
    int stack_ptr = 0;

    // Push initial state (0) onto the stack
    parse_stack[stack_ptr].state = 0;
    parse_stack[stack_ptr].ast_node = NULL; // No AST node for initial state
    stack_ptr++;

    int token_idx = 0;
    Token current_token = tokens[token_idx]; // Start with the first token

    printf("\n--- Starting Parsing ---\n");

    while (true) {
        int current_state = parse_stack[stack_ptr - 1].state;
        TokenType current_token_type = current_token.type;

        // Basic check for out-of-bounds token type for action table lookup
        if (current_token_type < 0 || current_token_type >= grammar->terminal_count) {
             fprintf(stderr, "Parser Error: Invalid token type (%s, ID: %d) encountered at input line %d, column %d. This token is not a recognized terminal for parsing table lookup.\n",
                     token_type_str(current_token_type), current_token_type, current_token.location.line, current_token.location.column);
             return NULL;
        }

        ActionEntry action = action_table[current_state][current_token_type];

        printf("State: %d, Current Token: %s ('%s', Line:%d Col:%d) | Action: ",
               current_state, token_type_str(current_token_type), current_token.lexeme,
               current_token.location.line, current_token.location.column);

        switch (action.type) {
            case ACTION_SHIFT: {
                int next_state = action.target_state_or_production_id;
                printf("SHIFT %d\n", next_state);

                // Create a leaf AST node for the shifted terminal if it's relevant.
                // For punctuation tokens like ';', '{', '}', we might not create a distinct AST node.
                // The `create_ast_leaf_from_token` function already handles this by returning NULL
                // for types it doesn't explicitly convert to a leaf node.
                ASTNode* shifted_node = create_ast_leaf_from_token(&current_token);

                // Push next state and token's AST node onto stack
                parse_stack[stack_ptr].state = next_state;
                parse_stack[stack_ptr].ast_node = shifted_node; // Attach AST node (can be NULL for some terminals)
                stack_ptr++;

                // Advance input token
                token_idx++;
                if (token_idx < num_tokens) {
                    current_token = tokens[token_idx];
                } else {
                    // This scenario means we ran out of tokens unexpectedly before reaching an explicit TOKEN_EOF.
                    // For robustness, set current_token to EOF.
                    current_token.type = TOKEN_EOF;
                    strncpy(current_token.lexeme, "EOF", MAX_LEXEME_LENGTH);
                    current_token.lexeme[MAX_LEXEME_LENGTH-1] = '\0';
                    current_token.location = (SourceLocation){ .line = current_token.location.line, .column = current_token.location.column + 1, .filename = current_token.location.filename };
                }
                break;
            }
            case ACTION_REDUCE: {
                int prod_id = action.target_state_or_production_id;
                const Production* p = &grammar->productions[prod_id]; // Use const Production*
                printf("REDUCE by %s -> ", p->left_symbol->name);
                for (int k = 0; k < p->right_count; ++k) {
                    printf("%s ", p->right_symbols[k]->name);
                }
                printf(" (Production %d)\n", prod_id);

                // Pop RHS symbols from stack and collect their AST nodes for semantic action
                ASTNode** children_ast_nodes = NULL;
                if (p->right_count > 0) {
                    children_ast_nodes = (ASTNode**)malloc(p->right_count * sizeof(ASTNode*));
                    if (!children_ast_nodes) {
                        fprintf(stderr, "Memory allocation failed for children_ast_nodes during reduction.\n");
                        exit(EXIT_FAILURE);
                    }
                    for (int k = 0; k < p->right_count; ++k) {
                        // Pop from the end of the stack, in reverse order of RHS
                        children_ast_nodes[p->right_count - 1 - k] = parse_stack[stack_ptr - 1 - k].ast_node;
                    }
                }

                stack_ptr -= p->right_count; // Pop states and AST nodes for RHS

                // Call semantic action to get AST node for LHS
                ASTNode* lhs_ast_node = NULL;
                if (p->semantic_action) {
                    lhs_ast_node = p->semantic_action(children_ast_nodes);
                } else {
                    // Fallback: if no semantic action, just pass through the first child if it exists
                    if (children_ast_nodes && p->right_count > 0) {
                         lhs_ast_node = children_ast_nodes[0];
                    }
                }
                if (children_ast_nodes) {
                    free(children_ast_nodes); // Free the temporary array of child pointers
                }


                // Push GoTo state for LHS non-terminal
                int state_after_pop = parse_stack[stack_ptr - 1].state;
                // Ensure non-terminal ID is within bounds for goto_table lookup
                if (p->left_symbol->id >= NUM_NON_TERMINALS_DEFINED) {
                    fprintf(stderr, "Parser Error: Non-terminal ID (%d) out of bounds for GOTO table lookup.\n", p->left_symbol->id);
                    return NULL;
                }
                int goto_state = goto_table[state_after_pop][p->left_symbol->id];

                if (goto_state == -1) {
                    fprintf(stderr, "Parser Error: No GOTO entry for state %d on non-terminal %s (ID: %d).\n",
                            state_after_pop, p->left_symbol->name, p->left_symbol->id);
                    return NULL;
                }

                parse_stack[stack_ptr].state = goto_state;
                parse_stack[stack_ptr].ast_node = lhs_ast_node; // Attach LHS AST node
                stack_ptr++;
                break;
            }
            case ACTION_ACCEPT:
                printf("ACCEPT\n");
                // The root of the AST should be the AST node associated with S' production.
                // After S' -> Program EOF reduction, the AST node for Program will be at stack[1].
                return parse_stack[1].ast_node; // Return the root AST node (Program)
            case ACTION_ERROR:
            default:
                fprintf(stderr, "\nParser Error: No valid action for state %d on token %s ('%s') at line %d, column %d.\n",
                        current_state, token_type_str(current_token_type), current_token.lexeme,
                        current_token.location.line, current_token.location.column);
                return NULL; // Parsing failed
        }
    }
}
