#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Dummy ASTNode for compilation
typedef struct ASTNode {
    int type;
    // ... other fields
} ASTNode;

enum SymbolIDs {
    // Non-terminals
    NT_PROGRAM = 0,
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
    NT_E,
    NT_T,
    NT_F,
    NT_S_PRIME, // Augmented start symbol

    // Terminals
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
    T_ASSIGN,
    T_PLUS_ASSIGN,
    T_MINUS_ASSIGN,
    T_LBRACE,
    T_RBRACE,
    T_PLUS,
    T_STAR,
    T_LPAREN,
    T_RPAREN,
    T_ID,
    T_EOF = 90
};

typedef enum {
    SYMBOL_TERMINAL,
    SYMBOL_NONTERMINAL
} SymbolType;

typedef struct {
    SymbolType type;
    int id;
    char* name;
} GrammarSymbol;

typedef struct Production {
    GrammarSymbol* left_symbol;
    GrammarSymbol** right_symbols;
    int right_count;
    int production_id;
    ASTNode* (*semantic_action)(ASTNode** children);
} Production;

typedef struct Grammar {
    Production *productions;
    int production_count;
    GrammarSymbol** terminals;
    int terminal_count;
    GrammarSymbol** non_terminals;
    int non_terminal_count;
    GrammarSymbol* start_symbol;
} Grammar;

typedef struct LRItem {
    int production_id;
    int dot_position;
    GrammarSymbol** lookahead_set; // Not used for LR(0), but kept for consistency
    int lookahead_count;          // Not used for LR(0), but kept for consistency
} LRItem;

typedef struct ItemSet {
    LRItem* items;
    int item_count;
    int state_id;
    int capacity;
} ItemSet;

typedef struct ItemSetList {
    ItemSet** sets; // Now stores pointers to ItemSet
    int count;
    int capacity;
} ItemSetList;

typedef enum ActionType {
    ACTION_SHIFT,
    ACTION_REDUCE,
    ACTION_ACCEPT,
    ACTION_ERROR
} Action;

typedef struct ParsingTable {
    Action** action_table;
    int** goto_table;
    int state_count;
    int terminal_count;
    int non_terminal_count;
} ParsingTable;

typedef struct StackEntry {
    int state;
    int symbol;
    ASTNode *ast_node;
} StackEntry;

typedef struct ParserStack {
    StackEntry* entries;
    int top;
    int capacity;
} ParserStack;

// FIRST SET OPERATIONS
#define MAX_TERMINALS 200
#define MAX_NON_TERMINALS 200

typedef unsigned char TerminalSet[MAX_TERMINALS / 8 + (MAX_TERMINALS % 8 != 0)];
TerminalSet firstSetsForNonTerminals[MAX_NON_TERMINALS];
TerminalSet followSetsForNonTerminals[MAX_NON_TERMINALS];

void set_add(TerminalSet s, int terminal_id) {
    if (terminal_id >= 0 && terminal_id < MAX_TERMINALS) {
        s[terminal_id / 8] |= (1 << (terminal_id % 8));
    }
}

int set_contains(const TerminalSet s, int terminal_id) {
    if (terminal_id >= 0 && terminal_id < MAX_TERMINALS) {
        return (s[terminal_id / 8] & (1 << (terminal_id % 8))) != 0;
    }
    return 0;
}

void set_clear(TerminalSet s) {
    memset(s, 0, sizeof(TerminalSet));
}

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

// Helper function to compute nullable set for all non-terminals
void compute_nullable_set(Grammar* grammar, bool* nullable) {
    // Initialize all non-terminals as not nullable
    for (int i = 0; i < MAX_NON_TERMINALS; i++) {
        nullable[i] = false;
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
    for (int i = 0; i < MAX_NON_TERMINALS; i++) {
        set_clear(firstSetsForNonTerminals[i]);
    }

    // Compute nullable set first
    bool nullable_status[MAX_NON_TERMINALS];
    for(int i = 0; i < MAX_NON_TERMINALS; ++i) {
        nullable_status[i] = false;
    }
    compute_nullable_set(grammar, nullable_status);

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            GrammarSymbol* A = p->left_symbol;

            if (p->right_count == 0) {
                continue;
            }

            for (int j = 0; j < p->right_count; j++) {
                GrammarSymbol* current_symbol = p->right_symbols[j];

                if (current_symbol->type == SYMBOL_TERMINAL) {
                    if (!set_contains(firstSetsForNonTerminals[A->id], current_symbol->id)) {
                        set_add(firstSetsForNonTerminals[A->id], current_symbol->id);
                        changed = true;
                    }
                    break;
                } else {
                    if (set_union(firstSetsForNonTerminals[A->id],
                                  firstSetsForNonTerminals[current_symbol->id])) {
                        changed = true;
                    }

                    if (!nullable_status[current_symbol->id]) {
                        break;
                    }
                }
            }
        }
    }
}

