#include "bigint.h"
#include <limits.h> // For LLONG_MAX
#include <stdio.h>
#include <string.h>

// Helper to initialize a BigInt to zero
void big_int_zero(BigInt *num) {
    memset(num->limbs, 0, sizeof(num->limbs));
    num->sign = 1;
}

// Compares absolute values: returns 0 if |a|==|b|, 1 if |a|>|b|, -1 if |a|<|b|
int big_int_abs_compare(const BigInt *a, const BigInt *b) {
    for (int i = NUM_LIMBS - 1; i >= 0; --i) {
        if (a->limbs[i] > b->limbs[i]) return 1;
        if (a->limbs[i] < b->limbs[i]) return -1;
    }
    return 0; // Absolute values are equal
}

// Performs result = |a| + |b| using 128-bit integers to handle carry safely.
void big_int_abs_add(BigInt *result, const BigInt *a, const BigInt *b) {
    unsigned long long carry = 0;
    for (int i = 0; i < NUM_LIMBS; ++i) {
        unsigned __int128 sum = (unsigned __int128)a->limbs[i] + b->limbs[i] + carry;
        result->limbs[i] = (unsigned long long)sum;
        carry = sum >> 64;
    }
}

// Performs result = |a| - |b|, assumes |a| >= |b|.
void big_int_abs_sub(BigInt *result, const BigInt *a, const BigInt *b) {
    long long borrow = 0;
    for (int i = 0; i < NUM_LIMBS; ++i) {
        // Use 128-bit arithmetic to safely handle borrow
        signed __int128 diff = (signed __int128)a->limbs[i] - b->limbs[i] - borrow;
        if (diff < 0) {
            // Correctly add 2^64 using a 128-bit literal to avoid the warning
            diff += ((unsigned __int128)1 << 64);
            borrow = 1;
        } else {
            borrow = 0;
        }
        result->limbs[i] = (unsigned long long)diff;
    }
}

// Normalizes the BigInt
void big_int_normalize(BigInt *num) {
    bool is_zero = true;
    for (int i = 0; i < NUM_LIMBS; ++i) {
        if (num->limbs[i] != 0) {
            is_zero = false;
            break;
        }
    }
    if (is_zero) {
        num->sign = 1; // Zero is always positive
    }
}

// Signed addition: result = a + b
void big_int_add(BigInt *result, const BigInt *a, const BigInt *b) {
    if (a->sign == b->sign) {
        big_int_abs_add(result, a, b);
        result->sign = a->sign;
    } else {
        int cmp = big_int_abs_compare(a, b);
        if (cmp >= 0) {
            big_int_abs_sub(result, a, b);
            result->sign = a->sign;
        } else {
            big_int_abs_sub(result, b, a);
            result->sign = b->sign;
        }
    }
    big_int_normalize(result);
}

// Signed subtraction: result = a - b
void big_int_sub(BigInt *result, const BigInt *a, const BigInt *b) {
    BigInt b_negated = *b;
    b_negated.sign = -b_negated.sign;
    big_int_add(result, a, &b_negated);
}

// Function to copy one BigInt to another
void big_int_copy(BigInt *dest, const BigInt *src) {
    memcpy(dest->limbs, src->limbs, sizeof(src->limbs));
    dest->sign = src->sign;
}

// Convert a long long to BigInt
void big_int_from_long_long(BigInt *num, long long val) {
    big_int_zero(num);
    if (val < 0) {
        num->sign = -1;
        val = -val;
    } else {
        num->sign = 1;
    }
    num->limbs[0] = (unsigned long long)val;
}

