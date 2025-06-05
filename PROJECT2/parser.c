#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "parser.h" // Includes lexer.h indirectly

// --- Global Variables for Parser Tables and Sets ---
ActionEntry** action_table;
int** goto_table;
int num_states;
bool nullable_status[MAX_NON_TERMINALS]; // Indicates if a non-terminal can derive epsilon
TerminalSet firstSetsForNonTerminals[MAX_NON_TERMINALS];
TerminalSet followSetsForNonTerminals[MAX_NON_TERMINALS];
ItemSetList canonical_collection; // The collection of LR(1) item sets

// --- Parser Stack and State Management ---

// Parser stack entry
typedef struct {
    int state;
    GrammarSymbol* symbol; // The grammar symbol pushed onto the stack
    ASTNode* ast_node;     // The AST node associated with the symbol
} StackEntry;

// Parser state
typedef struct {
    StackEntry* stack;
    int stack_top;      // Index of the top element of the stack
    int stack_capacity; // Current capacity of the stack array
    Token* input;       // Array of input tokens
    int input_position; // Current position in the input token array
    int input_length;   // Total number of input tokens
    bool has_error;     // Flag to indicate if a parsing error occurred
    char* error_message; // Detailed error message
} Parser;

// --- Function Prototypes for internal parser functions (to resolve implicit declarations) ---
static void parser_push(Parser* parser, int state, GrammarSymbol* symbol, ASTNode* ast_node);
static void parser_init(Parser* parser, Token* input, int input_length);
static void parser_free(Parser* parser);
static int parser_current_state(Parser* parser);
static Token* parser_current_token(Parser* parser);
static void parser_advance_input(Parser* parser);
static ASTNode* parser_get_node_from_token(Token* token);


// --- Set Operations for FIRST/FOLLOW Sets (Bitmask Implementation) ---

// Adds a terminal ID to a TerminalSet
void set_add(TerminalSet s, int terminal_id) {
    if (terminal_id >= 0 && terminal_id < MAX_TERMINALS) {
        s[terminal_id / 8] |= (1 << (terminal_id % 8));
    }
}

// Checks if a TerminalSet contains a specific terminal ID
int set_contains(const TerminalSet s, int terminal_id) {
    if (terminal_id >= 0 && terminal_id < MAX_TERMINALS) {
        return (s[terminal_id / 8] & (1 << (terminal_id % 8))) != 0;
    }
    return 0; // Terminal ID out of bounds
}

// Clears (empties) a TerminalSet
void set_clear(TerminalSet s) {
    memset(s, 0, sizeof(TerminalSet));
}

// Computes the union of two TerminalSets (s1 = s1 U s2)
// Returns true if s1 was changed, false otherwise
bool set_union(TerminalSet s1, const TerminalSet s2) {
    bool changed = false;
    for (int i = 0; i < sizeof(TerminalSet); ++i) {
        unsigned char old_val = s1[i];
        s1[i] |= s2[i];
        if (s1[i] != old_val) {
            changed = true;
        }
    }
    return changed;
}


// --- FIRST and FOLLOW Set Computation ---

// Computes the nullable set for all non-terminals in the grammar
void compute_nullable_set(Grammar* grammar, bool* nullable) {
    printf("DEBUG: Entering compute_nullable_set.\n");
    // Initialize all non-terminals as not nullable
    for (int i = 0; i < grammar->non_terminal_count; i++) {
        // Ensure the non-terminal pointer is valid before dereferencing
        if (!grammar->non_terminals[i]) {
            fprintf(stderr, "ERROR: NULL non-terminal symbol pointer at index %d during nullable init.\n", i);
            exit(EXIT_FAILURE);
        }
        int nt_id = grammar->non_terminals[i]->id;
        // Check if nt_id is in valid range for nullable_status array
        if (nt_id < 0 || nt_id >= MAX_NON_TERMINALS) {
            fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) found at index %d for nullable_status array (size %d).\n",
                    nt_id, grammar->non_terminals[i]->name, i, MAX_NON_TERMINALS);
            exit(EXIT_FAILURE); // Crash early to find the source
        }
        nullable[nt_id] = false;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            // Ensure the left-hand side symbol pointer is valid
            if (!p->left_symbol) {
                fprintf(stderr, "ERROR: NULL left_symbol in production %d during nullable computation.\n", i);
                exit(EXIT_FAILURE);
            }
            int left_nt_id = p->left_symbol->id;
            // Check left_nt_id validity
            if (left_nt_id < 0 || left_nt_id >= MAX_NON_TERMINALS) {
                fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) for left-hand side in production %d.\n",
                        left_nt_id, p->left_symbol->name, i);
                exit(EXIT_FAILURE);
            }

            // If production is A -> epsilon (empty right side)
            if (p->right_count == 0) {
                if (!nullable[left_nt_id]) {
                    nullable[left_nt_id] = true;
                    changed = true;
                }
                continue;
            }

            // Check if all symbols on right side are nullable
            bool all_right_nullable = true;
            for (int j = 0; j < p->right_count; j++) {
                // Ensure the right-hand side symbol pointer is valid
                if (!p->right_symbols || !p->right_symbols[j]) {
                    fprintf(stderr, "ERROR: NULL right_symbol at position %d in production %d during nullable computation.\n", j, i);
                    exit(EXIT_FAILURE);
                }
                GrammarSymbol* current_symbol = p->right_symbols[j];
                if (current_symbol->type == SYMBOL_TERMINAL) {
                    all_right_nullable = false;
                    break; // A terminal means the sequence is not nullable
                } else { // SYMBOL_NONTERMINAL
                    // Check non-terminal ID validity
                    if (current_symbol->id < 0 || current_symbol->id >= MAX_NON_TERMINALS) {
                        fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) for right-hand side symbol %d in production %d.\n",
                                current_symbol->id, current_symbol->name, j, i);
                        exit(EXIT_FAILURE);
                    }
                    if (!nullable[current_symbol->id]) {
                        all_right_nullable = false;
                        break; // A non-nullable non-terminal means the sequence is not nullable
                    }
                }
            }

            if (all_right_nullable && !nullable[left_nt_id]) {
                nullable[left_nt_id] = true;
                changed = true;
            }
        }
    }
}

// Computes the FIRST sets for all non-terminals in the grammar
void compute_first_sets(Grammar* grammar) {
    printf("DEBUG: Entering compute_first_sets.\n");
    // Initialize all FIRST sets for non-terminals to empty
    for (int i = 0; i < grammar->non_terminal_count; i++) {
        if (!grammar->non_terminals[i]) {
            fprintf(stderr, "ERROR: NULL non-terminal symbol pointer at index %d during FIRST set init.\n", i);
            exit(EXIT_FAILURE);
        }
        int nt_id = grammar->non_terminals[i]->id;
        if (nt_id < 0 || nt_id >= MAX_NON_TERMINALS) {
            fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) found at index %d for firstSetsForNonTerminals array (size %d).\n",
                    nt_id, grammar->non_terminals[i]->name, i, MAX_NON_TERMINALS);
            exit(EXIT_FAILURE);
        }
        set_clear(firstSetsForNonTerminals[nt_id]);
    }

    // Ensure nullable set is computed first
    compute_nullable_set(grammar, nullable_status);

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            if (!p->left_symbol) {
                fprintf(stderr, "ERROR: NULL left_symbol in production %d during FIRST computation.\n", i);
                exit(EXIT_FAILURE);
            }
            int A_id = p->left_symbol->id;
            if (A_id < 0 || A_id >= MAX_NON_TERMINALS) {
                fprintf(stderr, "ERROR: Invalid left-hand side non-terminal ID %d (name: %s) in production %d during FIRST computation.\n",
                        A_id, p->left_symbol->name, i);
                exit(EXIT_FAILURE);
            }
            printf("DEBUG: Processing production %d: %s -> ", i, p->left_symbol->name);
            for(int k=0; k<p->right_count; ++k) {
                if(p->right_symbols && p->right_symbols[k]) {
                    printf("%s ", p->right_symbols[k]->name);
                } else {
                    printf("(NULL) ");
                }
            }
            printf("(A_id=%d)\n", A_id);


            // For each symbol Y_j in alpha (right side of production A -> Y_1 Y_2 ... Y_k)
            for (int j = 0; j < p->right_count; j++) {
                if (!p->right_symbols || !p->right_symbols[j]) {
                    fprintf(stderr, "ERROR: NULL right_symbol at position %d in production %d during FIRST computation.\n", j, i);
                    exit(EXIT_FAILURE);
                }
                GrammarSymbol* current_symbol = p->right_symbols[j];
                printf("DEBUG:   Right-hand side symbol %d: %s (Type=%d, ID=%d)\n",
                       j, current_symbol->name, current_symbol->type, current_symbol->id);

                if (current_symbol->type == SYMBOL_TERMINAL) {
                    // If Y_j is a terminal, add Y_j to FIRST(A) and break
                    // Check terminal ID validity
                    if (current_symbol->id < 0 || current_symbol->id >= MAX_TERMINALS) {
                        fprintf(stderr, "ERROR: Invalid terminal ID %d (name: %s) for right-hand side symbol %d in production %d during FIRST computation.\n",
                                current_symbol->id, current_symbol->name, j, i);
                        exit(EXIT_FAILURE);
                    }
                    if (!set_contains(firstSetsForNonTerminals[A_id], current_symbol->id)) {
                        set_add(firstSetsForNonTerminals[A_id], current_symbol->id);
                        changed = true;
                    }
                    break;
                } else { // current_symbol is a non-terminal
                    // Check non-terminal ID validity
                    if (current_symbol->id < 0 || current_symbol->id >= MAX_NON_TERMINALS) {
                        fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) for right-hand side symbol %d in production %d during FIRST computation.\n",
                                current_symbol->id, current_symbol->name, j, i);
                        exit(EXIT_FAILURE);
                    }
                    // Add FIRST(Y_j) to FIRST(A)
                    printf("DEBUG:     Merging FIRST(%s) into FIRST(%s)\n",
                           grammar->non_terminals[current_symbol->id]->name, p->left_symbol->name);
                    if (set_union(firstSetsForNonTerminals[A_id],
                                  firstSetsForNonTerminals[current_symbol->id])) {
                        changed = true;
                    }

                    // If Y_j is not nullable, break
                    if (!nullable_status[current_symbol->id]) {
                        break;
                    }
                }
            }
        }
    }
}

