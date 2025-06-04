#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "parser.h"

enum SymbolIDs {
    // Non-terminals
    NT_PROGRAM = 0, // Your original start symbol
    NT_STATEMENT_LIST,
    NT_STATEMENT,
    NT_DECLARATION,
    NT_ASSIGNMENT,
    NT_INCREMENT,
    NT_DECREMENT,
    NT_WRITE_STATEMENT,
    NT_LOOP_STATEMENT,
    NT_CODE_BLOCK,
    NT_OUTPUT_LIST,
    NT_LIST_ELEMENT,
    NT_E, // For the test grammar (E -> E+T)
    NT_T,
    NT_F,

    // Terminals (start after the last non-terminal ID, e.g., 50 or 100)
    T_SEMICOLON = 50,
    T_NUMBER,
    T_WRITE,
    T_REPEAT,
    T_AND,
    T_TIMES,
    T_NEWLINE,
    T_IDENTIFIER,
    T_STRING,
    T_INTEGER,
    T_PLUS,    // For test grammar
    T_STAR,    // For test grammar
    T_LPAREN,  // For test grammar
    T_RPAREN,  // For test grammar
    T_ID,      // For test grammar (distinguish from IDENTIFIER if needed)
    T_EOF = 90 // End-of-file marker, as used in your code
};

typedef enum {
    SYMBOL_TERMINAL,
    SYMBOL_NONTERMINAL
} SymbolType;

typedef struct {
    SymbolType type;
    int id;               // Unique identifier
    char* name;           // Symbol name
} GrammarSymbol;

typedef struct Production {
    GrammarSymbol* left_symbol;           // Non-terminal on left side
    GrammarSymbol** right_symbols;        // Array of symbols on right side
    int right_count;           // Number of symbols on right side
    int production_id;         // Unique identifier
    ASTNode* (*semantic_action)(ASTNode** children); // Function to build AST
} Production;

typedef struct Grammar {
    Production *productions;    // Array of productions
    int production_count;      // Number of productions
    GrammarSymbol** terminals;           // Array of terminal symbols
    int terminal_count;       // Number of terminals
    GrammarSymbol** non_terminals;       // Array of non-terminal symbols
    int non_terminal_count;   // Number of non-terminals
    GrammarSymbol* start_symbol;         // Starting non-terminal
} Grammar;

typedef struct LRItem {
    int production_id;        // Which production this item represents
    int dot_position;         // Position of the dot in the production
    GrammarSymbol** lookahead_set;       // Set of lookahead symbols
    int lookahead_count;      // Number of lookahead symbols
} LRItem;

typedef struct ItemSet {
    LRItem* items;           // Array of LR items
    int item_count;          // Number of items in set
    int state_id;            // Unique identifier for this state
    int capacity;
} ItemSet;

typedef enum ActionType {
    ACTION_SHIFT,
    ACTION_REDUCE,
    ACTION_ACCEPT,
    ACTION_ERROR
} ActionType;

typedef struct Action {
    ActionType type;
    int value;               // State number for shift, production number for reduce
} Action;

typedef struct ParsingTable {
    Action** action_table;   // action_table[state][terminal] = Action
    int** goto_table;        // goto_table[state][non_terminal] = next_state
    int state_count;         // Number of states
    int terminal_count;      // Number of terminals
    int non_terminal_count;  // Number of non-terminals
} ParsingTable;

typedef struct StackEntry {
    int state;               // Current state
    int symbol;              // Symbol associated with this entry
    ASTNode *ast_node;       // AST node for this symbol
} StackEntry;

typedef struct ParserStack {
    StackEntry* entries;     // Array of stack entries
    int top;                 // Index of top element
    int capacity;            // Maximum capacity
} ParserStack;

// THIS IS AN LALR(1) PARSER IMPLEMENTATION FOR THE GRAMMAR GIVEN BELOW
// THE LEXER IS ALREADY IMPLEMENTED AND THE TOKEN STRING WILL BE PASSED TO THE PARSER
// Non-terminals: program, statement_list, statement, declaration, assignment, increment,
// decrement, write_statement, loop_statement, code_block,
// output_list, list_element
// Terminals: ;, number, write, repeat, and, times, newline
// IDENTIFIER, STRING, INTEGER
// Production Rules:
// R0: <program> -> <statement_list> (program is S prime)
// R1: <statement_list> -> <statement_list> <statement>
// R2: <statement_list> -> <statement>
// R3: <statement> -> <assignment> ;
// R4: <statement> -> <declaration> ;
// R5: <statement> -> <decrement> ;
// R6: <statement> -> <increment> ;
// R7: <statement> -> <write_statement> ;
// R8: <statement> -> <loop_statement>
// R9: <declaration> -> number IDENTIFIER
// R10: <assignment> -> IDENTIFIER := INTEGER
// R11: <decrement> -> IDENTIFIER -= INTEGER
// R12: <increment> -> IDENTIFIER += INTEGER
// R13: <write_statement> -> write <output_list>
// R14: <loop_statement> -> repeat INTEGER times <statement>
// R15: <loop_statement> -> repeat INTEGER times <code_block>
// R16: <code_block> -> { <statement_list> }
// R17: <output_list> -> <output_list> and <list_element>
// R18: <output_list> -> <list_element>
// R19: <list_element> -> INTEGER
// R20: <list_element> -> STRING
// R21: <list_element> -> newline