// Convert BigInt to long long (with overflow check)
bool big_int_to_long_long(const BigInt *num, long long* out_val) {
    for (int i = 1; i < NUM_LIMBS; ++i) {
        if (num->limbs[i] != 0) {
            fprintf(stderr, "Warning: BigInt value too large to fit in long long.\n");
            return false;
        }
    }

    if (num->sign == 1) {
        if (num->limbs[0] > LLONG_MAX) {
            fprintf(stderr, "Warning: Positive BigInt value overflows long long max.\n");
            return false;
        }
        *out_val = (long long)num->limbs[0];
    } else {
        unsigned long long abs_val = num->limbs[0];
        if (abs_val > (unsigned long long)LLONG_MAX + 1) {
            fprintf(stderr, "Warning: Negative BigInt value underflows long long min.\n");
            return false;
        }
        *out_val = -(long long)abs_val;
    }
    return true;
}

// Convert a string representation of a number to BigInt
void big_int_from_string(BigInt *num, const char *str) {
    big_int_zero(num);
    int final_sign = 1;
    int start_idx = 0;
    if (str[0] == '-') {
        final_sign = -1;
        start_idx = 1;
    } else if (str[0] == '+') {
        start_idx = 1;
    }

    BigInt temp_result;
    BigInt current_digit_bi;

    for (int i = start_idx; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') {
            fprintf(stderr, "Error: Invalid character '%c' in number string '%s'.\n", str[i], str);
            big_int_zero(num);
            return;
        }

        unsigned long long carry = 0;
        for (int k = 0; k < NUM_LIMBS; ++k) {
            unsigned __int128 product = (unsigned __int128)num->limbs[k] * 10 + carry;
            temp_result.limbs[k] = (unsigned long long)product;
            carry = (unsigned long long)(product >> 64);
        }
        temp_result.sign = 1;

        int digit = str[i] - '0';
        big_int_from_long_long(&current_digit_bi, digit);

        big_int_abs_add(num, &temp_result, &current_digit_bi);
    }

    num->sign = final_sign;
    big_int_normalize(num);
}

// Convert BigInt to a string representation
void big_int_to_string(const BigInt *num, char *str_buffer) {
    if (num == NULL || str_buffer == NULL) {
        if (str_buffer) strcpy(str_buffer, "");
        return;
    }

    BigInt temp_num;
    big_int_copy(&temp_num, num);

    bool is_zero = true;
    for (int i = 0; i < NUM_LIMBS; i++) {
        if (temp_num.limbs[i] != 0) {
            is_zero = false;
            break;
        }
    }
    if (is_zero) {
        strcpy(str_buffer, "0");
        return;
    }

    temp_num.sign = 1;

    char buffer[MAX_BIGINT_STRING_LEN + 1];
    int buffer_idx = 0;

    do {
        if (buffer_idx >= MAX_BIGINT_STRING_LEN) break;

        unsigned long long remainder = 0;
        for (int i = NUM_LIMBS - 1; i >= 0; --i) {
            unsigned __int128 current_val = ((unsigned __int128)remainder << 64) | temp_num.limbs[i];
            temp_num.limbs[i] = (unsigned long long)(current_val / 10);
            remainder = (unsigned long long)(current_val % 10);
        }
        buffer[buffer_idx++] = (char)(remainder + '0');

        is_zero = true;
        for (int i = 0; i < NUM_LIMBS; ++i) {
            if (temp_num.limbs[i] != 0) {
                is_zero = false;
                break;
            }
        }
        if (is_zero) break;
    } while (true);

    if (num->sign == -1) {
        buffer[buffer_idx++] = '-';
    }

    buffer[buffer_idx] = '\0';
    int len = buffer_idx;
    for (int i = 0; i < len / 2; ++i) {
        char temp = buffer[i];
        buffer[i] = buffer[len - 1 - i];
        buffer[len - 1 - i] = temp;
    }
    strcpy(str_buffer, buffer);
}

// Print BigInt (for debugging)
void big_int_print(const BigInt *num) {
    char str_buffer[MAX_BIGINT_STRING_LEN + 1];
    big_int_to_string(num, str_buffer);
    printf("%s", str_buffer);
}
