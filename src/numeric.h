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


/*
 * Sign values and macros to deal with packing/unpacking n_sign_dscale
 */
#define NUMERIC_SIGN_MASK   0xC000
#define NUMERIC_POS         0x0000
#define NUMERIC_NEG         0x4000
#define NUMERIC_NAN         0xC000
#define NUMERIC_DSCALE_MASK 0x3FFF
#define NUMERIC_SIGN(n)     ((n)->n_sign_dscale & NUMERIC_SIGN_MASK)
#define NUMERIC_DSCALE(n)   ((n)->n_sign_dscale & NUMERIC_DSCALE_MASK)
#define NUMERIC_IS_NAN(n)   (NUMERIC_SIGN(n) != NUMERIC_POS &&  \
                             NUMERIC_SIGN(n) != NUMERIC_NEG)


/*
 * The Numeric data type stored in the database
 *
 * NOTE: by convention, values in the packed form have been stripped of
 * all leading and trailing zero digits (where a "digit" is of base NBASE).
 * In particular, if the value is zero, there will be no digits at all!
 * The weight is arbitrary in that case, but we normally set it to zero.
 */
typedef struct NumericData
{
    int32_t     n_length;       /* byte length including this field */
    uint16_t    n_sign_dscale;  /* Sign + display scale */
    int16_t     n_weight;       /* Weight of 1st digit  */
    char        n_data[1];      /* Digits (really array of NumericDigit) */
} NumericData;

typedef NumericData *Numeric;

#define NUMERIC_HDRSZ   (sizeof(int32_t) + sizeof(uint16_t) + sizeof(int16_t))



typedef enum {
    NUMERIC_ERRCODE_NO_ERROR,
    NUMERIC_ERRCODE_DIVISION_BY_ZERO,
    NUMERIC_ERRCODE_INVALID_ARGUMENT,
    NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE
} numeric_errcode_t;

extern numeric_errcode_t numeric_from_str(const char *str, int precision,
        int scale, Numeric *result);
extern numeric_errcode_t numeric_to_str(const Numeric num, int scale,
        char **result);
extern numeric_errcode_t numeric_to_str_sci(const Numeric num, int scale,
        char **result);

extern numeric_errcode_t numeric_from_int32(int32_t val, Numeric *result);
extern numeric_errcode_t numeric_to_int32(const Numeric num, int32_t *result);
extern numeric_errcode_t numeric_from_int64(int64_t val, Numeric *result);
extern numeric_errcode_t numeric_to_int64(const Numeric num, int64_t *result);
extern numeric_errcode_t numeric_from_double(double val, Numeric *result);
extern numeric_errcode_t numeric_to_double(const Numeric num, double *result);
extern numeric_errcode_t numeric_from_float(float val, Numeric *result);
extern numeric_errcode_t numeric_to_float(const Numeric num, float *result);

extern numeric_errcode_t numeric_abs(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_minus(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_plus(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_sign(const Numeric num, Numeric *result);

extern numeric_errcode_t numeric_round(const Numeric num, int scale,
        Numeric *result);
extern numeric_errcode_t numeric_trunc(const Numeric num, int scale,
        Numeric *result);

extern numeric_errcode_t numeric_ceil(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_floor(const Numeric num, Numeric *result);

extern int numeric_cmp(const Numeric num1, const Numeric num2);
extern bool numeric_eq(const Numeric num1, const Numeric num2);
extern bool numeric_ne(const Numeric num1, const Numeric num2);
extern bool numeric_gt(const Numeric num1, const Numeric num2);
extern bool numeric_ge(const Numeric num1, const Numeric num2);
extern bool numeric_lt(const Numeric num1, const Numeric num2);
extern bool numeric_le(const Numeric num1, const Numeric num2);

extern numeric_errcode_t numeric_add(const Numeric num1, const Numeric num2,
        Numeric *result);
extern numeric_errcode_t numeric_sub(const Numeric num1, const Numeric num2,
        Numeric *result);
extern numeric_errcode_t numeric_mul(const Numeric num1, const Numeric num2,
        Numeric *result);
extern numeric_errcode_t numeric_div(const Numeric num1, const Numeric num2,
        Numeric *result);
extern numeric_errcode_t numeric_div_trunc(const Numeric num1,
        const Numeric num2, Numeric *result);
extern numeric_errcode_t numeric_mod(const Numeric num1, const Numeric num2,
        Numeric *result);
extern numeric_errcode_t numeric_min(const Numeric num1, const Numeric num2,
        Numeric *result);
extern numeric_errcode_t numeric_max(const Numeric num1, const Numeric num2,
        Numeric *result);

extern numeric_errcode_t numeric_sqrt(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_exp(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_ln(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_log10(const Numeric num, Numeric *result);
extern numeric_errcode_t numeric_power(const Numeric num1, const Numeric num2,
        Numeric *result);

#endif   /* _PG_NUMERIC_H_ */