/*
FIRST SET OPERATIONS AFTER THIS
===================================================== */
#define MAX_TERMINALS 100
#define MAX_NON_TERMINALS 100
// Define the bitmask set type
typedef unsigned char TerminalSet[MAX_TERMINALS / 8 + (MAX_TERMINALS % 8 != 0)];
TerminalSet firstSetsForNonTerminals[MAX_NON_TERMINALS];
// Functions for set operations (e.g., set_add, set_union, set_contains, set_clear)
void set_add(TerminalSet s, int terminal_id) {
    s[terminal_id / 8] |= (1 << (terminal_id % 8));
}
int set_contains(const TerminalSet s, int terminal_id) {
    return (s[terminal_id / 8] & (1 << (terminal_id % 8))) != 0;
}
// Clears all bits in a TerminalSet
void set_clear(TerminalSet s) {
    memset(s, 0, sizeof(TerminalSet));
}

// Computes the union of two TerminalSets, storing the result in s1.
// Returns true if s1 was changed, false otherwise.
bool set_union(TerminalSet s1, TerminalSet s2) {
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


// Helper function to check if a non-terminal can derive epsilon
bool is_nullable(Grammar* grammar, GrammarSymbol* symbol) {
    if (symbol->type == SYMBOL_TERMINAL) {
        return false;
    }

    // Check if any production for this non-terminal has an empty right side (epsilon production)
    for (int i = 0; i < grammar->production_count; i++) {
        Production* p = &grammar->productions[i];
        if (p->left_symbol->id == symbol->id && p->right_count == 0) {
            return true;
        }
    }
    return false;
}

// Helper function to compute nullable set for all non-terminals
void compute_nullable_set(Grammar* grammar, bool* nullable) {
    // Initialize all non-terminals as not nullable
    for (int i = 0; i < grammar->non_terminal_count; i++) {
        nullable[grammar->non_terminals[i]->id] = false;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];

            // If production is A -> epsilon (empty right side)
            if (p->right_count == 0) {
                if (!nullable[p->left_symbol->id]) {
                    nullable[p->left_symbol->id] = true;
                    changed = true;
                }
                continue;
            }

            // Check if all symbols on right side are nullable
            bool all_nullable = true;
            for (int j = 0; j < p->right_count; j++) {
                if (p->right_symbols[j]->type == SYMBOL_TERMINAL) {
                    all_nullable = false;
                    break;
                } else if (!nullable[p->right_symbols[j]->id]) {
                    all_nullable = false;
                    break;
                }
            }

            if (all_nullable && !nullable[p->left_symbol->id]) {
                nullable[p->left_symbol->id] = true;
                changed = true;
            }
        }
    }
}

void compute_first_sets(Grammar* grammar) {
    // Initialize all FIRST sets for non-terminals to empty
    for (int i = 0; i < grammar->non_terminal_count; i++) {
        set_clear(firstSetsForNonTerminals[grammar->non_terminals[i]->id]);
    }

    // Compute nullable set first
    bool nullable[MAX_NON_TERMINALS] = {false};
    compute_nullable_set(grammar, nullable);

    bool changed = true;
    while (changed) {
        changed = false;

        // For each production A -> X1 X2 ... Xk
        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            GrammarSymbol* A = p->left_symbol; // A is a non-terminal

            // Handle epsilon production (A -> epsilon)
            if (p->right_count == 0) {
                // Epsilon is typically represented by a special terminal
                // For now, we'll skip adding epsilon to FIRST set as it's handled by nullable
                continue;
            }

            // Process symbols X1, X2, ..., Xk from left to right
            for (int j = 0; j < p->right_count; j++) {
                GrammarSymbol* current_symbol = p->right_symbols[j];

                if (current_symbol->type == SYMBOL_TERMINAL) {
                    // Add terminal to FIRST(A)
                    if (!set_contains(firstSetsForNonTerminals[A->id], current_symbol->id)) {
                        set_add(firstSetsForNonTerminals[A->id], current_symbol->id);
                        changed = true;
                    }
                    break; // Stop here since terminal cannot derive epsilon
                } else { // SYMBOL_NONTERMINAL
                    // Add FIRST(current_symbol) to FIRST(A)
                    if (set_union(firstSetsForNonTerminals[A->id],
                                  firstSetsForNonTerminals[current_symbol->id])) {
                        changed = true;
                    }

                    // If current symbol is not nullable, stop processing
                    if (!nullable[current_symbol->id]) {
                        break;
                    }
                    // If current symbol is nullable, continue to next symbol
                }
            }
        }
    }
}
/*
END OF FIRST SET OPERATIONS
===================================================== */
/*

/*
FOLLOW SET OPERATIONS AFTER THIS
===================================================== */
TerminalSet followSetsForNonTerminals[MAX_NON_TERMINALS];

