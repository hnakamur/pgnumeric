/*-------------------------------------------------------------------------
 *
 * numeric.h
 *    Definitions for the exact numeric data type of Postgres
 *
 * Original coding 1998, Jan Wieck.  Heavily revised 2003, Tom Lane.
 *
 * Copyright (c) 1998-2010, PostgreSQL Global Development Group
 *
 * $PostgreSQL: pgsql/src/include/utils/numeric.h,v 1.29 2010/01/02 16:58:10 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef _PG_NUMERIC_H_
#define _PG_NUMERIC_H_

#include <stdint.h>
#include "bool.h"

/*
 * Hardcoded precision limit - arbitrary, but must be small enough that
 * dscale values will fit in 14 bits.
 */
#define NUMERIC_MAX_PRECISION       1000

/*
 * Internal limits on the scales chosen for calculation results
 */
#define NUMERIC_MAX_DISPLAY_SCALE   NUMERIC_MAX_PRECISION
#define NUMERIC_MIN_DISPLAY_SCALE   0

#define NUMERIC_MAX_RESULT_SCALE    (NUMERIC_MAX_PRECISION * 2)

/*
 * For inherently inexact calculations such as division and square root,
 * we try to get at least this many significant digits; the idea is to
 * deliver a result no worse than float8 would.
 */
#define NUMERIC_MIN_SIG_DIGITS      16

/* ----------
 * numeric data types
 *
 * numeric *values are represented in a base-NBASE floating point format.
 * Each "digit" ranges from 0 to NBASE-1.  The type NumericDigit is signed
 * and wide enough to store a digit.  We assume that NBASE*NBASE can fit in
 * an int.  Although the purely calculational routines could handle any even
 * NBASE that's less than sqrt(INT_MAX), in practice we are only interested
 * in NBASE a power of ten, so that I/O conversions and decimal rounding
 * are easy.  Also, it's actually more efficient if NBASE is rather less than
 * sqrt(INT_MAX), so that there is "headroom" for mul_var and div_var_fast to
 * postpone processing carries.
 * ----------
 */

#if 0
#define NBASE       10
#define HALF_NBASE  5
#define DEC_DIGITS  1           /* decimal digits per NBASE digit */
#define MUL_GUARD_DIGITS    4   /* these are measured in NBASE digits */
#define DIV_GUARD_DIGITS    8

typedef signed char NumericDigit;
#endif

#if 0
#define NBASE       100
#define HALF_NBASE  50
#define DEC_DIGITS  2           /* decimal digits per NBASE digit */
#define MUL_GUARD_DIGITS    3   /* these are measured in NBASE digits */
#define DIV_GUARD_DIGITS    6

typedef signed char NumericDigit;
#endif

#if 1
#define NBASE       10000
#define HALF_NBASE  5000
#define DEC_DIGITS  4           /* decimal digits per NBASE digit */
#define MUL_GUARD_DIGITS    2   /* these are measured in NBASE digits */
#define DIV_GUARD_DIGITS    4

typedef int16_t NumericDigit;
#endif

/* ----------
 * numeric is the format we use for arithmetic.  The digit-array part
 * is the same as the NumericData storage format, but the header is more
 * complex.
 *
 * The value represented by a numeric is determined by the sign, weight,
 * ndigits, and digits[] array.
 * Note: the first digit of a numeric's value is assumed to be multiplied
 * by NBASE ** weight.  Another way to say it is that there are weight+1
 * digits before the decimal point.  It is possible to have weight < 0.
 *
 * buf points at the physical start of the malloc'd digit buffer for the
 * numeric.  digits points at the first digit in actual use (the one
 * with the specified weight).  We normally leave an unused digit or two
 * (preset to zeroes) between buf and digits, so that there is room to store
 * a carry out of the top digit without reallocating space.  We just need to
 * decrement digits (and increment weight) to make room for the carry digit.
 * (There is no such extra space in a numeric value stored in the database,
 * only in a numeric in memory.)
 *
 * If buf is NULL then the digit buffer isn't actually malloc'd and should
 * not be freed --- see the constants below for an example.
 *
 * dscale, or display scale, is the nominal precision expressed as number
 * of digits after the decimal point (it must always be >= 0 at present).
 * dscale may be more than the number of physically stored fractional digits,
 * implying that we have suppressed storage of significant trailing zeroes.
 * It should never be less than the number of stored digits, since that would
 * imply hiding digits that are present.  NOTE that dscale is always expressed
 * in *decimal* digits, and so it may correspond to a fractional number of
 * base-NBASE digits --- divide by DEC_DIGITS to convert to NBASE digits.
 *
 * rscale, or result scale, is the target precision for a computation.
 * Like dscale it is expressed as number of *decimal* digits after the decimal
 * point, and is always >= 0 at present.
 * Note that rscale is not stored in variables --- it's figured on-the-fly
 * from the dscales of the inputs.
 *
 * NB: All the variable-level functions are written in a style that makes it
 * possible to give one and the same variable as argument and destination.
 * This is feasible because the digit buffer is separate from the variable.
 * ----------
 */
