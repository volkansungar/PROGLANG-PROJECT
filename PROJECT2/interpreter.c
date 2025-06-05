#include "interpreter.h" // Include its own header
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Global or passed-around runtime symbol table instance
static RuntimeSymbolTable global_runtime_sym_table;

// Forward declarations for interpret functions for different AST node types (internal to this file)
static void interpret_statement_list(ASTNode* node);
static void interpret_statement(ASTNode* node); // Forward declaration for interpret_statement
static void interpret_declaration(ASTNode* node);
static void interpret_assignment(ASTNode* node);
static void interpret_increment(ASTNode* node);
static void interpret_decrement(ASTNode* node);
static void interpret_write_statement(ASTNode* node);
static void interpret_loop_statement(ASTNode* node);
static void interpret_code_block(ASTNode* node);
static long long evaluate_int_value(ASTNode* node); // Function to get value from <int_value>

// --- Runtime Symbol Table (Environment) Implementations ---

void init_runtime_symbol_table(RuntimeSymbolTable* table) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

void add_or_update_runtime_symbol(RuntimeSymbolTable* table, const char* name, long long value) {
    // Check if symbol already exists, then update
    for (int i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            table->entries[i].value = value;
            return;
        }
    }

    // Symbol does not exist, add new entry
    if (table->count >= table->capacity) {
        // Double capacity or set to a default if currently 0
        table->capacity = (table->capacity == 0) ? 4 : table->capacity * 2;
        table->entries = (RuntimeSymbolEntry*)realloc(table->entries, table->capacity * sizeof(RuntimeSymbolEntry));
        if (!table->entries) {
            fprintf(stderr, "Memory allocation failed for runtime symbol table.\n");
            exit(EXIT_FAILURE);
        }
    }

    table->entries[table->count].name = strdup(name); // Duplicate string for ownership
    if (!table->entries[table->count].name) {
        fprintf(stderr, "Memory allocation failed for symbol name.\n");
        exit(EXIT_FAILURE);
    }
    table->entries[table->count].value = value;
    table->count++;
}

bool lookup_runtime_symbol(RuntimeSymbolTable* table, const char* name, long long* out_value) {
    for (int i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            if (out_value) {
                *out_value = table->entries[i].value;
            }
            return true;
        }
    }
    return false;
}

void free_runtime_symbol_table(RuntimeSymbolTable* table) {
    for (int i = 0; i < table->count; ++i) {
        free(table->entries[i].name); // Free duplicated names
    }
    free(table->entries); // Free the array itself
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

// --- Interpreter Logic Implementations ---

// Main interpretation entry point (defined here, declared in interpreter.h)
void interpret_program(ASTNode* root_node) {
    // The root node should be of type AST_PROGRAM.
    // Its first child is the StatementList.
    if (!root_node || root_node->type != AST_PROGRAM || root_node->num_children != 1 ||
        root_node->children[0]->type != AST_STATEMENT_LIST) {
        fprintf(stderr, "Interpreter Error: Invalid AST root node. Expected AST_PROGRAM with a StatementList child.\n");
        return;
    }

    init_runtime_symbol_table(&global_runtime_sym_table);

    printf("\n--- Starting Program Execution ---\n");

    // The PROGRAM node has a single child: the STATEMENT_LIST
    interpret_statement_list(root_node->children[0]);

    printf("--- Program Execution Finished ---\n");

    free_runtime_symbol_table(&global_runtime_sym_table);
}


static void interpret_statement_list(ASTNode* node) {
    if (!node || node->type != AST_STATEMENT_LIST) {
        fprintf(stderr, "Interpreter Error: Invalid AST_STATEMENT_LIST node structure.\n");
        return;
    }

    for (int i = 0; i < node->num_children; ++i) {
        interpret_statement(node->children[i]);
    }
}

static void interpret_statement(ASTNode* node) {
    if (!node) {
        fprintf(stderr, "Interpreter Error: NULL statement node.\n");
        return;
    }

    switch (node->type) {
        case AST_DECLARATION:
            interpret_declaration(node);
            break;
        case AST_ASSIGNMENT:
            interpret_assignment(node);
            break;
        case AST_INCREMENT:
            interpret_increment(node);
            break;
        case AST_DECREMENT:
            interpret_decrement(node);
            break;
        case AST_WRITE_STATEMENT:
            interpret_write_statement(node);
            break;
        case AST_LOOP_STATEMENT:
            interpret_loop_statement(node);
            break;
        // AST_STATEMENT_LIST is handled by interpret_statement_list
        // AST_PROGRAM is handled by interpret_program
        // Other types are not expected as top-level statements
        default:
            fprintf(stderr, "Interpreter Error: Unexpected AST node type for a statement: %d\n", node->type);
            break;
    }
}


static void interpret_declaration(ASTNode* node) {
    if (!node || node->type != AST_DECLARATION || node->num_children != 1 ||
        node->children[0]->type != AST_IDENTIFIER) {
        fprintf(stderr, "Interpreter Error: Invalid AST_DECLARATION node structure.\n");
        return;
    }

    char* var_name = node->children[0]->data.identifier.name;
    // Declare with default value 0
    add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, 0);
    printf("[DEBUG] Declared variable '%s' with initial value 0.\n", var_name);
}