// Computes the FOLLOW sets for all non-terminals in the grammar
void compute_follow_sets(Grammar* grammar) {
    printf("DEBUG: Entering compute_follow_sets.\n");
    // Initialize FOLLOW sets as empty for all non-terminals
    for (int i = 0; i < grammar->non_terminal_count; i++) {
        if (!grammar->non_terminals[i]) {
            fprintf(stderr, "ERROR: NULL non-terminal symbol pointer at index %d during FOLLOW set init.\n", i);
            exit(EXIT_FAILURE);
        }
        int nt_id = grammar->non_terminals[i]->id;
        if (nt_id < 0 || nt_id >= MAX_NON_TERMINALS) {
            fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) found at index %d for followSetsForNonTerminals array (size %d).\n",
                    nt_id, grammar->non_terminals[i]->name, i, MAX_NON_TERMINALS);
            exit(EXIT_FAILURE);
        }
        set_clear(followSetsForNonTerminals[nt_id]);
    }

    // Ensure nullable and FIRST sets are computed first
    compute_nullable_set(grammar, nullable_status);
    compute_first_sets(grammar);

    // Rule 1: Add $ to FOLLOW(start_symbol)
    if (!grammar->start_symbol) {
        fprintf(stderr, "ERROR: NULL start_symbol in grammar during FOLLOW set computation.\n");
        exit(EXIT_FAILURE);
    }
    if (grammar->start_symbol->id < 0 || grammar->start_symbol->id >= MAX_NON_TERMINALS) {
        fprintf(stderr, "ERROR: Invalid start_symbol ID %d (name: %s) during FOLLOW set computation.\n",
                grammar->start_symbol->id, grammar->start_symbol->name);
        exit(EXIT_FAILURE);
    }
    set_add(followSetsForNonTerminals[grammar->start_symbol->id], T_EOF);

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            if (!p->left_symbol) {
                fprintf(stderr, "ERROR: NULL left_symbol in production %d during FOLLOW computation.\n", i);
                exit(EXIT_FAILURE);
            }
            int A_id = p->left_symbol->id;
            if (A_id < 0 || A_id >= MAX_NON_TERMINALS) {
                fprintf(stderr, "ERROR: Invalid left-hand side non-terminal ID %d (name: %s) in production %d during FOLLOW computation.\n",
                        A_id, p->left_symbol->name, i);
                exit(EXIT_FAILURE);
            }

            for (int j = 0; j < p->right_count; j++) {
                if (!p->right_symbols || !p->right_symbols[j]) {
                    fprintf(stderr, "ERROR: NULL right_symbol at position %d in production %d during FOLLOW computation.\n", j, i);
                    exit(EXIT_FAILURE);
                }
                GrammarSymbol* B = p->right_symbols[j]; // The current symbol B

                if (B->type == SYMBOL_NONTERMINAL) {
                    // Check non-terminal ID validity for B
                    if (B->id < 0 || B->id >= MAX_NON_TERMINALS) {
                        fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) for symbol B at position %d in production %d during FOLLOW computation.\n",
                                B->id, B->name, j, i);
                        exit(EXIT_FAILURE);
                    }

                    // Handle B -> gamma (beta is the sequence after B)
                    bool beta_is_nullable = true;
                    for (int k = j + 1; k < p->right_count; k++) {
                        if (!p->right_symbols[k]) {
                            fprintf(stderr, "ERROR: NULL right_symbol at position %d (beta) in production %d during FOLLOW computation.\n", k, i);
                            exit(EXIT_FAILURE);
                        }
                        GrammarSymbol* Y = p->right_symbols[k]; // Symbol in beta

                        if (Y->type == SYMBOL_TERMINAL) {
                            // Check terminal ID validity for Y
                            if (Y->id < 0 || Y->id >= MAX_TERMINALS) {
                                fprintf(stderr, "ERROR: Invalid terminal ID %d (name: %s) for symbol Y at position %d (beta) in production %d during FOLLOW computation.\n",
                                        Y->id, Y->name, k, i);
                                exit(EXIT_FAILURE);
                            }
                            // Rule 2a: If B -> alpha Y beta and Y is a terminal, then Y is in FOLLOW(B)
                            if (!set_contains(followSetsForNonTerminals[B->id], Y->id)) {
                                set_add(followSetsForNonTerminals[B->id], Y->id);
                                changed = true;
                            }
                            beta_is_nullable = false; // A terminal means beta is not nullable
                            break;
                        } else { // Y is a non-terminal
                            // Check non-terminal ID validity for Y
                            if (Y->id < 0 || Y->id >= MAX_NON_TERMINALS) {
                                fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) for symbol Y at position %d (beta) in production %d during FOLLOW computation.\n",
                                        Y->id, Y->name, k, i);
                                exit(EXIT_FAILURE);
                            }
                            // Rule 2b: If B -> alpha Y beta and Y is a non-terminal, add FIRST(Y) to FOLLOW(B)
                            if (set_union(followSetsForNonTerminals[B->id],
                                          firstSetsForNonTerminals[Y->id])) {
                                changed = true;
                            }
                            if (!nullable_status[Y->id]) {
                                beta_is_nullable = false; // A non-nullable non-terminal means beta is not nullable
                                break;
                            }
                        }
                    }

                    // Rule 3: If B -> alpha beta and beta is nullable (or empty), add FOLLOW(A) to FOLLOW(B)
                    if (beta_is_nullable) {
                        if (set_union(followSetsForNonTerminals[B->id],
                                      followSetsForNonTerminals[A_id])) {
                            changed = true;
                        }
                    }
                }
            }
        }
    }
}


// --- LR(1) Item Set Operations ---

// Initializes an ItemSet
void item_set_init(ItemSet* set) {
    set->items = NULL;
    set->item_count = 0;
    set->capacity = 0;
    set->state_id = -1;
}

// Frees memory allocated for items within an ItemSet
void item_set_free(ItemSet* set) {
    if (set && set->items) {
        free(set->items);
        set->items = NULL;
    }
    if (set) {
        set->item_count = 0;
        set->capacity = 0;
    }
}

// Checks if two LR items have the same core (production_id and dot_position)
static bool lr_item_core_equal(const LRItem* item1, const LRItem* item2) {
    return (item1->production_id == item2->production_id &&
            item1->dot_position == item2->dot_position);
}

// Finds an LRItem by its core within an ItemSet
// Returns pointer to item if found, NULL otherwise.
static LRItem* find_item_by_core(ItemSet* set, int prod_id, int dot_pos) {
    for (int i = 0; i < set->item_count; ++i) {
        if (set->items[i].production_id == prod_id &&
            set->items[i].dot_position == dot_pos) {
            return &set->items[i];
        }
    }
    return NULL;
}

