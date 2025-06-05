#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "parser.h" // To access ASTNode structures and types
#include <stdbool.h> // For bool
#include <stdio.h>   // For FILE, printf

// --- Runtime Symbol Table (Environment) Declarations ---
typedef struct {
    char* name;
    long long value;
} RuntimeSymbolEntry;

typedef struct {
    RuntimeSymbolEntry* entries;
    int count;
    int capacity;
} RuntimeSymbolTable;

// Function declarations for managing the runtime symbol table
void init_runtime_symbol_table(RuntimeSymbolTable* table);
void add_or_update_runtime_symbol(RuntimeSymbolTable* table, const char* name, long long value);
bool lookup_runtime_symbol(RuntimeSymbolTable* table, const char* name, long long* out_value);
void free_runtime_symbol_table(RuntimeSymbolTable* table);

// --- Main Interpreter Function Declaration ---
void interpret_program(ASTNode* root_node);

#endif // INTERPRETER_H