typedef struct numeric
{
    int         ndigits;        /* # of digits in digits[] - can be 0! */
    int         weight;         /* weight of first digit */
    int         sign;           /* NUMERIC_POS, NUMERIC_NEG, or NUMERIC_NAN */
    int         dscale;         /* display scale */
    NumericDigit *buf;          /* start of malloc'd space for digits[] */
    NumericDigit *digits;       /* base-NBASE digits */
} numeric;


/*
 * Sign values and macros to deal with packing/unpacking n_sign_dscale
 */
#define NUMERIC_POS         0x0000
#define NUMERIC_NEG         0x4000
#define NUMERIC_NAN         0xC000
#define NUMERIC_SIGN(n)     ((n)->sign)
#define NUMERIC_DSCALE(n)   ((n)->dscale)
#define NUMERIC_IS_NAN(n)   (NUMERIC_SIGN(n) != NUMERIC_POS &&  \
                             NUMERIC_SIGN(n) != NUMERIC_NEG)
#define NUMERIC_IS_ZERO(n)  ((n)->ndigits == 0)


typedef enum {
    NUMERIC_ERRCODE_NO_ERROR,
    NUMERIC_ERRCODE_DIVISION_BY_ZERO,
    NUMERIC_ERRCODE_INVALID_ARGUMENT,
    NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE,
    NUMERIC_ERRCODE_OUT_OF_MEMORY
} numeric_errcode_t;

void numeric_init(numeric *var);
void numeric_dispose(numeric *var);

numeric_errcode_t numeric_from_str(const char *str, int precision,
        int scale, numeric *result);
numeric_errcode_t numeric_to_str(const numeric *num, int scale,
        char **result);
numeric_errcode_t numeric_to_str_sci(const numeric *num, int scale,
        char **result);

numeric_errcode_t numeric_from_int32(int32_t val, numeric *result);
numeric_errcode_t numeric_to_int32(const numeric *num, int32_t *result);
numeric_errcode_t numeric_from_int64(int64_t val, numeric *result);
numeric_errcode_t numeric_to_int64(const numeric *num, int64_t *result);
numeric_errcode_t numeric_from_double(double val, numeric *result);
numeric_errcode_t numeric_to_double(const numeric *num, double *result);
numeric_errcode_t numeric_from_float(float val, numeric *result);
numeric_errcode_t numeric_to_float(const numeric *num, float *result);

numeric_errcode_t numeric_abs(const numeric *num, numeric *result);
numeric_errcode_t numeric_minus(const numeric *num, numeric *result);
numeric_errcode_t numeric_plus(const numeric *num, numeric *result);
numeric_errcode_t numeric_sign(const numeric *num, numeric *result);

numeric_errcode_t numeric_round(const numeric *num, int scale, numeric *result);
numeric_errcode_t numeric_trunc(const numeric *num, int scale, numeric *result);

numeric_errcode_t numeric_ceil(const numeric *num, numeric *result);
numeric_errcode_t numeric_floor(const numeric *num, numeric *result);

int numeric_cmp(const numeric *num1, const numeric *num2);
bool numeric_eq(const numeric *num1, const numeric *num2);
bool numeric_ne(const numeric *num1, const numeric *num2);
bool numeric_gt(const numeric *num1, const numeric *num2);
bool numeric_ge(const numeric *num1, const numeric *num2);
bool numeric_lt(const numeric *num1, const numeric *num2);
bool numeric_le(const numeric *num1, const numeric *num2);

numeric_errcode_t numeric_add(const numeric *num1, const numeric *num2,
        numeric *result);
numeric_errcode_t numeric_sub(const numeric *num1, const numeric *num2,
        numeric *result);
numeric_errcode_t numeric_mul(const numeric *num1, const numeric *num2,
        numeric *result);
numeric_errcode_t numeric_div(const numeric *num1, const numeric *num2,
        numeric *result);
numeric_errcode_t numeric_div_trunc(const numeric *num1,
        const numeric *num2, numeric *result);
numeric_errcode_t numeric_mod(const numeric *num1, const numeric *num2,
        numeric *result);
numeric_errcode_t numeric_min(const numeric *num1, const numeric *num2,
        numeric *result);
numeric_errcode_t numeric_max(const numeric *num1, const numeric *num2,
        numeric *result);

numeric_errcode_t numeric_sqrt(const numeric *num, numeric *result);
numeric_errcode_t numeric_exp(const numeric *num, numeric *result);
numeric_errcode_t numeric_ln(const numeric *num, numeric *result);
numeric_errcode_t numeric_log10(const numeric *num, numeric *result);
numeric_errcode_t numeric_power(const numeric *num1, const numeric *num2,
        numeric *result);

#endif   /* _PG_NUMERIC_H_ */