void compute_follow_sets(Grammar* grammar) {
    // Initialize FOLLOW sets as empty for all non-terminals
    for (int i = 0; i < grammar->non_terminal_count; i++) {
        set_clear(followSetsForNonTerminals[grammar->non_terminals[i]->id]);
    }

    // Compute nullable set and first sets first, as they are prerequisites
    bool nullable[MAX_NON_TERMINALS] = {false};
    compute_nullable_set(grammar, nullable);
    compute_first_sets(grammar); // Ensure FIRST sets are computed and up-to-date

    // Add $ (end marker) to FOLLOW(start_symbol).
    // Assuming start_symbol->id is 0 and the end marker's ID is 90 as per your comment.
    // Make sure these IDs are consistently defined and used.
    set_add(followSetsForNonTerminals[grammar->start_symbol->id], 90); // Assuming 90 is the EOF terminal ID

    bool changed = true;
    while (changed) {
        changed = false;

        // For each production A -> alpha beta
        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            GrammarSymbol* A = p->left_symbol; // Left-hand side non-terminal

            // Iterate through the right-hand side symbols to find non-terminals B
            for (int j = 0; j < p->right_count; j++) {
                GrammarSymbol* B = p->right_symbols[j];

                if (B->type == SYMBOL_NONTERMINAL) {
                    // Case 1: Production A -> alpha B beta
                    // Add FIRST(beta) to FOLLOW(B)
                    // Iterate through symbols after B (beta)
                    bool beta_is_nullable = true; // Tracks if the rest of beta is nullable
                    for (int k = j + 1; k < p->right_count; k++) {
                        GrammarSymbol* Y = p->right_symbols[k];

                        if (Y->type == SYMBOL_TERMINAL) {
                            if (!set_contains(followSetsForNonTerminals[B->id], Y->id)) {
                                set_add(followSetsForNonTerminals[B->id], Y->id);
                                changed = true;
                            }
                            beta_is_nullable = false; // A terminal in beta means beta is not nullable from this point
                            break; // Stop looking further in beta, as terminal absorbs FIRST
                        } else { // Y is a non-terminal
                            if (set_union(followSetsForNonTerminals[B->id],
                                          firstSetsForNonTerminals[Y->id])) {
                                changed = true;
                            }
                            if (!nullable[Y->id]) {
                                beta_is_nullable = false; // A non-nullable non-terminal means beta is not nullable from this point
                                break;
                            }
                        }
                    }

                    // Case 2: If beta is nullable (or if B is the last symbol on RHS)
                    // Add FOLLOW(A) to FOLLOW(B)
                    if (beta_is_nullable) {
                        if (set_union(followSetsForNonTerminals[B->id],
                                      followSetsForNonTerminals[A->id])) {
                            changed = true;
                        }
                    }
                }
            }
        }
    }
}
/*
END OF FIRST SET OPERATIONS
===================================================== */


/*
CONSTRUCTING LR(0) ITEM SETS OPERATIONS AFTER THIS
===================================================== */

// Dummy semantic action for now
ASTNode* default_semantic_action(ASTNode** children) {
    return NULL; // Or return a new AST node for testing purposes
}

// --- Helper Functions for LRItem and ItemSet Management ---

// Function to compare two LRItems (for hashing and equality)
// Returns true if items are identical, false otherwise
bool lr_item_equal(const LRItem* item1, const LRItem* item2) {
    if (item1->production_id != item2->production_id ||
        item1->dot_position != item2->dot_position) {
        return false;
    }
    // For LR(0), lookahead doesn't matter for equality.
    // For LR(1) or LALR(1), you'd compare lookahead sets here too.
    return true;
}

// A simple hash function for an LRItem
// (Could be more sophisticated for larger grammars, but this is a start)
int lr_item_hash(const LRItem* item) {
    return item->production_id * 31 + item->dot_position; // Simple prime multiplication
}

// Function to check if an LRItem is already in an ItemSet
bool item_set_contains_item(const ItemSet* set, const LRItem* item) {
    for (int i = 0; i < set->item_count; ++i) {
        if (lr_item_equal(&set->items[i], item)) {
            return true;
        }
    }
    return false;
}