void compute_follow_sets(Grammar* grammar) {
    // Initialize FOLLOW sets as empty for all non-terminals
    for (int i = 0; i < MAX_NON_TERMINALS; i++) {
        set_clear(followSetsForNonTerminals[i]);
    }

    bool nullable_status[MAX_NON_TERMINALS];
    for(int i = 0; i < MAX_NON_TERMINALS; ++i) {
        nullable_status[i] = false;
    }
    compute_nullable_set(grammar, nullable_status);
    compute_first_sets(grammar);

    // Add $ to FOLLOW(start_symbol)
    set_add(followSetsForNonTerminals[grammar->start_symbol->id], T_EOF);

    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < grammar->production_count; i++) {
            Production* p = &grammar->productions[i];
            GrammarSymbol* A = p->left_symbol;

            for (int j = 0; j < p->right_count; j++) {
                GrammarSymbol* B = p->right_symbols[j];

                if (B->type == SYMBOL_NONTERMINAL) {
                    bool beta_is_nullable = true;
                    for (int k = j + 1; k < p->right_count; k++) {
                        GrammarSymbol* Y = p->right_symbols[k];

                        if (Y->type == SYMBOL_TERMINAL) {
                            if (!set_contains(followSetsForNonTerminals[B->id], Y->id)) {
                                set_add(followSetsForNonTerminals[B->id], Y->id);
                                changed = true;
                            }
                            beta_is_nullable = false;
                            break;
                        } else {
                            if (set_union(followSetsForNonTerminals[B->id],
                                          firstSetsForNonTerminals[Y->id])) {
                                changed = true;
                            }
                            if (!nullable_status[Y->id]) {
                                beta_is_nullable = false;
                                break;
                            }
                        }
                    }

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

// LR(0) ITEM SET OPERATIONS

ASTNode* default_semantic_action(ASTNode** children) {
    return NULL;
}

bool lr_item_equal(const LRItem* item1, const LRItem* item2) {
    return (item1->production_id == item2->production_id &&
            item1->dot_position == item2->dot_position);
}

int lr_item_hash(const LRItem* item) {
    return item->production_id * 31 + item->dot_position;
}

bool item_set_contains_item(const ItemSet* set, const LRItem* item) {
    for (int i = 0; i < set->item_count; ++i) {
        if (lr_item_equal(&set->items[i], item)) {
            return true;
        }
    }
    return false;
}

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

void item_set_init(ItemSet* set) {
    set->items = NULL;
    set->item_count = 0;
    set->capacity = 0;
    set->state_id = -1;
}

void item_set_free(ItemSet* set) {
    if (set && set->items) { // Check if set is not NULL and items are allocated
        free(set->items);
        set->items = NULL;
    }
    if (set) { // Only clear if set itself is not NULL
        set->item_count = 0;
        set->capacity = 0;
    }
}

bool item_sets_equal(const ItemSet* set1, const ItemSet* set2) {
    if (set1->item_count != set2->item_count) {
        return false;
    }

    for (int i = 0; i < set1->item_count; ++i) {
        if (!item_set_contains_item(set2, &set1->items[i])) {
            return false;
        }
    }
    return true;
}

void closure(ItemSet *items_set, Grammar *grammar, bool* nullable) {
    bool changed = true;

    while (changed) {
        changed = false;

        // Collect new items in a temporary list to avoid modifying `items_set` during iteration
        LRItem* temp_new_items = NULL;
        int temp_new_item_count = 0;
        int temp_new_item_capacity = 0;

        for (int i = 0; i < items_set->item_count; ++i) {
            LRItem current_item = items_set->items[i];
            Production* p = &grammar->productions[current_item.production_id];

            if (current_item.dot_position < p->right_count) {
                GrammarSymbol* symbol_after_dot = p->right_symbols[current_item.dot_position];

                if (symbol_after_dot->type == SYMBOL_NONTERMINAL) {
                    for (int prod_idx = 0; prod_idx < grammar->production_count; ++prod_idx) {
                        Production* B_prod = &grammar->productions[prod_idx];

                        if (B_prod->left_symbol->id == symbol_after_dot->id) {
                            LRItem new_item = {
                                .production_id = B_prod->production_id,
                                .dot_position = 0,
                                .lookahead_set = NULL,
                                .lookahead_count = 0
                            };

                            // Check if this new item is already in the original set
                            bool already_in_original = false;
                            for (int k = 0; k < items_set->item_count; k++) {
                                if (lr_item_equal(&items_set->items[k], &new_item)) {
                                    already_in_original = true;
                                    break;
                                }
                            }

                            if (!already_in_original) {
                                // Check if it's already in our temporary new_items list
                                bool already_added_to_temp = false;
                                for (int k = 0; k < temp_new_item_count; k++) {
                                    if (lr_item_equal(&temp_new_items[k], &new_item)) {
                                        already_added_to_temp = true;
                                        break;
                                    }
                                }

                                if (!already_added_to_temp) {
                                    if (temp_new_item_count >= temp_new_item_capacity) {
                                        temp_new_item_capacity = (temp_new_item_capacity == 0) ? 4 : temp_new_item_capacity * 2;
                                        temp_new_items = (LRItem*)realloc(temp_new_items, temp_new_item_capacity * sizeof(LRItem));
                                        if (temp_new_items == NULL) {
                                            fprintf(stderr, "Memory allocation failed for temp_new_items in closure.\n");
                                            exit(EXIT_FAILURE);
                                        }
                                    }
                                    temp_new_items[temp_new_item_count++] = new_item;
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Add all newly found items from the temporary list to the main items_set
        for (int i = 0; i < temp_new_item_count; ++i) {
            item_set_add_item(items_set, temp_new_items[i]);
        }

        if (temp_new_items) {
            free(temp_new_items);
        }
    }
}

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

        if (current_item.dot_position < p->right_count &&
            p->right_symbols[current_item.dot_position]->id == X->id) {

            LRItem new_item = {
                .production_id = current_item.production_id,
                .dot_position = current_item.dot_position + 1,
                .lookahead_set = NULL,
                .lookahead_count = 0
            };
            item_set_add_item(J, new_item);
        }
    }

    closure(J, grammar, nullable);
    return J;
}

void item_set_list_init(ItemSetList* list) {
    list->sets = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Modified: Add a pointer to an ItemSet
void item_set_list_add(ItemSetList* list, ItemSet* new_set_ptr) {
    if (list->count >= list->capacity) {
        list->capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        list->sets = (ItemSet**)realloc(list->sets, list->capacity * sizeof(ItemSet*));
        if (list->sets == NULL) {
            fprintf(stderr, "Memory allocation failed for ItemSetList.\n");
            exit(EXIT_FAILURE);
        }
    }
    list->sets[list->count++] = new_set_ptr; // Store the pointer
}

// Modified: Compare ItemSets pointed to
int find_item_set_in_list(const ItemSetList* list, const ItemSet* target_set) {
    for (int i = 0; i < list->count; ++i) {
        if (item_sets_equal(list->sets[i], target_set)) { // Dereference for comparison
            return i;
        }
    }
    return -1;
}

void print_item_set(const ItemSet* set, Grammar* grammar) {
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
        printf("\n");
    }
}

void create_lr0_sets(Grammar* grammar) {
    ItemSetList canonical_collection;
    item_set_list_init(&canonical_collection);

    bool nullable_status[MAX_NON_TERMINALS];
    for(int i = 0; i < MAX_NON_TERMINALS; ++i) {
        nullable_status[i] = false;
    }
    compute_nullable_set(grammar, nullable_status);

    // Create initial item set I0
    ItemSet* I0 = (ItemSet*)malloc(sizeof(ItemSet)); // Allocate on heap
    if (I0 == NULL) {
        fprintf(stderr, "Memory allocation failed for I0.\n");
        exit(EXIT_FAILURE);
    }
    item_set_init(I0);
    I0->state_id = 0;

    LRItem initial_item = {
        .production_id = 0, // Assuming production 0 is S' -> S
        .dot_position = 0,
        .lookahead_set = NULL,
        .lookahead_count = 0
    };
    item_set_add_item(I0, initial_item);
    closure(I0, grammar, nullable_status);

    item_set_list_add(&canonical_collection, I0);

    // Queue for processing (stores pointers to ItemSet)
    ItemSet** queue = (ItemSet**)malloc(sizeof(ItemSet*) * 100); // Stores pointers
    int queue_size = 0;
    int queue_front = 0;

    // Add I0 to queue (store its pointer)
    if (queue_size < 100) {
        queue[queue_size++] = I0;
    } else {
        fprintf(stderr, "Queue capacity exceeded.\n");
        exit(EXIT_FAILURE);
    }

    while (queue_front < queue_size) {
        ItemSet* current_I = queue[queue_front++]; // Get pointer from queue

        // Get all symbols that can follow the dot
        GrammarSymbol** symbols_to_process = (GrammarSymbol**)malloc(
            sizeof(GrammarSymbol*) * (grammar->terminal_count + grammar->non_terminal_count));
        int symbol_count = 0;

        // Add terminals
        for (int i = 0; i < grammar->terminal_count; i++) {
            // Exclude EOF from symbols to process for GOTO, as it's only for reductions/acceptance
            if (grammar->terminals[i]->id != T_EOF) {
                symbols_to_process[symbol_count++] = grammar->terminals[i];
            }
        }
        // Add non-terminals
        for (int i = 0; i < grammar->non_terminal_count; i++) {
            // Exclude augmented start symbol as it's not on the right side of any production
            if (grammar->non_terminals[i]->id != NT_S_PRIME) {
                symbols_to_process[symbol_count++] = grammar->non_terminals[i];
            }
        }


        for (int i = 0; i < symbol_count; i++) {
            GrammarSymbol* X = symbols_to_process[i];
            ItemSet* J = goto_set(current_I, X, grammar, nullable_status);

            if (J->item_count > 0) {
                int existing_idx = find_item_set_in_list(&canonical_collection, J);

                if (existing_idx == -1) {
                    J->state_id = canonical_collection.count;
                    item_set_list_add(&canonical_collection, J); // Store J's pointer

                    // Add to queue if there's space
                    if (queue_size < 100) { // Limit queue size for this test
                        queue[queue_size++] = J; // Store J's pointer
                    } else {
                         fprintf(stderr, "Queue capacity exceeded for new state.\n");
                         // If queue full, free J here to avoid leak
                         item_set_free(J);
                         free(J);
                    }
                } else {
                    // If J already exists, free the newly created J
                    item_set_free(J);
                    free(J);
                }
            } else {
                // If J is empty, free it
                item_set_free(J);
                free(J);
            }
        }

        free(symbols_to_process);
        // current_I is owned by canonical_collection or queue, do NOT free here.
    }

    // Print results
    printf("\n--- Generated LR(0) Item Sets ---\n");
    for (int i = 0; i < canonical_collection.count; ++i) {
        print_item_set(canonical_collection.sets[i], grammar); // Dereference pointer
        printf("\n");
    }

    // Cleanup: Free all ItemSet structs stored in the canonical_collection
    for (int i = 0; i < canonical_collection.count; ++i) {
        item_set_free(canonical_collection.sets[i]); // Free the internal items
        free(canonical_collection.sets[i]);         // Free the ItemSet struct itself
    }
    free(canonical_collection.sets); // Free the array of pointers

    // The queue pointers point to the same ItemSet structs freed above, so no need to free queue items again.
    free(queue); // Free the array of pointers for the queue
}


// --- Test Code ---
int main() {
    // 1. Define Grammar Symbols
    GrammarSymbol s_prime = {SYMBOL_NONTERMINAL, NT_S_PRIME, "S'"};
    GrammarSymbol E = {SYMBOL_NONTERMINAL, NT_E, "E"};
    GrammarSymbol T = {SYMBOL_NONTERMINAL, NT_T, "T"};
    GrammarSymbol F = {SYMBOL_NONTERMINAL, NT_F, "F"};
    GrammarSymbol plus = {SYMBOL_TERMINAL, T_PLUS, "+"};
    GrammarSymbol star = {SYMBOL_TERMINAL, T_STAR, "*"};
    GrammarSymbol lparen = {SYMBOL_TERMINAL, T_LPAREN, "("};
    GrammarSymbol rparen = {SYMBOL_TERMINAL, T_RPAREN, ")"};
    GrammarSymbol id = {SYMBOL_TERMINAL, T_ID, "id"};
    GrammarSymbol eof_sym = {SYMBOL_TERMINAL, T_EOF, "$"}; // Representing EOF

    // 2. Define Productions (for a simple arithmetic grammar: E -> E+T | T, T -> T*F | F, F -> (E) | id)
    Production productions[7]; // Increased size for F -> id

    // Augmented production S' -> E
    productions[0] = (Production){&s_prime, (GrammarSymbol*[]){&E}, 1, 0, default_semantic_action};

    // E -> E + T
    productions[1] = (Production){&E, (GrammarSymbol*[]){&E, &plus, &T}, 3, 1, default_semantic_action};
    // E -> T
    productions[2] = (Production){&E, (GrammarSymbol*[]){&T}, 1, 2, default_semantic_action};

    // T -> T * F
    productions[3] = (Production){&T, (GrammarSymbol*[]){&T, &star, &F}, 3, 3, default_semantic_action};
    // T -> F
    productions[4] = (Production){&T, (GrammarSymbol*[]){&F}, 1, 4, default_semantic_action};

    // F -> ( E )
    productions[5] = (Production){&F, (GrammarSymbol*[]){&lparen, &E, &rparen}, 3, 5, default_semantic_action};
    // F -> id
    productions[6] = (Production){&F, (GrammarSymbol*[]){&id}, 1, 6, default_semantic_action};


    // 3. Populate Grammar Structure
    Grammar grammar;
    grammar.productions = productions;
    grammar.production_count = 7; // Updated production count

    GrammarSymbol* terminals[] = {&plus, &star, &lparen, &rparen, &id, &eof_sym};
    grammar.terminals = terminals;
    grammar.terminal_count = sizeof(terminals) / sizeof(terminals[0]);

    GrammarSymbol* non_terminals[] = {&s_prime, &E, &T, &F};
    grammar.non_terminals = non_terminals;
    grammar.non_terminal_count = sizeof(non_terminals) / sizeof(non_terminals[0]);

    grammar.start_symbol = &s_prime; // The augmented start symbol

    // 4. Test Functions
    printf("--- Computing FIRST Sets ---\n");
    compute_first_sets(&grammar);

    // Print FIRST sets (manual check)
    for (int i = 0; i < grammar.non_terminal_count; ++i) {
        GrammarSymbol* nt = grammar.non_terminals[i];
        printf("FIRST(%s): { ", nt->name);
        for (int j = 0; j < grammar.terminal_count; ++j) {
            if (set_contains(firstSetsForNonTerminals[nt->id], grammar.terminals[j]->id)) {
                printf("%s ", grammar.terminals[j]->name);
            }
        }
        printf("}\n");
    }

    printf("\n--- Computing FOLLOW Sets ---\n");
    compute_follow_sets(&grammar);

    // Print FOLLOW sets (manual check)
    for (int i = 0; i < grammar.non_terminal_count; ++i) {
        GrammarSymbol* nt = grammar.non_terminals[i];
        printf("FOLLOW(%s): { ", nt->name);
        for (int j = 0; j < grammar.terminal_count; ++j) {
            if (set_contains(followSetsForNonTerminals[nt->id], grammar.terminals[j]->id)) {
                printf("%s ", grammar.terminals[j]->name);
            }
        }
        printf("}\n");
    }

    // Test LR(0) Item Set Generation
    create_lr0_sets(&grammar);

    return 0;
}
