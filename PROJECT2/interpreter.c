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
// Changed return type and parameter type to BigInt for evaluation
static void evaluate_big_int_value(ASTNode* node, BigInt* result);




// --- Runtime Symbol Table (Environment) Implementations ---

void init_runtime_symbol_table(RuntimeSymbolTable* table) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

// Changed value to const BigInt*
void add_or_update_runtime_symbol(RuntimeSymbolTable* table, const char* name, const BigInt* value) {
    // Check if symbol already exists, then update
    for (int i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            // Update the existing BigInt value
            big_int_copy(&table->entries[i].value, value); // Use your BigInt copy function
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
    // Copy the BigInt value to the new entry
    big_int_copy(&table->entries[table->count].value, value); // Use your BigInt copy function
    table->count++;
}

// Changed out_value to BigInt*
bool lookup_runtime_symbol(RuntimeSymbolTable* table, const char* name, BigInt* out_value) {
    for (int i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            if (out_value) {
                big_int_copy(out_value, &table->entries[i].value); // Copy the BigInt value
            }
            return true;
        }
    }
    return false;
}

void free_runtime_symbol_table(RuntimeSymbolTable* table) {
    for (int i = 0; i < table->count; ++i) {
        free(table->entries[i].name); // Free duplicated names
        // No need to free BigInt.value as it's stored by value, not pointer.
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

    printf("\n--- Program Execution Finished ---\n");

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
    BigInt dummy_lookup; // Dummy for lookup, we only care if it exists

    // Check if the variable is already declared
    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &dummy_lookup)) {
        fprintf(stderr, "Runtime Error: Variable '%s' already declared at line %d, column %d.\n",
                var_name, node->location.line, node->location.column);
        return; // Stop processing this declaration
    }

    // Declare with default value 0 (as BigInt)
    BigInt zero_val;
    big_int_zero(&zero_val);
    add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, &zero_val);
    printf("[DEBUG] Declared variable '%s' with initial value 0.\n", var_name);
}