// Adds an LRItem to an ItemSet. Handles resizing.
// This function assumes the item's lookahead set is already computed or will be merged.
static void item_set_add_item(ItemSet* set, LRItem item) {
    if (set->item_count >= set->capacity) {
        set->capacity = (set->capacity == 0) ? 4 : set->capacity * 2;
        set->items = (LRItem*)realloc(set->items, set->capacity * sizeof(LRItem));
        if (set->items == NULL) {
            fprintf(stderr, "Memory allocation failed for ItemSet items.\n");
            exit(EXIT_FAILURE);
        }
    }
    set->items[set->item_count++] = item;
}

// Computes the closure of an ItemSet for LR(1) items
void closure(ItemSet *items_set, Grammar *grammar, bool* nullable) {
    bool changed = true;

    while (changed) {
        changed = false;

        // Iterate over a snapshot of current items to avoid issues with resizing
        // and to process newly added items in subsequent iterations
        LRItem* current_items_snapshot = (LRItem*)malloc(items_set->item_count * sizeof(LRItem));
        if (current_items_snapshot == NULL) {
            fprintf(stderr, "Memory allocation failed for current_items_snapshot in closure.\n");
            exit(EXIT_FAILURE);
        }
        memcpy(current_items_snapshot, items_set->items, items_set->item_count * sizeof(LRItem));
        int snapshot_count = items_set->item_count;

        for (int i = 0; i < snapshot_count; ++i) {
            LRItem current_item = current_items_snapshot[i];
            Production* p = &grammar->productions[current_item.production_id];

            // If the dot is not at the end of the production
            if (current_item.dot_position < p->right_count) {
                // Ensure p->right_symbols and p->right_symbols[current_item.dot_position] are valid
                if (!p->right_symbols || !p->right_symbols[current_item.dot_position]) {
                    fprintf(stderr, "ERROR: NULL symbol after dot in production %d at position %d during closure.\n",
                            current_item.production_id, current_item.dot_position);
                    exit(EXIT_FAILURE);
                }
                GrammarSymbol* symbol_after_dot = p->right_symbols[current_item.dot_position];

                if (symbol_after_dot->type == SYMBOL_NONTERMINAL) {
                    // Check non-terminal ID validity
                    if (symbol_after_dot->id < 0 || symbol_after_dot->id >= MAX_NON_TERMINALS) {
                        fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) after dot in production %d during closure.\n",
                                symbol_after_dot->id, symbol_after_dot->name, current_item.production_id);
                        exit(EXIT_FAILURE);
                    }

                    // For each production B -> gamma where B is symbol_after_dot
                    for (int prod_idx = 0; prod_idx < grammar->production_count; ++prod_idx) {
                        Production* B_prod = &grammar->productions[prod_idx];

                        if (!B_prod->left_symbol) {
                            fprintf(stderr, "ERROR: NULL left_symbol in production %d during closure (B_prod).\n", prod_idx);
                            exit(EXIT_FAILURE);
                        }
                        if (B_prod->left_symbol->id == symbol_after_dot->id) {
                            // Create a new item B -> .gamma
                            LRItem new_item_candidate = {
                                .production_id = B_prod->production_id,
                                .dot_position = 0
                            };
                            set_clear(new_item_candidate.lookahead_set); // Clear for fresh lookahead calculation

                            // Calculate FIRST(beta) where beta is the sequence after symbol_after_dot
                            TerminalSet first_beta;
                            set_clear(first_beta);
                            bool beta_is_nullable = true;

                            for (int k = current_item.dot_position + 1; k < p->right_count; ++k) {
                                if (!p->right_symbols[k]) {
                                    fprintf(stderr, "ERROR: NULL symbol Yk at position %d (beta) in production %d during closure.\n", k, current_item.production_id);
                                    exit(EXIT_FAILURE);
                                }
                                GrammarSymbol* Yk = p->right_symbols[k];

                                if (Yk->type == SYMBOL_TERMINAL) {
                                    if (Yk->id < 0 || Yk->id >= MAX_TERMINALS) {
                                        fprintf(stderr, "ERROR: Invalid terminal ID %d (name: %s) for Yk at position %d (beta) in production %d during closure.\n",
                                                Yk->id, Yk->name, k, current_item.production_id);
                                        exit(EXIT_FAILURE);
                                    }
                                    set_add(first_beta, Yk->id);
                                    beta_is_nullable = false;
                                    break;
                                } else { // Non-terminal
                                    if (Yk->id < 0 || Yk->id >= MAX_NON_TERMINALS) {
                                        fprintf(stderr, "ERROR: Invalid non-terminal ID %d (name: %s) for Yk at position %d (beta) in production %d during closure.\n",
                                                Yk->id, Yk->name, k, current_item.production_id);
                                        exit(EXIT_FAILURE);
                                    }
                                    set_union(first_beta, firstSetsForNonTerminals[Yk->id]);
                                    if (!nullable[Yk->id]) {
                                        beta_is_nullable = false;
                                        break;
                                    }
                                }
                            }

                            // Add FIRST(beta) to the new item's lookahead
                            set_union(new_item_candidate.lookahead_set, first_beta);

                            // If beta is nullable, propagate lookahead from A -> alpha .B beta
                            if (beta_is_nullable) {
                                set_union(new_item_candidate.lookahead_set, current_item.lookahead_set);
                            }

                            // Check if this new item (core + lookahead) already exists or needs lookahead merge
                            LRItem* existing_item = find_item_by_core(items_set,
                                                                       new_item_candidate.production_id,
                                                                       new_item_candidate.dot_position);

                            if (existing_item == NULL) {
                                // New item, add it to the set
                                item_set_add_item(items_set, new_item_candidate);
                                changed = true; // A new item was added
                            } else {
                                // Item already exists, merge lookahead sets
                                if (set_union(existing_item->lookahead_set, new_item_candidate.lookahead_set)) {
                                    changed = true; // Lookahead set of an existing item changed
                                }
                            }
                        }
                    }
                }
            }
        }
        free(current_items_snapshot);
    }
}