static void interpret_assignment(ASTNode* node) {
    if (!node || node->type != AST_ASSIGNMENT || node->num_children != 2 ||
        node->children[0]->type != AST_IDENTIFIER || node->children[1]->type != AST_INT_VALUE) {
        fprintf(stderr, "Interpreter Error: Invalid AST_ASSIGNMENT node structure.\n");
        return;
    }

    char* var_name = node->children[0]->data.identifier.name;
    long long value = evaluate_int_value(node->children[1]);

    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, NULL)) {
        add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, value);
        printf("[DEBUG] Assigned '%s' := %lld.\n", var_name, value);
    } else {
        fprintf(stderr, "Runtime Error: Undeclared variable '%s' in assignment at line %d, column %d.\n",
                var_name, node->location.line, node->location.column);
    }
}

static void interpret_increment(ASTNode* node) {
    if (!node || node->type != AST_INCREMENT || node->num_children != 2 ||
        node->children[0]->type != AST_IDENTIFIER || node->children[1]->type != AST_INT_VALUE) {
        fprintf(stderr, "Interpreter Error: Invalid AST_INCREMENT node structure.\n");
        return;
    }

    char* var_name = node->children[0]->data.identifier.name;
    long long increment_value = evaluate_int_value(node->children[1]);
    long long current_value;

    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &current_value)) {
        add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, current_value + increment_value);
        printf("[DEBUG] Incremented '%s' by %lld. New value: %lld.\n", var_name, increment_value, current_value + increment_value);
    } else {
        fprintf(stderr, "Runtime Error: Undeclared variable '%s' in increment at line %d, column %d.\n",
                var_name, node->location.line, node->location.column);
    }
}

static void interpret_decrement(ASTNode* node) {
    if (!node || node->type != AST_DECREMENT || node->num_children != 2 ||
        node->children[0]->type != AST_IDENTIFIER || node->children[1]->type != AST_INT_VALUE) {
        fprintf(stderr, "Interpreter Error: Invalid AST_DECREMENT node structure.\n");
        return;
    }

    char* var_name = node->children[0]->data.identifier.name;
    long long decrement_value = evaluate_int_value(node->children[1]);
    long long current_value;

    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &current_value)) {
        add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, current_value - decrement_value);
        printf("[DEBUG] Decremented '%s' by %lld. New value: %lld.\n", var_name, decrement_value, current_value - decrement_value);
    } else {
        fprintf(stderr, "Runtime Error: Undeclared variable '%s' in decrement at line %d, column %d.\n",
                var_name, node->location.line, node->location.column);
    }
}