static void interpret_assignment(ASTNode* node) {
    if (!node || node->type != AST_ASSIGNMENT || node->num_children != 2 ||
        node->children[0]->type != AST_IDENTIFIER || node->children[1]->type != AST_INT_VALUE) {
        fprintf(stderr, "Interpreter Error: Invalid AST_ASSIGNMENT node structure.\n");
        return;
    }

    char* var_name = node->children[0]->data.identifier.name;
    BigInt value_to_assign; // Result of evaluation
    evaluate_big_int_value(node->children[1], &value_to_assign);

    BigInt dummy_lookup; // Dummy for lookup, if we only care if it exists
    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &dummy_lookup)) {
        add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, &value_to_assign);
        printf("[DEBUG] Assigned '%s' := ", var_name);
        big_int_print(&value_to_assign);
        printf(".\n");
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
    BigInt increment_val;
    evaluate_big_int_value(node->children[1], &increment_val);

    BigInt current_value;
    BigInt new_value;

    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &current_value)) {
        big_int_add(&new_value, &current_value, &increment_val);
        add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, &new_value);
        printf("[DEBUG] Incremented '%s' by ", var_name);
        big_int_print(&increment_val);
        printf(". New value: ");
        big_int_print(&new_value);
        printf(".\n");
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
    BigInt decrement_val;
    evaluate_big_int_value(node->children[1], &decrement_val);

    BigInt current_value;
    BigInt new_value;

    if (lookup_runtime_symbol(&global_runtime_sym_table, var_name, &current_value)) {
        big_int_sub(&new_value, &current_value, &decrement_val);
        add_or_update_runtime_symbol(&global_runtime_sym_table, var_name, &new_value);
        printf("[DEBUG] Decremented '%s' by ", var_name);
        big_int_print(&decrement_val);
        printf(". New value: ");
        big_int_print(&new_value);
        printf(".\n");
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
    char str_buffer[MAX_BIGINT_STRING_LEN + 1]; // Buffer for printing BigInts

    for (int i = 0; i < output_list_node->num_children; ++i) {
        ASTNode* list_element = output_list_node->children[i];
        if (!list_element || list_element->num_children != 1) { // Each list element has one child (int_value, string, newline)
            fprintf(stderr, "Interpreter Error: Invalid AST_LIST_ELEMENT node structure within output list.\n");
            continue;
        }

        ASTNode* element_content = list_element->children[0];

        switch (element_content->type) {
            case AST_INT_VALUE: {
                BigInt value_to_print;
                evaluate_big_int_value(element_content, &value_to_print);
                big_int_to_string(&value_to_print, str_buffer);
                printf("%s", str_buffer);
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

    // Evaluate the initial loop count. This will be the *effective* number of times the loop runs.
    BigInt loop_count;
    evaluate_big_int_value(count_expr_node, &loop_count);

    BigInt zero;
    big_int_zero(&zero);

    // Check for negative loop count
    if (loop_count.sign == -1) {
        fprintf(stderr, "Runtime Error: Loop count cannot be negative at line %d, column %d. Skipping loop.\n",
                node->location.line, node->location.column);
        return;
    }

    // If initial count is zero, skip the loop entirely
    if (big_int_abs_compare(&loop_count, &zero) == 0) {
        printf("[DEBUG] Interpreting loop statement (count: 0, skipping loop).\n");
        return;
    }

    printf("[DEBUG] Interpreting loop statement (BigInt count: ");
    big_int_print(&loop_count);
    printf(").\n");

    BigInt current_iteration;
    big_int_zero(&current_iteration);
    BigInt one;
    big_int_from_long_long(&one, 1);

    // Loop as long as current_iteration < loop_count
    while (big_int_abs_compare(&current_iteration, &loop_count) < 0) {
        if (body_node->type == AST_CODE_BLOCK) {
            interpret_code_block(body_node);
        } else {
            interpret_statement(body_node);
        }
        big_int_add(&current_iteration, &current_iteration, &one); // Increment internal counter for loop iterations
        printf("[DEBUG] Loop iteration count: "); // Debug for loop
        big_int_print(&current_iteration);
        printf(".\n");
    }
    printf("[DEBUG] Loop finished. Iterations completed: ");
    big_int_print(&current_iteration);
    printf(".\n");
}

static void interpret_code_block(ASTNode* node) {
    if (!node || node->type != AST_CODE_BLOCK || node->num_children != 1 || node->children[0]->type != AST_STATEMENT_LIST) {
        fprintf(stderr, "Interpreter Error: Invalid AST_CODE_BLOCK node structure.\n");
        return;
    }
    printf("[DEBUG] Entering code block.\n");
    interpret_statement_list(node->children[0]);
    printf("[DEBUG] Exiting code block.\n\n"); // Added newline for clarity
}


// Evaluates an <int_value> AST node to its BigInt value
static void evaluate_big_int_value(ASTNode* node, BigInt* result) {
    if (!node || node->type != AST_INT_VALUE || node->num_children != 1) {
        fprintf(stderr, "Interpreter Error: Invalid AST_INT_VALUE node structure. Expected one child.\n");
        big_int_zero(result);
        return;
    }

    ASTNode* child = node->children[0];
    if (child->type == AST_INTEGER_LITERAL) {
        // The AST_INTEGER_LITERAL node now directly holds a BigInt
        big_int_copy(result, &child->data.integer);
    } else if (child->type == AST_IDENTIFIER) {
        char* var_name = child->data.identifier.name;
        if (!lookup_runtime_symbol(&global_runtime_sym_table, var_name, result)) {
            fprintf(stderr, "Runtime Error: Undeclared variable '%s' used in expression at line %d, column %d.\n",
                    var_name, node->location.line, node->location.column);
            big_int_zero(result); // Return 0 for undeclared variable
        }
    } else {
        fprintf(stderr, "Interpreter Error: Invalid child type for AST_INT_VALUE: %d\n", child->type);
        big_int_zero(result);
    }
}