// Checks if two ItemSets are equal (same items with same lookaheads)
static bool item_sets_equal(const ItemSet* set1, const ItemSet* set2) {
    if (set1->item_count != set2->item_count) {
        return false;
    }

    // For each item in set1, find an identical item in set2 (core + lookahead)
    for (int i = 0; i < set1->item_count; ++i) {
        bool found = false;
        for (int j = 0; j < set2->item_count; ++j) {
            if (lr_item_core_equal(&set1->items[i], &set2->items[j])) {
                // Now check if lookahead sets are identical
                if (memcmp(set1->items[i].lookahead_set, set2->items[j].lookahead_set, sizeof(TerminalSet)) == 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;
    }

    return true;
}

// Initializes an ItemSetList
static void item_set_list_init(ItemSetList* list) {
    list->sets = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Adds an ItemSet pointer to an ItemSetList. Handles resizing.
static void item_set_list_add(ItemSetList* list, ItemSet* new_set_ptr) {
    if (list->count >= list->capacity) {
        list->capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        list->sets = (ItemSet**)realloc(list->sets, list->capacity * sizeof(ItemSet*));
        if (list->sets == NULL) {
            fprintf(stderr, "Memory allocation failed for ItemSetList.\n");
            exit(EXIT_FAILURE);
        }
    }
    list->sets[list->count++] = new_set_ptr;
}

// Finds an ItemSet in an ItemSetList and returns its index, or -1 if not found
static int find_item_set_in_list(const ItemSetList* list, const ItemSet* target_set) {
    for (int i = 0; i < list->count; ++i) {
        if (item_sets_equal(list->sets[i], target_set)) {
            return i;
        }
    }
    return -1;
}

// Prints the contents of an ItemSet for debugging
static void print_item_set(const ItemSet* set, Grammar* grammar) {
    printf("I%d:\n", set->state_id);
    for (int i = 0; i < set->item_count; ++i) {
        LRItem item = set->items[i];
        Production* p = &grammar->productions[item.production_id];
        printf("  %s ->", p->left_symbol->name);
        for (int j = 0; j < p->right_count; ++j) {
            if (j == item.dot_position) {
                printf(" .");
            }
            printf(" %s", p->right_symbols[j]->name);
        }
        if (item.dot_position == p->right_count) {
            printf(" .");
        }
        printf(", {");
        bool first_lookahead = true;
        for (int k = 0; k < MAX_TERMINALS; ++k) {
            if (set_contains(item.lookahead_set, k)) {
                if (!first_lookahead) {
                    printf(", ");
                }
                // Find terminal name for printing
                const char* term_name = "UNKNOWN";
                for (int t_idx = 0; t_idx < grammar->terminal_count; ++t_idx) {
                    if (grammar->terminals[t_idx]->id == k) {
                        term_name = grammar->terminals[t_idx]->name;
                        break;
                    }
                }
                printf("%s", term_name);
                first_lookahead = false;
            }
        }
        printf("}\n");
    }
}

// Computes the GOTO function: GOTO(I, X)
static ItemSet* goto_set(const ItemSet *I, GrammarSymbol *X, Grammar *grammar, bool* nullable) {
    ItemSet* J = (ItemSet*)malloc(sizeof(ItemSet));
    if (J == NULL) {
        fprintf(stderr, "Memory allocation failed for new ItemSet in goto_set.\n");
        exit(EXIT_FAILURE);
    }
    item_set_init(J);

    for (int i = 0; i < I->item_count; ++i) {
        LRItem current_item = I->items[i];
        Production* p = &grammar->productions[current_item.production_id];

        // If dot is before X
        if (current_item.dot_position < p->right_count &&
            p->right_symbols[current_item.dot_position]->id == X->id) {

            LRItem new_item = {
                .production_id = current_item.production_id,
                .dot_position = current_item.dot_position + 1
            };
            // Lookahead set is copied directly from the source item
            memcpy(new_item.lookahead_set, current_item.lookahead_set, sizeof(TerminalSet));
            item_set_add_item(J, new_item);
        }
    }

    // Apply closure to the newly formed set J
    closure(J, grammar, nullable);
    return J;
}

// Creates the canonical collection of LR(1) item sets
void create_lr1_sets(Grammar* grammar) {
    item_set_list_init(&canonical_collection);

    // Ensure nullable and FIRST sets are computed
    compute_nullable_set(grammar, nullable_status);
    compute_first_sets(grammar);

    // Create initial item set I0: closure({S' -> .S, $})
    ItemSet* I0 = (ItemSet*)malloc(sizeof(ItemSet));
    if (I0 == NULL) {
        fprintf(stderr, "Memory allocation failed for I0.\n");
        exit(EXIT_FAILURE);
    }
    item_set_init(I0);
    I0->state_id = 0;

    LRItem initial_item = {
        .production_id = 0, // Assuming production 0 is S' -> Program
        .dot_position = 0
    };
    set_clear(initial_item.lookahead_set);
    set_add(initial_item.lookahead_set, T_EOF); // Add EOF as lookahead for S' -> .Program
    item_set_add_item(I0, initial_item);
    closure(I0, grammar, nullable_status);

    item_set_list_add(&canonical_collection, I0);

    // Use a queue for BFS traversal of item sets
    ItemSet** queue = (ItemSet**)malloc(sizeof(ItemSet*) * 100); // Initial queue capacity
    int queue_size = 0;
    int queue_front = 0;

    if (queue_size < 100) {
        queue[queue_size++] = I0;
    } else {
        fprintf(stderr, "Initial queue capacity exceeded. Increase queue size.\n");
        // This is a critical error for the algorithm to proceed correctly.
        // In a real application, consider dynamic resizing for the queue as well.
        exit(EXIT_FAILURE);
    }

    while (queue_front < queue_size) {
        ItemSet* current_I = queue[queue_front++];

        // Collect all grammar symbols (terminals and non-terminals)
        GrammarSymbol** all_symbols = (GrammarSymbol**)malloc(
            sizeof(GrammarSymbol*) * (grammar->terminal_count + grammar->non_terminal_count));
        int symbol_count = 0;

        for (int i = 0; i < grammar->terminal_count; i++) {
            all_symbols[symbol_count++] = grammar->terminals[i];
        }
        for (int i = 0; i < grammar->non_terminal_count; i++) {
            all_symbols[symbol_count++] = grammar->non_terminals[i];
        }

        // For each grammar symbol X, compute GOTO(current_I, X)
        for (int i = 0; i < symbol_count; i++) {
            GrammarSymbol* X = all_symbols[i];
            ItemSet* J = goto_set(current_I, X, grammar, nullable_status);

            if (J->item_count > 0) { // If the GOTO set is not empty
                int existing_idx = find_item_set_in_list(&canonical_collection, J);

                if (existing_idx == -1) {
                    // New item set found, add to canonical collection and queue
                    J->state_id = canonical_collection.count;
                    item_set_list_add(&canonical_collection, J);

                    if (queue_size < 100) { // Check queue capacity
                        queue[queue_size++] = J;
                    } else {
                         fprintf(stderr, "Queue capacity exceeded for new state. Increase queue size.\n");
                         item_set_free(J); // Free J if it can't be queued
                         free(J);
                    }
                } else {
                    // Item set already exists, free the newly created duplicate
                    item_set_free(J);
                    free(J);
                }
            } else {
                // GOTO set is empty, free it
                item_set_free(J);
                free(J);
            }
        }
        free(all_symbols);
    }

    printf("\n--- Generated LR(1) Item Sets (Total States: %d) ---\n", canonical_collection.count);
    for (int i = 0; i < canonical_collection.count; ++i) {
        print_item_set(canonical_collection.sets[i], grammar);
        printf("\n");
    }

    free(queue);
}


// --- Parsing Table Construction ---

// Initializes the action and goto parsing tables
static void initialize_parsing_tables(int total_states, Grammar* grammar) {
    num_states = total_states;

    // Allocate action table
    action_table = (ActionEntry**)malloc(num_states * sizeof(ActionEntry*));
    if (!action_table) { fprintf(stderr, "Failed to allocate action_table rows.\n"); exit(EXIT_FAILURE); }
    for (int i = 0; i < num_states; ++i) {
        action_table[i] = (ActionEntry*)malloc(MAX_TERMINALS * sizeof(ActionEntry));
        if (!action_table[i]) { fprintf(stderr, "Failed to allocate action_table columns for state %d.\n", i); exit(EXIT_FAILURE); }
        for (int j = 0; j < MAX_TERMINALS; ++j) {
            action_table[i][j].type = ACTION_ERROR;
            action_table[i][j].target_state_or_production_id = -1;
        }
    }

    // Allocate goto table
    goto_table = (int**)malloc(num_states * sizeof(int*));
    if (!goto_table) { fprintf(stderr, "Failed to allocate goto_table rows.\n"); exit(EXIT_FAILURE); }
    for (int i = 0; i < num_states; ++i) {
        goto_table[i] = (int*)malloc(MAX_NON_TERMINALS * sizeof(int));
        if (!goto_table[i]) { fprintf(stderr, "Failed to allocate goto_table columns for state %d.\n", i); exit(EXIT_FAILURE); }
        for (int j = 0; j < MAX_NON_TERMINALS; ++j) {
            goto_table[i][j] = -1; // Initialize with -1 (no transition)
        }
    }
}

// Frees memory allocated for the parsing tables
void free_parsing_tables() {
    if (action_table) {
        for (int i = 0; i < num_states; ++i) {
            free(action_table[i]);
        }
        free(action_table);
        action_table = NULL;
    }
    if (goto_table) {
        for (int i = 0; i < num_states; ++i) {
            free(goto_table[i]);
        }
        free(goto_table);
        goto_table = NULL;
    }
}

// Prints the ACTION table for debugging
static void print_action_table(Grammar* grammar) {
    printf("\n--- ACTION Table ---\n");
    printf("%-5s", "State");
    for (int i = 0; i < grammar->terminal_count; ++i) {
        printf("%-10s", grammar->terminals[i]->name);
    }
    printf("\n");

    for (int i = 0; i < num_states; ++i) {
        printf("%-5d", i);
        for (int j = 0; j < grammar->terminal_count; ++j) {
            int terminal_id = grammar->terminals[j]->id;
            if (terminal_id < 0 || terminal_id >= MAX_TERMINALS) {
                 printf("ERROR     "); // Out of bounds ID
                 continue;
            }

            ActionEntry entry = action_table[i][terminal_id];
            switch (entry.type) {
                case ACTION_SHIFT:
                    printf("s%-9d", entry.target_state_or_production_id);
                    break;
                case ACTION_REDUCE:
                    printf("r%-9d", entry.target_state_or_production_id);
                    break;
                case ACTION_ACCEPT:
                    printf("ACC       ");
                    break;
                case ACTION_ERROR:
                    printf("          "); // Empty for error
                    break;
            }
        }
        printf("\n");
    }
}

// Prints the GOTO table for debugging
static void print_goto_table(Grammar* grammar) {
    printf("\n--- GOTO Table ---\n");
    printf("%-5s", "State");
    for (int i = 0; i < grammar->non_terminal_count; ++i) {
        printf("%-10s", grammar->non_terminals[i]->name);
    }
    printf("\n");

    for (int i = 0; i < num_states; ++i) {
        printf("%-5d", i);
        for (int j = 0; j < grammar->non_terminal_count; ++j) {
            int non_terminal_id = grammar->non_terminals[j]->id;
            if (non_terminal_id < 0 || non_terminal_id >= MAX_NON_TERMINALS) {
                printf("ERROR     "); // Out of bounds ID
                continue;
            }
            int target_state = goto_table[i][non_terminal_id];
            if (target_state != -1) {
                printf("%-10d", target_state);
            } else {
                printf("          "); // Empty for no transition
            }
        }
        printf("\n");
    }
}

// Builds the LR parsing tables (ACTION and GOTO)
void build_parsing_tables(Grammar* grammar, ItemSetList* canonical_collection_ptr, bool* nullable_status_ptr) {
    // Initialize tables based on the total number of states
    initialize_parsing_tables(canonical_collection_ptr->count, grammar);

    printf("\n--- Building Parsing Tables ---\n");

    for (int i = 0; i < canonical_collection_ptr->count; ++i) { // For each state I_i
        ItemSet* current_state = canonical_collection_ptr->sets[i];

        // Process items for Action table entries (Shift and Reduce)
        for (int k = 0; k < current_state->item_count; ++k) {
            LRItem item = current_state->items[k];
            Production* p = &grammar->productions[item.production_id];

            if (item.dot_position < p->right_count) {
                // Case 1: Shift Action (X is a terminal)
                GrammarSymbol* symbol_after_dot = p->right_symbols[item.dot_position];
                if (symbol_after_dot->type == SYMBOL_TERMINAL) {
                    ItemSet* next_state = goto_set(current_state, symbol_after_dot, grammar, nullable_status_ptr);
                    int target_state_id = find_item_set_in_list(canonical_collection_ptr, next_state);

                    if (target_state_id == -1) {
                        fprintf(stderr, "Error: GOTO state not found in canonical collection for shift. This indicates a bug in LR(1) set generation.\n");
                        item_set_free(next_state);
                        free(next_state);
                        continue;
                    }

                    int terminal_id = symbol_after_dot->id;
                    if (terminal_id >= 0 && terminal_id < MAX_TERMINALS) {
                        if (action_table[current_state->state_id][terminal_id].type != ACTION_ERROR) {
                            // CONFLICT DETECTION: Shift/Shift or Shift/Reduce
                            fprintf(stderr, "Conflict detected at ACTION[%d][%s] (terminal ID %d)\n",
                                    current_state->state_id, symbol_after_dot->name, terminal_id);
                            fprintf(stderr, "  Existing: Type %d (Shift:%d, Reduce:%d, Accept:%d), Value %d\n",
                                    action_table[current_state->state_id][terminal_id].type, ACTION_SHIFT, ACTION_REDUCE, ACTION_ACCEPT,
                                    action_table[current_state->state_id][terminal_id].target_state_or_production_id);
                            fprintf(stderr, "  New: SHIFT %d\n", target_state_id);

                            // Simple conflict resolution: Favor Shift over Reduce
                            if (action_table[current_state->state_id][terminal_id].type == ACTION_REDUCE) {
                                printf("  Resolved Shift/Reduce conflict in favor of SHIFT.\n");
                                // Keep the existing shift if it was already a shift, otherwise overwrite.
                                // If it was a reduce, the shift takes precedence.
                                action_table[current_state->state_id][terminal_id].type = ACTION_SHIFT;
                                action_table[current_state->state_id][terminal_id].target_state_or_production_id = target_state_id;
                            }
                            // If it was already a shift, no change needed.
                        } else {
                            action_table[current_state->state_id][terminal_id].type = ACTION_SHIFT;
                            action_table[current_state->state_id][terminal_id].target_state_or_production_id = target_state_id;
                        }
                    }
                    item_set_free(next_state); // Free the temporary GOTO set
                    free(next_state);
                }
            } else {
                // Case 2: Reduce or Accept Action (dot is at the end of production)
                if (p->left_symbol->id == NT_S_PRIME && set_contains(item.lookahead_set, T_EOF)) {
                    // Accept action: S' -> Program . , lookahead $
                    if (T_EOF >= 0 && T_EOF < MAX_TERMINALS) {
                        if (action_table[current_state->state_id][T_EOF].type != ACTION_ERROR) {
                            fprintf(stderr, "Conflict detected at ACTION[%d][EOF]. Existing type %d.\n",
                                    current_state->state_id, action_table[current_state->state_id][T_EOF].type);
                            // Accept should ideally be unique. This indicates a severe grammar issue.
                        }
                        action_table[current_state->state_id][T_EOF].type = ACTION_ACCEPT;
                        action_table[current_state->state_id][T_EOF].target_state_or_production_id = -1; // No target state/prod ID for ACCEPT
                    }
                } else {
                    // Reduce action: A -> alpha . , for all 'a' in LOOKAHEAD(item)
                    for (int term_id = 0; term_id < MAX_TERMINALS; ++term_id) {
                        if (set_contains(item.lookahead_set, term_id)) {
                            if (action_table[current_state->state_id][term_id].type != ACTION_ERROR) {
                                // CONFLICT DETECTION: Reduce/Reduce or Shift/Reduce
                                fprintf(stderr, "Conflict detected at ACTION[%d][terminal ID %d]\n",
                                        current_state->state_id, term_id);
                                fprintf(stderr, "  Existing: Type %d (Shift:%d, Reduce:%d, Accept:%d), Value %d\n",
                                        action_table[current_state->state_id][term_id].type, ACTION_SHIFT, ACTION_REDUCE, ACTION_ACCEPT,
                                        action_table[current_state->state_id][term_id].target_state_or_production_id);
                                fprintf(stderr, "  New: REDUCE by Production %d (%s -> ...)\n",
                                        p->production_id, p->left_symbol->name);

                                // Simple conflict resolution: Favor Shift over Reduce, otherwise keep first Reduce
                                if (action_table[current_state->state_id][term_id].type == ACTION_SHIFT) {
                                    printf("  Resolved Shift/Reduce conflict in favor of SHIFT (keeping existing).\n");
                                    continue; // Keep the existing shift action
                                } else {
                                    printf("  Resolved Reduce/Reduce conflict by choosing first encountered production (keeping existing).\n");
                                    continue; // Keep the first reduce action
                                }
                            }
                            action_table[current_state->state_id][term_id].type = ACTION_REDUCE;
                            action_table[current_state->state_id][term_id].target_state_or_production_id = p->production_id;
                        }
                    }
                }
            }
        }

        // Process non-terminals for Goto table entries
        for (int j = 0; j < grammar->non_terminal_count; ++j) {
            GrammarSymbol* NT = grammar->non_terminals[j];
            ItemSet* next_state = goto_set(current_state, NT, grammar, nullable_status_ptr);

            if (next_state->item_count > 0) {
                int target_state_id = find_item_set_in_list(canonical_collection_ptr, next_state);
                if (target_state_id == -1) {
                    fprintf(stderr, "Error: GOTO state for non-terminal not found. This indicates a bug in LR(1) set generation.\n");
                    item_set_free(next_state);
                    free(next_state);
                    continue;
                }
                int non_terminal_id = NT->id;
                if (non_terminal_id >= 0 && non_terminal_id < MAX_NON_TERMINALS) {
                    goto_table[current_state->state_id][non_terminal_id] = target_state_id;
                }
            }
            item_set_free(next_state); // Free the temporary GOTO set
            free(next_state);
        }
    }

    print_action_table(grammar);
    print_goto_table(grammar);
}


// --- AST Node Creation Functions ---

// General helper to create a new AST node and initialize common fields
ASTNode* create_ast_node(ASTNodeType type, SourceLocation location) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Memory allocation failed for AST node\n");
        exit(EXIT_FAILURE);
    }

    node->type = type;
    node->children = NULL;
    node->child_count = 0;
    node->location = location; // Set location

    // Initialize union data to safe values
    memset(&node->data, 0, sizeof(node->data));

    return node;
}

ASTNode* create_binary_op_node(BinaryOperator op, ASTNode* left, ASTNode* right, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_BINARY_OP, location);
    node->data.binary_op.operator = op;
    node->data.binary_op.left = left;
    node->data.binary_op.right = right;

    node->child_count = 2;
    node->children = (ASTNode**)malloc(2 * sizeof(ASTNode*));
    if (!node->children) {
        fprintf(stderr, "Memory allocation failed for binary_op children.\n");
        free(node); exit(EXIT_FAILURE);
    }
    node->children[0] = left;
    node->children[1] = right;

    return node;
}

ASTNode* create_unary_op_node(UnaryOperator op, ASTNode* operand, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_UNARY_OP, location);
    node->data.unary_op.operator = op;
    node->data.unary_op.operand = operand;

    node->child_count = 1;
    node->children = (ASTNode**)malloc(sizeof(ASTNode*));
    if (!node->children) {
        fprintf(stderr, "Memory allocation failed for unary_op children.\n");
        free(node); exit(EXIT_FAILURE);
    }
    node->children[0] = operand;

    return node;
}

