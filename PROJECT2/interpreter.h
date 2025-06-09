#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "parser.h" // To access ASTNode structures and types
#include "bigint.h"
#include <stdbool.h> // For bool
#include <stdio.h>   // For FILE, printf

// --- Runtime Symbol Table (Environment) Declarations ---
typedef struct {
    char* name;
    BigInt value; // Change to BigInt by value
} RuntimeSymbolEntry;

typedef struct {
    RuntimeSymbolEntry* entries;
    int count;
    int capacity;
} RuntimeSymbolTable;

// Function declarations for managing the runtime symbol table
void init_runtime_symbol_table(RuntimeSymbolTable* table);
// Change value to const BigInt*
void add_or_update_runtime_symbol(RuntimeSymbolTable* table, const char* name, const BigInt* value);
// Change out_value to BigInt*
bool lookup_runtime_symbol(RuntimeSymbolTable* table, const char* name, BigInt* out_value);
void free_runtime_symbol_table(RuntimeSymbolTable* table);

// --- Main Interpreter Function Declaration ---
void interpret_program(ASTNode* root_node);

// BigInt specific functions (some moved/renamed/added)
void big_int_zero(BigInt *num);
int big_int_abs_compare(const BigInt *a, const BigInt *b); // 0: a==b, 1: a>b, -1: a<b
void big_int_abs_add(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_abs_sub(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_normalize(BigInt *num);
void big_int_add(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_sub(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_copy(BigInt *dest, const BigInt *src);
void big_int_from_long_long(BigInt *num, long long val); // New: Convert long long to BigInt
bool big_int_to_long_long(const BigInt *num, long long* out_val); // New: Convert BigInt to long long, with overflow check
void big_int_from_string(BigInt *num, const char *str); // Already declared, now implemented
void big_int_to_string(const BigInt *num, char *str_buffer); // Already declared, now implemented
void big_int_print(const BigInt *num); // Already declared, likely useful for debugging

#endif // INTERPRETER_H