// Function to add an LRItem to an ItemSet (handles reallocation)
void item_set_add_item(ItemSet* set, LRItem item) {
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

// Function to initialize an ItemSet
void item_set_init(ItemSet* set) {
    set->items = NULL;
    set->item_count = 0;
    set->capacity = 0;
    set->state_id = -1; // Not yet assigned
}

// Function to free memory associated with an ItemSet
void item_set_free(ItemSet* set) {
    free(set->items);
    set->items = NULL;
    set->item_count = 0;
    set->capacity = 0;
}

// --- Hashing for ItemSets (Crucial for detecting duplicates) ---
// This is a basic approach. For robust hashing, especially with many items,
// you might sort the items within the set first to ensure consistent hash.
// For LR(0), lookahead sets are empty, so we don't need to consider them for hashing.

// A simple hash for an ItemSet.
// For robust hashing, items should be sorted within the set before hashing.
unsigned long hash_item_set(const ItemSet* set) {
    unsigned long hash = 5381; // djb2 hash constant
    for (int i = 0; i < set->item_count; ++i) {
        hash = ((hash << 5) + hash) + lr_item_hash(&set->items[i]);
    }
    return hash;
}

// Function to compare two ItemSets for equality
bool item_sets_equal(const ItemSet* set1, const ItemSet* set2) {
    if (set1->item_count != set2->item_count) {
        return false;
    }
    // Deep comparison: check if all items in set1 are in set2, and vice versa.
    // A more efficient way would be to sort both sets and then compare item by item.
    // For now, a simple nested loop:
    for (int i = 0; i < set1->item_count; ++i) {
        if (!item_set_contains_item(set2, &set1->items[i])) {
            return false;
        }
    }
    return true;
}


// --- LR(0) Closure Algorithm ---
// Expands an ItemSet by adding all implied items
void closure(ItemSet *items_set, Grammar *grammar, bool* nullable) { // Add nullable as a dependency
    bool changed = true;
    // Use a temporary list to add new items and then merge
    LRItem* new_items = NULL;
    int new_item_count = 0;
    int new_item_capacity = 0;

    while (changed) {
        changed = false;
        new_item_count = 0; // Reset for each iteration

        for (int i = 0; i < items_set->item_count; ++i) {
            LRItem current_item = items_set->items[i];
            Production* p = &grammar->productions[current_item.production_id];

            // Check if dot is not at the end and is followed by a non-terminal
            if (current_item.dot_position < p->right_count) {
                GrammarSymbol* symbol_after_dot = p->right_symbols[current_item.dot_position];

                if (symbol_after_dot->type == SYMBOL_NONTERMINAL) {
                    // For each production B -> gamma
                    for (int prod_idx = 0; prod_idx < grammar->production_count; ++prod_idx) {
                        Production* B_prod = &grammar->productions[prod_idx];

                        if (B_prod->left_symbol->id == symbol_after_dot->id) {
                            LRItem new_item = {
                                .production_id = B_prod->production_id,
                                .dot_position = 0, // Dot at the beginning of the new production
                                .lookahead_set = NULL, // For LR(0), lookahead is not explicitly carried
                                .lookahead_count = 0
                            };

                            // Check if this new item is already in the set
                            if (!item_set_contains_item(items_set, &new_item)) {
                                // Add to a temporary list first to avoid modifying set while iterating
                                if (new_item_count >= new_item_capacity) {
                                    new_item_capacity = (new_item_capacity == 0) ? 4 : new_item_capacity * 2;
                                    new_items = (LRItem*)realloc(new_items, new_item_capacity * sizeof(LRItem));
                                    if (new_items == NULL) {
                                        fprintf(stderr, "Memory allocation failed for new_items in closure.\n");
                                        exit(EXIT_FAILURE);
                                    }
                                }
                                new_items[new_item_count++] = new_item;
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
        // Add all newly found items to the main items_set
        for (int i = 0; i < new_item_count; ++i) {
            item_set_add_item(items_set, new_items[i]);
        }
    }
    free(new_items); // Clean up temp buffer
}


// --- LR(0) Goto Algorithm ---
// Computes the new ItemSet by advancing the dot past a symbol X
ItemSet* goto_set(const ItemSet *I, GrammarSymbol *X, Grammar *grammar, bool* nullable) {
    ItemSet* J = (ItemSet*)malloc(sizeof(ItemSet));
    if (J == NULL) {
        fprintf(stderr, "Memory allocation failed for new ItemSet in goto_set.\n");
        exit(EXIT_FAILURE);
    }
    item_set_init(J);

    for (int i = 0; i < I->item_count; ++i) {
        LRItem current_item = I->items[i];
        Production* p = &grammar->productions[current_item.production_id];

        // Check if dot is not at the end and is followed by symbol X
        if (current_item.dot_position < p->right_count &&
            p->right_symbols[current_item.dot_position]->id == X->id) {

            LRItem new_item = {
                .production_id = current_item.production_id,
                .dot_position = current_item.dot_position + 1, // Advance the dot
                .lookahead_set = NULL,
                .lookahead_count = 0
            };
            item_set_add_item(J, new_item);
        }
    }

    // Apply closure to the new set of items
    closure(J, grammar, nullable); // Pass nullable to closure
    return J;
}

// --- Main LR(0) Item Set Construction ---

// (Need a hash table implementation for mapping ItemSets to State IDs)
// For simplicity, let's use a dynamic array as a "list of ItemSets"
// and iterate to find duplicates. For larger grammars, a real hash table is needed.

typedef struct ItemSetList {
    ItemSet* sets;
    int count;
    int capacity;
} ItemSetList;

void item_set_list_init(ItemSetList* list) {
    list->sets = NULL;
    list->count = 0;
    list->capacity = 0;
}

void item_set_list_add(ItemSetList* list, ItemSet new_set) {
    if (list->count >= list->capacity) {
        list->capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        list->sets = (ItemSet*)realloc(list->sets, list->capacity * sizeof(ItemSet));
        if (list->sets == NULL) {
            fprintf(stderr, "Memory allocation failed for ItemSetList.\n");
            exit(EXIT_FAILURE);
        }
    }
    list->sets[list->count++] = new_set;
}

// Function to find an ItemSet in the list, returns its index or -1 if not found
int find_item_set_in_list(const ItemSetList* list, const ItemSet* target_set) {
    for (int i = 0; i < list->count; ++i) {
        if (item_sets_equal(&list->sets[i], target_set)) {
            return i;
        }
    }
    return -1;
}

// Function to print an ItemSet (for debugging)
void print_item_set(const ItemSet* set, Grammar* grammar) {
    printf("I%d:\n", set->state_id);
    for (int i = 0; i < set->item_count; ++i) {
        LRItem item = set->items[i];
        Production* p = &grammar->productions[item.production_id];
        printf("  %s -> ", p->left_symbol->name);
        for (int j = 0; j < p->right_count; ++j) {
            if (j == item.dot_position) {
                printf(" .");
            }
            printf(" %s", p->right_symbols[j]->name);
        }
        if (item.dot_position == p->right_count) { // Dot at the end
            printf(" .");
        }
        printf("\n");
    }
}


// Main function to create all LR(0) item sets
void create_lr0_sets(Grammar* grammar) {
    ItemSetList canonical_collection;
    item_set_list_init(&canonical_collection);

    // Need nullable set for closure function
    bool nullable[MAX_NON_TERMINALS];
    compute_nullable_set(grammar, nullable);

    // 1. Create the initial item set I0 (augmented grammar start production)
    ItemSet I0;
    item_set_init(&I0);
    I0.state_id = 0; // First state is 0

    // Assuming R0: <program> -> <statement_list> is the augmented production
    // And grammar->start_symbol is <program>
    // So the item is <program> -> . <statement_list>
    // However, the standard augmented grammar is S' -> . S.
    // Your R0 is <program> -> <statement_list>. If <program> is S', this is fine.
    // Let's assume production 0 is the augmented start rule.
    LRItem initial_item = {
        .production_id = 0, // Assuming production 0 is S' -> .S
        .dot_position = 0,
        .lookahead_set = NULL,
        .lookahead_count = 0
    };
    item_set_add_item(&I0, initial_item);
    closure(&I0, grammar, nullable);

    item_set_list_add(&canonical_collection, I0);

    // Queue for processing item sets
    ItemSet* q_sets = (ItemSet*)malloc(sizeof(ItemSet) * 4); // Initial queue capacity
    int q_front = 0;
    int q_rear = 0;
    int q_capacity = 4;

    q_sets[q_rear++] = I0; // Add I0 to the queue

    while (q_front < q_rear) {
        ItemSet current_I = q_sets[q_front++];

        // Iterate over all possible grammar symbols (terminals and non-terminals)
        // You'll need a way to get all unique symbols (terminals + non-terminals)
        // For simplicity, let's just iterate through IDs and check if they exist
        // A better approach would be to iterate through grammar->terminals and grammar->non_terminals
        GrammarSymbol** all_symbols = (GrammarSymbol**)malloc(
            sizeof(GrammarSymbol*) * (grammar->terminal_count + grammar->non_terminal_count)
        );
        int all_symbols_count = 0;
        for (int i = 0; i < grammar->terminal_count; ++i) {
            all_symbols[all_symbols_count++] = grammar->terminals[i];
        }
        for (int i = 0; i < grammar->non_terminal_count; ++i) {
            all_symbols[all_symbols_count++] = grammar->non_terminals[i];
        }

        for (int i = 0; i < all_symbols_count; ++i) {
            GrammarSymbol* X = all_symbols[i];
            ItemSet* J = goto_set(&current_I, X, grammar, nullable);

            if (J->item_count > 0) { // If goto set is not empty
                int existing_idx = find_item_set_in_list(&canonical_collection, J);

                if (existing_idx == -1) { // New item set
                    J->state_id = canonical_collection.count; // Assign new state ID
                    item_set_list_add(&canonical_collection, *J); // Add to canonical collection

                    // Add to queue for further processing
                    if (q_rear >= q_capacity) {
                        q_capacity *= 2;
                        q_sets = (ItemSet*)realloc(q_sets, sizeof(ItemSet) * q_capacity);
                        if (q_sets == NULL) {
                            fprintf(stderr, "Memory allocation failed for queue.\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    q_sets[q_rear++] = *J;
                } else {
                    // Item set already exists, free the newly created J and use existing ID
                    item_set_free(J); // Free items array inside J
                    free(J);          // Free the ItemSet struct itself
                }
            } else {
                // J is empty, free it
                item_set_free(J);
                free(J);
            }
        }
        free(all_symbols); // Free the temporary array of symbols
    }

    // Print all generated item sets (for verification)
    printf("\n--- Generated LR(0) Item Sets ---\n");
    for (int i = 0; i < canonical_collection.count; ++i) {
        print_item_set(&canonical_collection.sets[i], grammar);
        printf("\n");
    }

    // --- Cleanup ---
    // Free all ItemSets in the canonical collection
    for (int i = 0; i < canonical_collection.count; ++i) {
        item_set_free(&canonical_collection.sets[i]);
    }
    free(canonical_collection.sets);
    free(q_sets);
}


/*
END OF CONSTRUCTING LR(0) ITEM SETS OPERATIONS
===================================================== */


int main() {
    // 1. Define Grammar Symbols (Terminals and Non-Terminals)
    GrammarSymbol symbols[MAX_NON_TERMINALS + MAX_TERMINALS];

    // Initialize Non-Terminals
    symbols[NT_PROGRAM] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_PROGRAM, "program"};
    symbols[NT_STATEMENT_LIST] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_STATEMENT_LIST, "statement_list"};
    symbols[NT_STATEMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_STATEMENT, "statement"};
    symbols[NT_DECLARATION] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_DECLARATION, "declaration"};
    symbols[NT_ASSIGNMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_ASSIGNMENT, "assignment"};
    symbols[NT_INCREMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_INCREMENT, "increment"};
    symbols[NT_DECREMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_DECREMENT, "decrement"};
    symbols[NT_WRITE_STATEMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_WRITE_STATEMENT, "write_statement"};
    symbols[NT_LOOP_STATEMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_LOOP_STATEMENT, "loop_statement"};
    symbols[NT_CODE_BLOCK] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_CODE_BLOCK, "code_block"};
    symbols[NT_OUTPUT_LIST] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_OUTPUT_LIST, "output_list"};
    symbols[NT_LIST_ELEMENT] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_LIST_ELEMENT, "list_element"};
    // Add test grammar non-terminals
    symbols[NT_E] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_E, "E"};
    symbols[NT_T] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_T, "T"};
    symbols[NT_F] = (GrammarSymbol){SYMBOL_NONTERMINAL, NT_F, "F"};


    // Initialize Terminals
    symbols[T_SEMICOLON] = (GrammarSymbol){SYMBOL_TERMINAL, T_SEMICOLON, ";"};
    symbols[T_NUMBER] = (GrammarSymbol){SYMBOL_TERMINAL, T_NUMBER, "number"};
    symbols[T_WRITE] = (GrammarSymbol){SYMBOL_TERMINAL, T_WRITE, "write"};
    symbols[T_REPEAT] = (GrammarSymbol){SYMBOL_TERMINAL, T_REPEAT, "repeat"};
    symbols[T_AND] = (GrammarSymbol){SYMBOL_TERMINAL, T_AND, "and"};
    symbols[T_TIMES] = (GrammarSymbol){SYMBOL_TERMINAL, T_TIMES, "times"};
    symbols[T_NEWLINE] = (GrammarSymbol){SYMBOL_TERMINAL, T_NEWLINE, "newline"};
    symbols[T_IDENTIFIER] = (GrammarSymbol){SYMBOL_TERMINAL, T_IDENTIFIER, "IDENTIFIER"};
    symbols[T_STRING] = (GrammarSymbol){SYMBOL_TERMINAL, T_STRING, "STRING"};
    symbols[T_INTEGER] = (GrammarSymbol){SYMBOL_TERMINAL, T_INTEGER, "INTEGER"};
    // Add test grammar terminals
    symbols[T_PLUS] = (GrammarSymbol){SYMBOL_TERMINAL, T_PLUS, "+"};
    symbols[T_STAR] = (GrammarSymbol){SYMBOL_TERMINAL, T_STAR, "*"};
    symbols[T_LPAREN] = (GrammarSymbol){SYMBOL_TERMINAL, T_LPAREN, "("};
    symbols[T_RPAREN] = (GrammarSymbol){SYMBOL_TERMINAL, T_RPAREN, ")"};
    symbols[T_ID] = (GrammarSymbol){SYMBOL_TERMINAL, T_ID, "id"};
    symbols[T_EOF] = (GrammarSymbol){SYMBOL_TERMINAL, T_EOF, "$"}; // EOF marker


    // Define Productions for the main grammar
    // Semantic actions are NULL for now as they are not relevant for FIRST/FOLLOW/LR(0)
    Production main_productions[] = {
        // R0: <program> -> <statement_list> (Augmented production for LR(0) start)
        { &symbols[NT_PROGRAM], (GrammarSymbol*[]){&symbols[NT_STATEMENT_LIST]}, 1, 0, default_semantic_action },
        // R1: <statement_list> -> <statement_list> <statement>
        { &symbols[NT_STATEMENT_LIST], (GrammarSymbol*[]){&symbols[NT_STATEMENT_LIST], &symbols[NT_STATEMENT]}, 2, 1, default_semantic_action },
        // R2: <statement_list> -> <statement>
        { &symbols[NT_STATEMENT_LIST], (GrammarSymbol*[]){&symbols[NT_STATEMENT]}, 1, 2, default_semantic_action },
        // R3: <statement> -> <assignment> ;
        { &symbols[NT_STATEMENT], (GrammarSymbol*[]){&symbols[NT_ASSIGNMENT], &symbols[T_SEMICOLON]}, 2, 3, default_semantic_action },
        // R4: <statement> -> <declaration> ;
        { &symbols[NT_STATEMENT], (GrammarSymbol*[]){&symbols[NT_DECLARATION], &symbols[T_SEMICOLON]}, 2, 4, default_semantic_action },
        // R5: <statement> -> <decrement> ;
        { &symbols[NT_STATEMENT], (GrammarSymbol*[]){&symbols[NT_DECREMENT], &symbols[T_SEMICOLON]}, 2, 5, default_semantic_action },
        // R6: <statement> -> <increment> ;
        { &symbols[NT_STATEMENT], (GrammarSymbol*[]){&symbols[NT_INCREMENT], &symbols[T_SEMICOLON]}, 2, 6, default_semantic_action },
        // R7: <statement> -> <write_statement> ;
        { &symbols[NT_STATEMENT], (GrammarSymbol*[]){&symbols[NT_WRITE_STATEMENT], &symbols[T_SEMICOLON]}, 2, 7, default_semantic_action },
        // R8: <statement> -> <loop_statement>
        { &symbols[NT_STATEMENT], (GrammarSymbol*[]){&symbols[NT_LOOP_STATEMENT]}, 1, 8, default_semantic_action },
        // R9: <declaration> -> number IDENTIFIER
        { &symbols[NT_DECLARATION], (GrammarSymbol*[]){&symbols[T_NUMBER], &symbols[T_IDENTIFIER]}, 2, 9, default_semantic_action },
        // R10: <assignment> -> IDENTIFIER := INTEGER (Assuming := is T_ASSIGN, which you need to define)
        // Adding T_ASSIGN in enum if not there:
        // T_ASSIGN = ?, T_PLUS_ASSIGN = ?, T_MINUS_ASSIGN = ?
        // If not defined, you must define them, or map to existing tokens.
        // For this example, let's assume they are defined.
        { &symbols[NT_ASSIGNMENT], (GrammarSymbol*[]){&symbols[T_IDENTIFIER], &symbols[112 /*T_ASSIGN*/], &symbols[T_INTEGER]}, 3, 10, default_semantic_action }, // Placeholder for T_ASSIGN
        // Let's explicitly define T_ASSIGN, T_PLUS_ASSIGN, T_MINUS_ASSIGN
        // symbols[T_ASSIGN] = (GrammarSymbol){SYMBOL_TERMINAL, T_ASSIGN, ":="};
        // symbols[T_PLUS_ASSIGN] = (GrammarSymbol){SYMBOL_TERMINAL, T_PLUS_ASSIGN, "+="};
        // symbols[T_MINUS_ASSIGN] = (GrammarSymbol){SYMBOL_TERMINAL, T_MINUS_ASSIGN, "-="};

        // R11: <decrement> -> IDENTIFIER -= INTEGER
        { &symbols[NT_DECREMENT], (GrammarSymbol*[]){&symbols[T_IDENTIFIER], &symbols[114 /*T_MINUS_ASSIGN*/], &symbols[T_INTEGER]}, 3, 11, default_semantic_action },
        // R12: <increment> -> IDENTIFIER += INTEGER
        { &symbols[NT_INCREMENT], (GrammarSymbol*[]){&symbols[T_IDENTIFIER], &symbols[113 /*T_PLUS_ASSIGN*/], &symbols[T_INTEGER]}, 3, 12, default_semantic_action },
        // R13: <write_statement> -> write <output_list>
        { &symbols[NT_WRITE_STATEMENT], (GrammarSymbol*[]){&symbols[T_WRITE], &symbols[NT_OUTPUT_LIST]}, 2, 13, default_semantic_action },
        // R14: <loop_statement> -> repeat INTEGER times <statement>
        { &symbols[NT_LOOP_STATEMENT], (GrammarSymbol*[]){&symbols[T_REPEAT], &symbols[T_INTEGER], &symbols[T_TIMES], &symbols[NT_STATEMENT]}, 4, 14, default_semantic_action },
        // R15: <loop_statement> -> repeat INTEGER times <code_block>
        { &symbols[NT_LOOP_STATEMENT], (GrammarSymbol*[]){&symbols[T_REPEAT], &symbols[T_INTEGER], &symbols[T_TIMES], &symbols[NT_CODE_BLOCK]}, 4, 15, default_semantic_action },
        // R16: <code_block> -> { <statement_list> } (Assuming { is T_LBRACE, } is T_RBRACE)
        // Adding T_LBRACE, T_RBRACE in enum if not there:
        // T_LBRACE = ?, T_RBRACE = ?
        { &symbols[NT_CODE_BLOCK], (GrammarSymbol*[]){&symbols[110 /*T_LBRACE*/], &symbols[NT_STATEMENT_LIST], &symbols[111 /*T_RBRACE*/]}, 3, 16, default_semantic_action },
        // R17: <output_list> -> <output_list> and <list_element>
        { &symbols[NT_OUTPUT_LIST], (GrammarSymbol*[]){&symbols[NT_OUTPUT_LIST], &symbols[T_AND], &symbols[NT_LIST_ELEMENT]}, 3, 17, default_semantic_action },
        // R18: <output_list> -> <list_element>
        { &symbols[NT_OUTPUT_LIST], (GrammarSymbol*[]){&symbols[NT_LIST_ELEMENT]}, 1, 18, default_semantic_action },
        // R19: <list_element> -> INTEGER
        { &symbols[NT_LIST_ELEMENT], (GrammarSymbol*[]){&symbols[T_INTEGER]}, 1, 19, default_semantic_action },
        // R20: <list_element> -> STRING
        { &symbols[NT_LIST_ELEMENT], (GrammarSymbol*[]){&symbols[T_STRING]}, 1, 20, default_semantic_action },
        // R21: <list_element> -> newline
        { &symbols[NT_LIST_ELEMENT], (GrammarSymbol*[]){&symbols[T_NEWLINE]}, 1, 21, default_semantic_action },

        // Add productions for the simpler test grammar E -> E+T | T, T -> T*F | F, F -> (E) | id
        // This is a common test grammar for LR parsing.
        // Adjust production_id to be unique from your main grammar (e.g., start from 100)
        { &symbols[NT_E], (GrammarSymbol*[]){&symbols[NT_E], &symbols[T_PLUS], &symbols[NT_T]}, 3, 100, default_semantic_action }, // R100: E -> E + T
        { &symbols[NT_E], (GrammarSymbol*[]){&symbols[NT_T]}, 1, 101, default_semantic_action }, // R101: E -> T
        { &symbols[NT_T], (GrammarSymbol*[]){&symbols[NT_T], &symbols[T_STAR], &symbols[NT_F]}, 3, 102, default_semantic_action }, // R102: T -> T * F
        { &symbols[NT_T], (GrammarSymbol*[]){&symbols[NT_F]}, 1, 103, default_semantic_action }, // R103: T -> F
        { &symbols[NT_F], (GrammarSymbol*[]){&symbols[T_LPAREN], &symbols[NT_E], &symbols[T_RPAREN]}, 3, 104, default_semantic_action }, // R104: F -> ( E )
        { &symbols[NT_F], (GrammarSymbol*[]){&symbols[T_ID]}, 1, 105, default_semantic_action } // R105: F -> id
    };
    int num_main_productions = sizeof(main_productions) / sizeof(Production);


    // 2. Setup Grammar structure for the main grammar
    Grammar main_grammar;
    main_grammar.productions = main_productions;
    main_grammar.production_count = num_main_productions;

    main_grammar.terminals = (GrammarSymbol**)malloc(sizeof(GrammarSymbol*) * (T_EOF - T_SEMICOLON + 1));
    int main_term_idx = 0;
    for (int i = T_SEMICOLON; i <= T_EOF; ++i) {
        // Exclude test grammar symbols if only testing main grammar's LR(0) states
        // This requires careful ID management or separate grammar definitions
        if (i != T_PLUS && i != T_STAR && i != T_LPAREN && i != T_RPAREN && i != T_ID) {
            main_grammar.terminals[main_term_idx++] = &symbols[i];
        }
    }
    main_grammar.terminal_count = main_term_idx;

    main_grammar.non_terminals = (GrammarSymbol**)malloc(sizeof(GrammarSymbol*) * (NT_LIST_ELEMENT - NT_PROGRAM + 1));
    int main_non_term_idx = 0;
    for (int i = NT_PROGRAM; i <= NT_LIST_ELEMENT; ++i) {
        main_grammar.non_terminals[main_non_term_idx++] = &symbols[i];
    }
    main_grammar.non_terminal_count = main_non_term_idx;

    main_grammar.start_symbol = &symbols[NT_PROGRAM]; // Set the start symbol for the main grammar

    printf("--- Testing Main Grammar LR(0) Item Sets ---\n");
    create_lr0_sets(&main_grammar); // Compute LR(0) for main grammar

    // --- Cleanup for main_grammar ---
    free(main_grammar.terminals);
    free(main_grammar.non_terminals);


    // --- Setup and Test for the simpler E, T, F grammar ---
    printf("\n--- Testing E, T, F Grammar LR(0) Item Sets ---\n");

    // Define productions for E, T, F
    Production etf_productions[] = {
        // Augmented production S' -> E
        { &symbols[NT_PROGRAM], (GrammarSymbol*[]){&symbols[NT_E]}, 1, 0, default_semantic_action }, // Reusing prod_id 0 and NT_PROGRAM as S'
        { &symbols[NT_E], (GrammarSymbol*[]){&symbols[NT_E], &symbols[T_PLUS], &symbols[NT_T]}, 3, 100, default_semantic_action },
        { &symbols[NT_E], (GrammarSymbol*[]){&symbols[NT_T]}, 1, 101, default_semantic_action },
        { &symbols[NT_T], (GrammarSymbol*[]){&symbols[NT_T], &symbols[T_STAR], &symbols[NT_F]}, 3, 102, default_semantic_action },
        { &symbols[NT_T], (GrammarSymbol*[]){&symbols[NT_F]}, 1, 103, default_semantic_action },
        { &symbols[NT_F], (GrammarSymbol*[]){&symbols[T_LPAREN], &symbols[NT_E], &symbols[T_RPAREN]}, 3, 104, default_semantic_action },
        { &symbols[NT_F], (GrammarSymbol*[]){&symbols[T_ID]}, 1, 105, default_semantic_action }
    };
    int num_etf_productions = sizeof(etf_productions) / sizeof(Production);

    Grammar etf_grammar;
    etf_grammar.productions = etf_productions;
    etf_grammar.production_count = num_etf_productions;

    etf_grammar.terminals = (GrammarSymbol**)malloc(sizeof(GrammarSymbol*) * (T_EOF - T_PLUS + 1));
    int etf_term_idx = 0;
    // Only include terminals relevant to E,T,F grammar + EOF
    etf_grammar.terminals[etf_term_idx++] = &symbols[T_PLUS];
    etf_grammar.terminals[etf_term_idx++] = &symbols[T_STAR];
    etf_grammar.terminals[etf_term_idx++] = &symbols[T_LPAREN];
    etf_grammar.terminals[etf_term_idx++] = &symbols[T_RPAREN];
    etf_grammar.terminals[etf_term_idx++] = &symbols[T_ID];
    etf_grammar.terminals[etf_term_idx++] = &symbols[T_EOF];
    etf_grammar.terminal_count = etf_term_idx;

    etf_grammar.non_terminals = (GrammarSymbol**)malloc(sizeof(GrammarSymbol*) * 4); // NT_PROGRAM, NT_E, NT_T, NT_F
    int etf_non_term_idx = 0;
    etf_grammar.non_terminals[etf_non_term_idx++] = &symbols[NT_PROGRAM]; // S' for this test
    etf_grammar.non_terminals[etf_non_term_idx++] = &symbols[NT_E];
    etf_grammar.non_terminals[etf_non_term_idx++] = &symbols[NT_T];
    etf_grammar.non_terminals[etf_non_term_idx++] = &symbols[NT_F];
    etf_grammar.non_terminal_count = etf_non_term_idx;

    etf_grammar.start_symbol = &symbols[NT_PROGRAM]; // S' -> E

    create_lr0_sets(&etf_grammar); // Compute LR(0) for E,T,F grammar

    // --- Cleanup for etf_grammar ---
    free(etf_grammar.terminals);
    free(etf_grammar.non_terminals);


    return 0;
}