ASTNode* create_identifier_node(const char* name, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_IDENTIFIER, location);
    node->data.identifier.name = strdup(name);
    if (!node->data.identifier.name) {
        fprintf(stderr, "Memory allocation failed for identifier name.\n");
        free(node); exit(EXIT_FAILURE);
    }
    return node;
}

ASTNode* create_number_node(long long value, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_NUMBER, location);
    node->data.number.value = value;
    return node;
}

ASTNode* create_string_node(const char* value, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_STRING, location);
    node->data.string.value = strdup(value);
    if (!node->data.string.value) {
        fprintf(stderr, "Memory allocation failed for string value.\n");
        free(node); exit(EXIT_FAILURE);
    }
    return node;
}

ASTNode* create_assignment_node(ASTNode* target, ASTNode* value, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_ASSIGNMENT, location);
    node->data.assignment.target = target;
    node->data.assignment.value = value;

    node->child_count = 2;
    node->children = (ASTNode**)malloc(2 * sizeof(ASTNode*));
    if (!node->children) {
        fprintf(stderr, "Memory allocation failed for assignment children.\n");
        free(node); exit(EXIT_FAILURE);
    }
    node->children[0] = target;
    node->children[1] = value;

    return node;
}

ASTNode* create_write_statement_node(ASTNode* expression, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_WRITE_STATEMENT, location);
    node->data.write_stmt.expression = expression;

    node->child_count = 1;
    node->children = (ASTNode**)malloc(sizeof(ASTNode*));
    if (!node->children) {
        fprintf(stderr, "Memory allocation failed for write_stmt children.\n");
        free(node); exit(EXIT_FAILURE);
    }
    node->children[0] = expression;

    return node;
}

