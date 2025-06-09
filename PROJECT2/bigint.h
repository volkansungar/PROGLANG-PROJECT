//
// Created by Volkan on 9.06.2025.
//

#ifndef BIGINT_H
#define BIGINT_H
#include <stdbool.h>
// For 100 digits: log10(2^64) approx 19.26. So 6 limbs * 19 digits/limb = approx 114 digits.
// This is sufficient for 100 digits.
#define NUM_LIMBS 6
// Max string length for BigInt, roughly 100 digits + sign + null terminator
#define MAX_BIGINT_STRING_LEN 102

typedef struct {
    unsigned long long limbs[NUM_LIMBS];
    int sign; // 1 for positive, -1 for negative
} BigInt;
// Function prototypes
void big_int_add(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_sub(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_abs_add(BigInt *result, const BigInt *a, const BigInt *b);
void big_int_abs_sub(BigInt *result, const BigInt *a, const BigInt *b);
int big_int_abs_compare(const BigInt *a, const BigInt *b); // 0: a==b, 1: a>b, -1: a<b
void big_int_normalize(BigInt *num);
void big_int_copy(BigInt *dest, const BigInt *src);
void big_int_from_long_long(BigInt *num, long long val); // New: Convert long long to BigInt
bool big_int_to_long_long(const BigInt *num, long long* out_val); // New: Convert BigInt to long long, with overflow check
void big_int_from_string(BigInt *num, const char *str); // Already declared, now implemented
void big_int_to_string(const BigInt *num, char *str_buffer); // Already declared, now implemented
void big_int_print(const BigInt *num); // Already declared, likely useful for debugging
#endif //BIGINT_H