static void interpret_write_statement(ASTNode* node) {
    if (!node || node->type != AST_WRITE_STATEMENT || node->num_children != 1 ||
        node->children[0]->type != AST_OUTPUT_LIST) {
        fprintf(stderr, "Interpreter Error: Invalid AST_WRITE_STATEMENT node structure.\n");
        return;
    }

    ASTNode* output_list_node = node->children[0];

    for (int i = 0; i < output_list_node->num_children; ++i) {
        ASTNode* list_element = output_list_node->children[i];
        if (!list_element || list_element->num_children != 1) { // Each list element has one child (int_value, string, newline)
            fprintf(stderr, "Interpreter Error: Invalid AST_LIST_ELEMENT node structure within output list.\n");
            continue;
        }

        ASTNode* element_content = list_element->children[0];

        switch (element_content->type) {
            case AST_INT_VALUE: {
                long long value = evaluate_int_value(element_content);
                printf("%lld", value);
                break;
            }
            case AST_STRING_LITERAL:
                printf("%s", element_content->data.string_value);
                break;
            case AST_NEWLINE:
                printf("\n");
                break;
            default:
                fprintf(stderr, "Interpreter Error: Unsupported AST node type in output list: %d\n", element_content->type);
                break;
        }
    }
}


static void interpret_loop_statement(ASTNode* node) {
    // Loop statement structure: AST_LOOP_STATEMENT (with data.loop.count_expr and data.loop.body)
    if (!node || node->type != AST_LOOP_STATEMENT || !node->data.loop.count_expr || !node->data.loop.body) {
        fprintf(stderr, "Interpreter Error: Invalid AST_LOOP_STATEMENT node structure. Missing count_expr or body.\n");
        return;
    }

    ASTNode* count_expr_node = node->data.loop.count_expr;
    ASTNode* body_node = node->data.loop.body;

    long long count = evaluate_int_value(count_expr_node);

    printf("[DEBUG] Interpreting loop statement (count: %lld).\n", count);

    for (long long i = 0; i < count; ++i) {
        if (body_node->type == AST_STATEMENT) {
            interpret_statement(body_node);
        } else if (body_node->type == AST_CODE_BLOCK) {
            interpret_code_block(body_node);
        } else {
            fprintf(stderr, "Interpreter Error: Invalid loop body type: %d\n", body_node->type);
            break; // Exit loop on invalid body to prevent infinite errors
        }
    }
}

static void interpret_code_block(ASTNode* node) {
    if (!node || node->type != AST_CODE_BLOCK || node->num_children != 1 || node->children[0]->type != AST_STATEMENT_LIST) {
        fprintf(stderr, "Interpreter Error: Invalid AST_CODE_BLOCK node structure.\n");
        return;
    }
    printf("[DEBUG] Entering code block.\n");
    interpret_statement_list(node->children[0]);
    printf("[DEBUG] Exiting code block.\n");
}


// Evaluates an <int_value> AST node to its numerical value
static long long evaluate_int_value(ASTNode* node) {
    if (!node || node->type != AST_INT_VALUE || node->num_children != 1) {
        fprintf(stderr, "Interpreter Error: Invalid AST_INT_VALUE node structure. Expected one child.\n");
        return 0;
    }

    ASTNode* child = node->children[0];
    if (child->type == AST_INTEGER_LITERAL) {
        return child->data.int_value;
    } else if (child->type == AST_IDENTIFIER) {
        char* var_name = child->data.identifier.name;
        long long value;
        if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &value)) {
            return value;
        } else {
            fprintf(stderr, "Runtime Error: Undeclared variable '%s' used in expression at line %d, column %d.\n",
                    var_name, node->location.line, node->location.column);
            return 0; // Return 0 for undeclared variable to avoid crash
        }
    } else {
        fprintf(stderr, "Interpreter Error: Invalid child type for AST_INT_VALUE: %d\n", child->type);
        return 0;
    }
}