ASTNode* create_loop_statement_node(ASTNode* count, ASTNode* body, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_LOOP_STATEMENT, location);
    node->data.loop_stmt.count = count;
    node->data.loop_stmt.body = body;

    node->child_count = 2;
    node->children = (ASTNode**)malloc(2 * sizeof(ASTNode*));
    if (!node->children) {
        fprintf(stderr, "Memory allocation failed for loop_stmt children.\n");
        free(node); exit(EXIT_FAILURE);
    }
    node->children[0] = count;
    node->children[1] = body;

    return node;
}

ASTNode* create_code_block_node(ASTNode* statement_list, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_CODE_BLOCK, location);
    node->data.code_block.statements = statement_list->data.statement_list.statements;
    node->data.code_block.statement_count = statement_list->data.statement_list.statement_count;

    // The children pointer of the code block directly points to the statements array
    // owned by the statement_list node. The statement_list node itself will be freed,
    // but its internal statements array will be managed by the code_block.
    node->child_count = statement_list->data.statement_list.statement_count;
    node->children = statement_list->data.statement_list.statements;
    // Prevent double freeing: mark the statement_list's statements as NULL
    // since ownership is transferred.
    statement_list->data.statement_list.statements = NULL;
    statement_list->child_count = 0; // Also clear its children count

    return node;
}

ASTNode* create_statement_list_node(ASTNode** statements, int count, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_STATEMENT_LIST, location);
    node->data.statement_list.statements = statements;
    node->data.statement_list.statement_count = count;

    node->child_count = count;
    node->children = statements; // Children point directly to the array of statements

    return node;
}

ASTNode* create_program_node(ASTNode* statement_list, SourceLocation location) {
    ASTNode* node = create_ast_node(AST_PROGRAM, location);
    node->data.program.statement_list = statement_list;

    node->child_count = 1;
    node->children = (ASTNode**)malloc(sizeof(ASTNode*));
    if (!node->children) {
        fprintf(stderr, "Memory allocation failed for program children.\n");
        free(node); exit(EXIT_FAILURE);
    }
    node->children[0] = statement_list;

    return node;
}


// --- Semantic Action Implementations ---

// Default semantic action: simply passes through the first child's AST node
ASTNode* semantic_action_passthrough(ASTNode** children) {
    if (children && children[0]) return children[0];
    return NULL;
}

// Semantic action for the top-level Program production
ASTNode* semantic_action_program(ASTNode** children) {
    // Program -> StatementList
    SourceLocation loc = {0, 0, NULL};
    if (children && children[0]) loc = children[0]->location;
    return create_program_node(children[0], loc);
}

// Semantic action for a single statement in a statement list
ASTNode* semantic_action_statement_list_single(ASTNode** children) {
    // StatementList -> Statement
    ASTNode** stmts = (ASTNode**)malloc(sizeof(ASTNode*));
    if (!stmts) {
        fprintf(stderr, "Memory allocation failed for statement list.\n");
        exit(EXIT_FAILURE);
    }
    stmts[0] = children[0];
    SourceLocation loc = {0, 0, NULL};
    if (children && children[0]) loc = children[0]->location;
    return create_statement_list_node(stmts, 1, loc);
}

// Semantic action for multiple statements in a statement list
ASTNode* semantic_action_statement_list_multi(ASTNode** children) {
    // StatementList -> Statement SEMICOLON StatementList
    ASTNode* first_stmt = children[0];
    ASTNode* rest_stmts_list = children[2]; // This is the StatementList AST node

    int current_count = 0;
    ASTNode** current_stmts = NULL;

    if (rest_stmts_list && rest_stmts_list->type == AST_STATEMENT_LIST) {
        current_count = rest_stmts_list->data.statement_list.statement_count;
        current_stmts = rest_stmts_list->data.statement_list.statements;
    }

    // Allocate a new array large enough for all statements
    ASTNode** new_stmts = (ASTNode**)malloc((current_count + 1) * sizeof(ASTNode*));
    if (!new_stmts) {
        fprintf(stderr, "Memory allocation failed for new statement list.\n");
        exit(EXIT_FAILURE);
    }
    new_stmts[0] = first_stmt; // Add the first statement

    // Copy existing statements from the rest_stmts_list
    if (current_stmts) {
        memcpy(new_stmts + 1, current_stmts, current_count * sizeof(ASTNode*));
        // Free the old statements array from the rest_stmts_list node
        // as ownership is transferred to the new_stmts array.
        free(current_stmts);
        rest_stmts_list->data.statement_list.statements = NULL;
        rest_stmts_list->child_count = 0; // Clear its children as well
    }

    SourceLocation loc = {0, 0, NULL};
    if (children && children[0]) loc = children[0]->location;
    return create_statement_list_node(new_stmts, current_count + 1, loc);
}

// Semantic action for an identifier (already an AST node from lexer)
ASTNode* semantic_action_id(ASTNode** children) {
    return children[0];
}

// Semantic action for a number (already an AST node from lexer)
ASTNode* semantic_action_number(ASTNode** children) {
    return children[0];
}

// Semantic action for a string (already an AST node from lexer)
ASTNode* semantic_action_string(ASTNode** children) {
    return children[0];
}

// Semantic action for an expression in parentheses
ASTNode* semantic_action_paren_expr(ASTNode** children) {
    // F -> LPAREN E RPAREN
    return children[1]; // The expression inside parentheses
}

// Semantic action for assignment statement
ASTNode* semantic_action_assignment(ASTNode** children) {
    // Assignment -> IDENTIFIER ASSIGN E
    SourceLocation loc = {0, 0, NULL};
    if (children && children[0]) loc = children[0]->location;
    return create_assignment_node(children[0], children[2], loc);
}

// Semantic action for plus-assignment (e.g., x += 5)
ASTNode* semantic_action_plus_assign(ASTNode** children) {
    // Assignment -> IDENTIFIER PLUS_ASSIGN E
    SourceLocation node_loc = {0, 0, NULL};
    if (children && children[0]) node_loc = children[0]->location;

    // Create a binary operation node for `identifier + E`
    ASTNode* binary_op = create_binary_op_node(BINOP_ADD, children[0], children[2], node_loc);
    // Then create an assignment node for `identifier = (identifier + E)`
    return create_assignment_node(children[0], binary_op, node_loc);
}

// Semantic action for minus-assignment (e.g., x -= 5)
ASTNode* semantic_action_minus_assign(ASTNode** children) {
    // Assignment -> IDENTIFIER MINUS_ASSIGN E
    SourceLocation node_loc = {0, 0, NULL};
    if (children && children[0]) node_loc = children[0]->location;

    // Create a binary operation node for `identifier - E`
    ASTNode* binary_op = create_binary_op_node(BINOP_SUBTRACT, children[0], children[2], node_loc);
    // Then create an assignment node for `identifier = (identifier - E)`
    return create_assignment_node(children[0], binary_op, node_loc);
}

// Semantic action for write statement
ASTNode* semantic_action_write_statement(ASTNode** children) {
    // WriteStatement -> WRITE E
    SourceLocation loc = {0, 0, NULL};
    if (children && children[1]) loc = children[1]->location;
    return create_write_statement_node(children[1], loc);
}

// Semantic action for loop statement
ASTNode* semantic_action_loop_statement(ASTNode** children) {
    // LoopStatement -> REPEAT E TIMES CodeBlock
    SourceLocation loc = {0, 0, NULL};
    if (children && children[1]) loc = children[1]->location;
    else if (children && children[3]) loc = children[3]->location; // Fallback
    return create_loop_statement_node(children[1], children[3], loc);
}

// Semantic action for binary addition (E -> E + T)
ASTNode* semantic_action_binary_add(ASTNode** children) {
    // E -> E PLUS T
    SourceLocation loc = {0, 0, NULL};
    if (children && children[0]) loc = children[0]->location;
    return create_binary_op_node(BINOP_ADD, children[0], children[2], loc);
}

// Semantic action for binary multiplication (T -> T * F)
ASTNode* semantic_action_binary_multiply(ASTNode** children) {
    // T -> T STAR F
    SourceLocation loc = {0, 0, NULL};
    if (children && children[0]) loc = children[0]->location;
    return create_binary_op_node(BINOP_MULTIPLY, children[0], children[2], loc);
}

// --- Parser Core Logic ---

// Pushes a state, grammar symbol, and AST node onto the parser stack
static void parser_push(Parser* parser, int state, GrammarSymbol* symbol, ASTNode* ast_node) {
    if (parser->stack_top + 1 >= parser->stack_capacity) {
        parser->stack_capacity *= 2; // Double capacity
        StackEntry* new_stack = (StackEntry*)realloc(parser->stack,
                                           parser->stack_capacity * sizeof(StackEntry));
        if (!new_stack) {
            fprintf(stderr, "Memory re-allocation failed for parser stack.\n");
            exit(EXIT_FAILURE);
        }
        parser->stack = new_stack;
    }

    parser->stack_top++;
    parser->stack[parser->stack_top].state = state;
    parser->stack[parser->stack_top].symbol = symbol;
    parser->stack[parser->stack_top].ast_node = ast_node;
}

// Initializes the parser state
static void parser_init(Parser* parser, Token* input, int input_length) {
    parser->stack_capacity = 100; // Initial stack capacity
    parser->stack = (StackEntry*)malloc(parser->stack_capacity * sizeof(StackEntry));
    if (!parser->stack) {
        fprintf(stderr, "Memory allocation failed for parser stack.\n");
        exit(EXIT_FAILURE);
    }
    parser->stack_top = -1; // Stack is empty
    parser->input = input;
    parser->input_position = 0;
    parser->input_length = input_length;
    parser->has_error = false;
    parser->error_message = NULL;

    // Push initial state 0 onto the stack
    parser_push(parser, 0, NULL, NULL);
}

// Frees memory allocated by the parser
static void parser_free(Parser* parser) {
    if (parser->stack) {
        free(parser->stack);
        parser->stack = NULL;
    }
    if (parser->error_message) {
        free(parser->error_message);
        parser->error_message = NULL;
    }
}

// Pops 'count' elements from the parser stack
static void parser_pop(Parser* parser, int count) {
    if (count > parser->stack_top + 1) {
        fprintf(stderr, "Error: Trying to pop more items (%d) than available (%d) on stack\n",
                count, parser->stack_top + 1);
        parser->has_error = true; // Mark error
        return;
    }
    parser->stack_top -= count;
}

// Returns the state at the top of the stack
static int parser_current_state(Parser* parser) {
    if (parser->stack_top >= 0) {
        return parser->stack[parser->stack_top].state;
    }
    return 0; // Should not happen if stack is properly initialized with state 0
}

// Returns a pointer to the current input token
static Token* parser_current_token(Parser* parser) {
    if (parser->input_position < parser->input_length) {
        return &parser->input[parser->input_position];
    }
    // Return a static EOF token if input is exhausted
    static Token eof_token = {TOKEN_EOF, "$", {0, 0, "EOF"}, .value.int_value = 0};
    return &eof_token;
}

// Advances the input token stream to the next token
static void parser_advance_input(Parser* parser) {
    if (parser->input_position < parser->input_length) {
        parser->input_position++;
    }
}

// Creates an AST node directly from a terminal token
static ASTNode* parser_get_node_from_token(Token* token) {
    switch (token->type) {
        case TOKEN_IDENTIFIER:
            return create_identifier_node(token->lexeme, token->location);
        case TOKEN_INTEGER:
            return create_number_node(token->value.int_value, token->location);
        case TOKEN_STRING:
            // Skip the quotes when creating the string node
            if (strlen(token->lexeme) >= 2) {
                char* inner_string = strdup(token->lexeme + 1);
                inner_string[strlen(inner_string) - 1] = '\0'; // Remove trailing quote
                ASTNode* node = create_string_node(inner_string, token->location);
                free(inner_string); // Free the temporary duplicated string
                return node;
            }
            return create_string_node("", token->location); // Empty string for malformed
        default:
            // For other terminals (operators, punctuation), we typically don't create AST nodes
            // unless they directly represent a leaf in the AST (e.g., for unary ops).
            // For this parser, only identifiers, numbers, and strings are direct leaves.
            return NULL;
    }
}


// Main LR(1) parsing algorithm
ASTNode* parse(Grammar* grammar, Token* input, int num_tokens) {
    Parser parser;
    parser_init(&parser, input, num_tokens);

    printf("\n--- Starting LR(1) Parsing ---\n");

    while (!parser.has_error) {
        int current_state = parser_current_state(&parser);
        Token* current_token = parser_current_token(&parser);

        // Corrected format specifier: %d for current_token->type
        printf("Stack: %d, Current Token: '%s' (type %s, ID %d)\n",
               current_state, current_token->lexeme, token_type_str(current_token->type), current_token->type);

        // Look up action in ACTION table
        // Ensure that current_token->type is a valid index for the action_table.
        if (current_token->type < 0 || current_token->type >= MAX_TERMINALS) {
            parser.has_error = true;
            parser.error_message = strdup("Invalid terminal symbol type encountered during parsing. Token ID out of bounds.");
            break;
        }

        ActionEntry action = action_table[current_state][current_token->type];

        switch (action.type) {
            case ACTION_SHIFT: {
                printf("  Action: SHIFT %d\n", action.target_state_or_production_id);

                // Create AST node from terminal if applicable (e.g., Identifier, Number, String)
                ASTNode* token_node = parser_get_node_from_token(current_token);

                // Find the grammar symbol for this terminal (needed for stack entry)
                GrammarSymbol* terminal_symbol = NULL;
                // Iterate through grammar's terminals to find the matching symbol
                for (int i = 0; i < grammar->terminal_count; i++) {
                    if (grammar->terminals[i]->id == current_token->type) {
                        terminal_symbol = grammar->terminals[i];
                        break;
                    }
                }
                if (!terminal_symbol) {
                    parser.has_error = true;
                    parser.error_message = strdup("Internal error: Terminal symbol not found in grammar.");
                    break;
                }

                parser_push(&parser, action.target_state_or_production_id,
                           terminal_symbol, token_node);
                parser_advance_input(&parser);
                break;
            }

            case ACTION_REDUCE: {
                int production_id = action.target_state_or_production_id;
                Production* production = &grammar->productions[production_id];

                printf("  Action: REDUCE by production %d (%s ->",
                       production_id, production->left_symbol->name);
                for (int i = 0; i < production->right_count; i++) {
                    printf(" %s", production->right_symbols[i]->name);
                }
                printf(")\n");

                // Collect AST nodes from stack for semantic action
                ASTNode** child_nodes = NULL;
                if (production->right_count > 0) {
                    child_nodes = (ASTNode**)malloc(production->right_count * sizeof(ASTNode*));
                    if (!child_nodes) {
                        parser.has_error = true;
                        parser.error_message = strdup("Memory allocation failed for child_nodes during reduction");
                        break;
                    }
                    for (int i = 0; i < production->right_count; i++) {
                        // Elements are popped from stack_top down to stack_top - right_count + 1
                        // So, child_nodes[0] corresponds to the first symbol on the RHS (bottom of the popped segment)
                        int stack_index = parser.stack_top - production->right_count + 1 + i;
                        if (stack_index >= 0 && stack_index <= parser.stack_top) {
                            child_nodes[i] = parser.stack[stack_index].ast_node;
                        } else {
                            // This indicates a stack underflow or logic error
                            fprintf(stderr, "Error: Stack underflow during reduction for production %d\n", production_id);
                            child_nodes[i] = NULL;
                        }
                    }
                }

                // Pop symbols and states from stack
                parser_pop(&parser, production->right_count);

                // Apply semantic action to build new AST node
                ASTNode* new_node = NULL;
                if (production->semantic_action) {
                    new_node = production->semantic_action(child_nodes);
                } else {
                    // Default behavior if no semantic action specified:
                    // If RHS has one symbol, pass its AST node through.
                    if (production->right_count == 1 && child_nodes && child_nodes[0]) {
                        new_node = child_nodes[0];
                    }
                }

                // Look up GOTO state
                int current_state_after_pop = parser_current_state(&parser);
                // Ensure production->left_symbol->id is a valid index for the goto_table (non-terminal ID)
                if (production->left_symbol->id < 0 || production->left_symbol->id >= MAX_NON_TERMINALS) {
                    parser.has_error = true;
                    parser.error_message = strdup("Invalid non-terminal symbol ID for GOTO table lookup");
                    if (child_nodes) free(child_nodes);
                    break;
                }
                int goto_state = goto_table[current_state_after_pop][production->left_symbol->id];

                if (goto_state == -1) {
                    parser.has_error = true;
                    char err_msg[256];
                    snprintf(err_msg, sizeof(err_msg), "GOTO table entry not found for state %d and non-terminal %s",
                             current_state_after_pop, production->left_symbol->name);
                    parser.error_message = strdup(err_msg);
                    if (child_nodes) free(child_nodes);
                    break;
                }

                printf("  GOTO state %d\n", goto_state);

                // Push non-terminal and new state onto the stack
                parser_push(&parser, goto_state, production->left_symbol, new_node);

                if (child_nodes) free(child_nodes); // Free the temporary child_nodes array
                break;
            }

            case ACTION_ACCEPT: {
                printf("  Action: ACCEPT\n");

                // The root of the AST should be the single AST node remaining on the stack
                ASTNode* result = NULL;
                if (parser.stack_top >= 0 && parser.stack[parser.stack_top].ast_node) {
                    result = parser.stack[parser.stack_top].ast_node;
                } else {
                    fprintf(stderr, "Warning: Parser accepted but no root AST node found on stack.\n");
                }

                parser_free(&parser);
                return result;
            }

            case ACTION_ERROR:
            default: {
                printf("  Action: ERROR\n");
                parser.has_error = true;

                // Create detailed error message
                char* error_msg = (char*)malloc(512);
                if (error_msg) {
                    // Corrected format specifier: %d for current_token->type. Added token ID.
                    snprintf(error_msg, 512,
                            "Syntax error at %s:%d:%d: unexpected token '%s' (type %s, ID %d).",
                            current_token->location.filename,
                            current_token->location.line,
                            current_token->location.column,
                            current_token->lexeme,
                            token_type_str(current_token->type),
                            current_token->type);
                } else {
                    error_msg = strdup("Syntax error: memory allocation failed for error message.");
                }
                parser.error_message = error_msg;
                break;
            }
        }
    }

    // Parsing failed
    if (parser.error_message) {
        fprintf(stderr, "Parse error: %s\n", parser.error_message);
    }

    parser_free(&parser);
    return NULL;
}


// --- AST Cleanup and Printing ---

// Recursively frees memory allocated for an AST node and its children
void free_ast_node(ASTNode* node) {
    if (!node) return;

    // Free specific data based on node type
    switch (node->type) {
        case AST_IDENTIFIER:
            if (node->data.identifier.name) {
                free(node->data.identifier.name);
            }
            break;
        case AST_STRING:
            if (node->data.string.value) {
                free(node->data.string.value);
            }
            break;
        case AST_STATEMENT_LIST:
            // Only free the array itself, not the individual statements,
            // as they are freed by recursive calls on children.
            if (node->data.statement_list.statements) {
                // IMPORTANT: If this statement list's statements array
                // was transferred to a CODE_BLOCK node, it should be NULL here.
                // Check if node->children points to the same memory to avoid double-free.
                if (node->children == node->data.statement_list.statements) {
                    // This means children array is the same as statements array.
                    // The recursive free will handle individual ASTNodes.
                    // We only free the 'statements' array itself here.
                    free(node->data.statement_list.statements);
                }
            }
            break;
        case AST_CODE_BLOCK:
            // Similar to STATEMENT_LIST, free the array if it's distinct from children
            if (node->data.code_block.statements) {
                if (node->children == node->data.code_block.statements) {
                    free(node->data.code_block.statements);
                }
            }
            break;
        default:
            break;
    }

    // Recursively free children nodes
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            free_ast_node(node->children[i]);
        }
        // Free the array of child pointers itself
        free(node->children);
    }

    // Finally, free the node itself
    free(node);
}

// Prints the AST structure for debugging purposes
void print_ast_node(ASTNode* node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) printf("  ");

    printf("[%s:%d:%d] ", node->location.filename ? node->location.filename : "null",
                           node->location.line, node->location.column);

    switch (node->type) {
        case AST_PROGRAM:
            printf("Program\n");
            break;
        case AST_STATEMENT_LIST:
            printf("StatementList (%d statements)\n", node->data.statement_list.statement_count);
            break;
        case AST_ASSIGNMENT:
            printf("Assignment\n");
            break;
        case AST_BINARY_OP:
            printf("BinaryOp (");
            switch (node->data.binary_op.operator) {
                case BINOP_ADD: printf("ADD"); break;
                case BINOP_SUBTRACT: printf("SUBTRACT"); break;
                case BINOP_MULTIPLY: printf("MULTIPLY"); break;
                case BINOP_ASSIGN: printf("ASSIGN"); break;
                case BINOP_PLUS_ASSIGN: printf("PLUS_ASSIGN"); break;
                case BINOP_MINUS_ASSIGN: printf("MINUS_ASSIGN"); break;
                default: printf("UNKNOWN_BINOP"); break;
            }
            printf(")\n");
            break;
        case AST_UNARY_OP:
            printf("UnaryOp (");
            switch (node->data.unary_op.operator) {
                case UNOP_INCREMENT: printf("INCREMENT"); break;
                case UNOP_DECREMENT: printf("DECREMENT"); break;
                default: printf("UNKNOWN_UNOP"); break;
            }
            printf(")\n");
            break;
        case AST_IDENTIFIER:
            printf("Identifier: %s\n", node->data.identifier.name);
            break;
        case AST_NUMBER:
            printf("Number: %lld\n", node->data.number.value); // Use %lld for long long
            break;
        case AST_STRING:
            printf("String: \"%s\"\n", node->data.string.value);
            break;
        case AST_WRITE_STATEMENT:
            printf("WriteStatement\n");
            break;
        case AST_LOOP_STATEMENT:
            printf("LoopStatement\n");
            break;
        case AST_CODE_BLOCK:
            printf("CodeBlock (%d statements)\n", node->data.code_block.statement_count);
            break;
        default:
            printf("Unknown AST Node Type (%d)\n", node->type);
            break;
    }

    // Print children recursively
    for (int i = 0; i < node->child_count; i++) {
        print_ast_node(node->children[i], depth + 1);
    }
}
