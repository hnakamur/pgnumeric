/*-------------------------------------------------------------------------
 *
 * numeric.c
 *    An exact numeric data type for the Postgres database system
 *
 * Original coding 1998, Jan Wieck.  Heavily revised 2003, Tom Lane.
 * Revised 2011 for standalone use, Hiroaki Nakamura.
 *
 * Many of the algorithmic ideas are borrowed from David M. Smith's "FM"
 * multiple-precision math library, most recently published as Algorithm
 * 786: Multiple-Precision Complex Arithmetic and Functions, ACM
 * Transactions on Mathematical Software, Vol. 24, No. 4, December 1998,
 * pages 359-367.
 *
 * Copyright (c) 1998-2011, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *    $PostgreSQL: pgsql/src/backend/utils/adt/numeric.c,v 1.123 2010/02/26 02:01:09 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#include "numeric.h"

extern double get_double_nan(void);
extern double get_float_nan(void);
extern numeric_errcode_t float_in(const char *num, float *result);
extern numeric_errcode_t float_out(float num, char **result);
extern numeric_errcode_t double_in(const char *num, double *result);
extern numeric_errcode_t double_out(float num, char **result);

extern int pg_strncasecmp(const char *s1, const char *s2, size_t n);

#define Assert(condition)

/*
 * Max
 *      Return the maximum of two numbers.
 */
#define Max(x, y)       ((x) > (y) ? (x) : (y))

/*
 * Min
 *      Return the minimum of two numbers.
 */
#define Min(x, y)       ((x) < (y) ? (x) : (y))

/*
 * Abs
 *      Return the absolute value of the argument.
 */
#define Abs(x)          ((x) >= 0 ? (x) : -(x))


/* ----------
 * Uncomment the following to enable compilation of dump_var()
 * and to get a dump of any result produced by make_result().
 * ----------
#define NUMERIC_DEBUG
 */



/* ----------
 * Some preinitialized constants
 * ----------
 */
static NumericDigit const_zero_data[1] = {0};
static numeric const_zero =
{0, 0, NUMERIC_POS, 0, NULL, const_zero_data};

static NumericDigit const_one_data[1] = {1};
static numeric const_one =
{1, 0, NUMERIC_POS, 0, NULL, const_one_data};

static NumericDigit const_two_data[1] = {2};
static numeric const_two =
{1, 0, NUMERIC_POS, 0, NULL, const_two_data};

#if DEC_DIGITS == 4 || DEC_DIGITS == 2
static NumericDigit const_ten_data[1] = {10};
static numeric const_ten =
{1, 0, NUMERIC_POS, 0, NULL, const_ten_data};
#elif DEC_DIGITS == 1
static NumericDigit const_ten_data[1] = {1};
static numeric const_ten =
{1, 1, NUMERIC_POS, 0, NULL, const_ten_data};
#endif

#if DEC_DIGITS == 4
static NumericDigit const_zero_point_five_data[1] = {5000};
#elif DEC_DIGITS == 2
static NumericDigit const_zero_point_five_data[1] = {50};
#elif DEC_DIGITS == 1
static NumericDigit const_zero_point_five_data[1] = {5};
#endif
static numeric const_zero_point_five =
{1, -1, NUMERIC_POS, 1, NULL, const_zero_point_five_data};

#if DEC_DIGITS == 4
static NumericDigit const_zero_point_nine_data[1] = {9000};
#elif DEC_DIGITS == 2
static NumericDigit const_zero_point_nine_data[1] = {90};
#elif DEC_DIGITS == 1
static NumericDigit const_zero_point_nine_data[1] = {9};
#endif
static numeric const_zero_point_nine =
{1, -1, NUMERIC_POS, 1, NULL, const_zero_point_nine_data};

#if DEC_DIGITS == 4
static NumericDigit const_zero_point_01_data[1] = {100};
static numeric const_zero_point_01 =
{1, -1, NUMERIC_POS, 2, NULL, const_zero_point_01_data};
#elif DEC_DIGITS == 2
static NumericDigit const_zero_point_01_data[1] = {1};
static numeric const_zero_point_01 =
{1, -1, NUMERIC_POS, 2, NULL, const_zero_point_01_data};
#elif DEC_DIGITS == 1
static NumericDigit const_zero_point_01_data[1] = {1};
static numeric const_zero_point_01 =
{1, -2, NUMERIC_POS, 2, NULL, const_zero_point_01_data};
#endif

#if DEC_DIGITS == 4
static NumericDigit const_one_point_one_data[2] = {1, 1000};
#elif DEC_DIGITS == 2
static NumericDigit const_one_point_one_data[2] = {1, 10};
#elif DEC_DIGITS == 1
static NumericDigit const_one_point_one_data[2] = {1, 1};
#endif
static numeric const_one_point_one =
{2, 0, NUMERIC_POS, 1, NULL, const_one_point_one_data};

static numeric const_nan =
{0, 0, NUMERIC_NAN, 0, NULL, NULL};

#if DEC_DIGITS == 4
static const int round_powers[4] = {0, 1000, 100, 10};
#endif


/* ----------
 * Local functions
 * ----------
 */

#ifdef NUMERIC_DEBUG
static void dump_var(const char *str, numeric *var);
#else
#define dump_var(s,v)
#endif

#define digitbuf_alloc(ndigits)  \
    ((NumericDigit *) malloc((ndigits) * sizeof(NumericDigit)))
#define digitbuf_free(buf)  \
    do { \
         if ((buf) != NULL) \
             free(buf); \
    } while (0)


#define NUMERIC_DIGITS(num) ((NumericDigit *)(num)->digits)
#define NUMERIC_NDIGITS(num) ((num)->ndigits)

static void alloc_var(numeric *var, int ndigits);

static numeric_errcode_t numeric_out(const numeric *num, char **result);
static numeric_errcode_t set_var_from_str(const char *str, const char *cp,
                numeric *dest, const char **result);
static void copy_var(const numeric *value, numeric *dest);
static void set_var_from_var(const numeric *value, numeric *dest);
static char *get_str_from_var(numeric *var, int dscale);
static char *get_str_from_var_sci(numeric *var, int rscale);

static numeric_errcode_t make_result(const numeric *var, numeric *result);

static numeric_errcode_t check_bounds_and_round(numeric *var, int precision,
                int scale);

static numeric_errcode_t numericvar_to_int32(numeric *var, int32_t *result);
static bool numericvar_to_int64(numeric *var, int64_t *result);
static void int64_to_numericvar(int64_t val, numeric *var);
static numeric_errcode_t numericvar_to_double_no_overflow(const numeric *var,
                double *result);

static int cmp_numerics(const numeric *num1, const numeric *num2);
static int cmp_var(const numeric *var1, const numeric *var2);
static int cmp_var_common(const NumericDigit *var1digits, int var1ndigits,
                int var1weight, int var1sign,
                const NumericDigit *var2digits, int var2ndigits,
                int var2weight, int var2sign);
static void add_var(const numeric *var1, const numeric *var2, numeric *result);
static void sub_var(const numeric *var1, const numeric *var2, numeric *result);
static void mul_var(const numeric *var1, const numeric *var2, numeric *result,
                int rscale);
static numeric_errcode_t div_var(const numeric *var1, const numeric *var2,
                numeric *result, int rscale, bool round);
static numeric_errcode_t div_var_fast(numeric *var1, numeric *var2,
                numeric *result, int rscale, bool round);
static int  select_div_scale(const numeric *var1, const numeric *var2);
static numeric_errcode_t mod_var(const numeric *var1, const numeric *var2,
                numeric *result);
static void ceil_var(const numeric *var, numeric *result);
static void floor_var(const numeric *var, numeric *result);

static numeric_errcode_t sqrt_var(const numeric *arg, numeric *result,
                int rscale);
static numeric_errcode_t exp_var(const numeric *arg, numeric *result,
                int rscale);
static void exp_var_internal(const numeric *arg, numeric *result, int rscale);
static numeric_errcode_t ln_var(const numeric *arg, numeric *result,
                int rscale);
static numeric_errcode_t log_var(const numeric *base, const numeric *num,
                numeric *result);
static numeric_errcode_t power_var(const numeric *base, const numeric *exp,
                numeric *result);
static void power_var_int(const numeric *base, int exp, numeric *result,
                int rscale);

static int cmp_abs(const numeric *var1, const numeric *var2);
static int cmp_abs_common(const NumericDigit *var1digits, int var1ndigits,
                int var1weight,
                const NumericDigit *var2digits, int var2ndigits,
                int var2weight);
static void add_abs(const numeric *var1, const numeric *var2, numeric *result);
static void sub_abs(const numeric *var1, const numeric *var2, numeric *result);
static void round_var(numeric *var, int rscale);
static void trunc_var(numeric *var, int rscale);
static void strip_var(numeric *var);


/* ----------------------------------------------------------------------
 *
 * Input-, output- and rounding-functions
 *
 * ----------------------------------------------------------------------
 */

numeric_errcode_t
numeric_from_str(const char *str, int precision, int scale, numeric *result)
{
    const char *cp;
    numeric_errcode_t errcode;

    /* Skip leading spaces */
    cp = str;
    while (*cp)
    {
        if (!isspace((unsigned char) *cp))
            break;
        cp++;
    }

    /*
     * Check for NaN
     */
    if (pg_strncasecmp(cp, "NaN", 3) == 0)
    {
        errcode = make_result(&const_nan, result);
        if (errcode != NUMERIC_ERRCODE_NO_ERROR)
            return errcode;

        /* Should be nothing left but spaces */
        cp += 3;
        while (*cp)
        {
            if (!isspace((unsigned char) *cp))
                return NUMERIC_ERRCODE_INVALID_ARGUMENT;
            cp++;
        }
    }
    else
    {
        /*
         * Use set_var_from_str() to parse a normal numeric value
         */
        numeric  value;

        numeric_init(&value);

        errcode = set_var_from_str(str, cp, &value, &cp);
        if (errcode != NUMERIC_ERRCODE_NO_ERROR)
            return errcode;

        /*
         * We duplicate a few lines of code here because we would like to
         * throw any trailing-junk syntax error before any semantic error
         * resulting from check_bounds_and_round.  We can't easily fold the
         * two cases together because we mustn't apply check_bounds_and_round
         * to a NaN.
         */
        while (*cp)
        {
            if (!isspace((unsigned char) *cp))
                return NUMERIC_ERRCODE_INVALID_ARGUMENT;
            cp++;
        }

        check_bounds_and_round(&value, precision, scale);

        errcode = make_result(&value, result);
        if (errcode != NUMERIC_ERRCODE_NO_ERROR)
            return errcode;
        numeric_dispose(&value);
    }

    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * numeric_out() -
 *
 *  Convert numeric value to string
 */
static numeric_errcode_t
numeric_out(const numeric *num, char **result)
{
    numeric  x;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
    {
        *result = strdup("NaN");
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /*
     * Get the number in the variable format.
     *
     * Even if we didn't need to change format, we'd still need to copy the
     * value to have a modifiable copy for rounding.  set_var_from_var() also
     * guarantees there is extra digit space in case we produce a carry out
     * from rounding.
     */
    numeric_init(&x);
    set_var_from_var(num, &x);

    *result = get_str_from_var(&x, x.dscale);

    numeric_dispose(&x);

    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * numeric_to_str() -
 *
 *  Convert numeric value to string
 */
numeric_errcode_t
numeric_to_str(const numeric *num, int scale, char **result)
{
    numeric  x;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
    {
        *result = strdup("NaN");
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /*
     * Get the number in the variable format.
     *
     * Even if we didn't need to change format, we'd still need to copy the
     * value to have a modifiable copy for rounding.  set_var_from_var() also
     * guarantees there is extra digit space in case we produce a carry out
     * from rounding.
     */
    numeric_init(&x);
    set_var_from_var(num, &x);

    if (scale < 0)
        scale = x.dscale;
    *result = get_str_from_var(&x, scale);

    numeric_dispose(&x);

    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * numeric_to_str_sci() -
 *
 *  Convert numeric value to string in scientific notation.
 */
numeric_errcode_t
numeric_to_str_sci(const numeric *num, int scale, char **result)
{
    numeric  x;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
    {
        *result = strdup("NaN");
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    numeric_init(&x);
    set_var_from_var(num, &x);

    if (scale < 0)
        scale = x.dscale;
    *result = get_str_from_var_sci(&x, scale);

    numeric_dispose(&x);
    return NUMERIC_ERRCODE_NO_ERROR;
}

/* ----------------------------------------------------------------------
 *
 * Sign manipulation, rounding and the like
 *
 * ----------------------------------------------------------------------
 */

numeric_errcode_t
numeric_abs(const numeric *num, numeric *result)
{
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    errcode = make_result(num, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;
    result->sign = NUMERIC_POS;

    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_minus(const numeric *num, numeric *result)
{
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    errcode = make_result(num, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    if (!NUMERIC_IS_ZERO(num))
    {
        /* Else, flip the sign */
        if (NUMERIC_SIGN(num) == NUMERIC_POS)
            result->sign = NUMERIC_NEG;
        else
            result->sign = NUMERIC_POS;
    }

    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_plus(const numeric *num, numeric *result)
{
    return make_result(num, result);
}

/*
 * numeric_sign() -
 *
 * returns -1 if the argument is less than 0, 0 if the argument is equal
 * to 0, and 1 if the argument is greater than zero.
 */
numeric_errcode_t
numeric_sign(const numeric *num, numeric *result)
{
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    if (NUMERIC_IS_ZERO(num))
        return make_result(&const_zero, result);
    else
    {
        /*
         * And if there are some, we return a copy of ONE with the sign of our
         * argument
         */
        errcode = make_result(&const_one, result);
        if (errcode != NUMERIC_ERRCODE_NO_ERROR)
            return errcode;
        result->sign = NUMERIC_SIGN(num);
    }

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_round() -
 *
 *  Round a value to have 'scale' digits after the decimal point.
 *  We allow negative 'scale', implying rounding before the decimal
 *  point --- Oracle interprets rounding that way.
 */
numeric_errcode_t
numeric_round(const numeric *num, int scale, numeric *result)
{
    numeric  arg;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    /*
     * Limit the scale value to avoid possible overflow in calculations
     */
    scale = Max(scale, -NUMERIC_MAX_RESULT_SCALE);
    scale = Min(scale, NUMERIC_MAX_RESULT_SCALE);

    /*
     * Unpack the argument and round it at the proper digit position
     */
    numeric_init(&arg);
    set_var_from_var(num, &arg);

    round_var(&arg, scale);

    /* We don't allow negative output dscale */
    if (scale < 0)
        arg.dscale = 0;

    /*
     * Return the rounded result
     */
    errcode = make_result(&arg, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&arg);
    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_trunc() -
 *
 *  Truncate a value to have 'scale' digits after the decimal point.
 *  We allow negative 'scale', implying a truncation before the decimal
 *  point --- Oracle interprets truncation that way.
 */
numeric_errcode_t
numeric_trunc(const numeric *num, int scale, numeric *result)
{
    numeric  arg;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    /*
     * Limit the scale value to avoid possible overflow in calculations
     */
    scale = Max(scale, -NUMERIC_MAX_RESULT_SCALE);
    scale = Min(scale, NUMERIC_MAX_RESULT_SCALE);

    /*
     * Unpack the argument and truncate it at the proper digit position
     */
    numeric_init(&arg);
    set_var_from_var(num, &arg);

    trunc_var(&arg, scale);

    /* We don't allow negative output dscale */
    if (scale < 0)
        arg.dscale = 0;

    /*
     * Return the truncated result
     */
    errcode = make_result(&arg, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&arg);
    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_ceil() -
 *
 *  Return the smallest integer greater than or equal to the argument
 */
numeric_errcode_t
numeric_ceil(const numeric *num, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    numeric_init(&result_var);

    set_var_from_var(num, &result_var);
    ceil_var(&result_var, &result_var);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;
    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_floor() -
 *
 *  Return the largest integer equal to or less than the argument
 */
numeric_errcode_t
numeric_floor(const numeric *num, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    numeric_init(&result_var);

    set_var_from_var(num, &result_var);
    floor_var(&result_var, &result_var);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;
    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/* ----------------------------------------------------------------------
 *
 * Comparison functions
 *
 * Note: btree indexes need these routines not to leak memory; therefore,
 * be careful to free working copies of toasted datums.  Most places don't
 * need to be so careful.
 * ----------------------------------------------------------------------
 */


int
numeric_cmp(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2);
}

bool
numeric_eq(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2) == 0;
}

bool
numeric_ne(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2) != 0;
}

bool
numeric_gt(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2) > 0;
}

bool
numeric_ge(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2) >= 0;
}

bool
numeric_lt(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2) < 0;
}

bool
numeric_le(const numeric *num1, const numeric *num2)
{
    return cmp_numerics(num1, num2) <= 0;
}

static int
cmp_numerics(const numeric *num1, const numeric *num2)
{
    int         result;

    /*
     * We consider all NANs to be equal and larger than any non-NAN. This is
     * somewhat arbitrary; the important thing is to have a consistent sort
     * order.
     */
    if (NUMERIC_IS_NAN(num1))
    {
        if (NUMERIC_IS_NAN(num2))
            result = 0;         /* NAN = NAN */
        else
            result = 1;         /* NAN > non-NAN */
    }
    else if (NUMERIC_IS_NAN(num2))
    {
        result = -1;            /* non-NAN < NAN */
    }
    else
    {
        result = cmp_var_common(NUMERIC_DIGITS(num1), NUMERIC_NDIGITS(num1),
                                num1->weight, NUMERIC_SIGN(num1),
                                NUMERIC_DIGITS(num2), NUMERIC_NDIGITS(num2),
                                num2->weight, NUMERIC_SIGN(num2));
    }

    return result;
}


/* ----------------------------------------------------------------------
 *
 * Basic arithmetic functions
 *
 * ----------------------------------------------------------------------
 */


/*
 * numeric_add() -
 *
 *  Add two numerics
 */
numeric_errcode_t
numeric_add(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    /*
     * Unpack the values, let add_var() compute the result_var and return it.
     */
    numeric_init(&result_var);

    add_var(num1, num2, &result_var);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_sub() -
 *
 *  Subtract one numeric from another
 */
numeric_errcode_t
numeric_sub(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    /*
     * Unpack the values, let sub_var() compute the result_var and return it.
     */
    numeric_init(&result_var);

    sub_var(num1, num2, &result_var);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_mul() -
 *
 *  Calculate the product of two numerics
 */
numeric_errcode_t
numeric_mul(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    /*
     * Unpack the values, let mul_var() compute the result_var and return it.
     * Unlike add_var() and sub_var(), mul_var() will round its result_var. In the
     * case of numeric_mul(), which is invoked for the * operator on numerics,
     * we request exact representation for the product (rscale = sum(dscale of
     * arg1, dscale of arg2)).
     */
    numeric_init(&result_var);

    mul_var(num1, num2, &result_var, num1->dscale + num2->dscale);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_div() -
 *
 *  Divide one numeric into another
 */
numeric_errcode_t
numeric_div(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  result_var;
    int         rscale;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    /*
     * Unpack the arguments
     */
    numeric_init(&result_var);

    /*
     * Select scale for division result_var
     */
    rscale = select_div_scale(num1, num2);

    /*
     * Do the divide and return the result_var
     */
    errcode = div_var(num1, num2, &result_var, rscale, true);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_div_trunc() -
 *
 *  Divide one numeric into another, truncating the result to an integer
 */
numeric_errcode_t
numeric_div_trunc(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    /*
     * Unpack the arguments
     */
    numeric_init(&result_var);

    /*
     * Do the divide and return the result_var
     */
    errcode = div_var(num1, num2, &result_var, 0, false);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_mod() -
 *
 *  Calculate the modulo of two numerics
 */
numeric_errcode_t
numeric_mod(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    numeric_init(&result_var);

    errcode = mod_var(num1, num2, &result_var);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}



/*
 * numeric_min() -
 *
 *  Return the smaller of two numbers
 */
numeric_errcode_t
numeric_min(const numeric *num1, const numeric *num2, numeric *result)
{
    /*
     * Use cmp_numerics so that this will agree with the comparison operators,
     * particularly as regards comparisons involving NaN.
     */
    if (cmp_numerics(num1, num2) < 0)
        return make_result(num1, result);
    else
        return make_result(num2, result);
}


/*
 * numeric_max() -
 *
 *  Return the larger of two numbers
 */
numeric_errcode_t
numeric_max(const numeric *num1, const numeric *num2, numeric *result)
{
    /*
     * Use cmp_numerics so that this will agree with the comparison operators,
     * particularly as regards comparisons involving NaN.
     */
    if (cmp_numerics(num1, num2) > 0)
        return make_result(num1, result);
    else
        return make_result(num2, result);
}


/* ----------------------------------------------------------------------
 *
 * Advanced math functions
 *
 * ----------------------------------------------------------------------
 */


/*
 * numeric_sqrt() -
 *
 *  Compute the square root of a numeric.
 */
numeric_errcode_t
numeric_sqrt(const numeric *num, numeric *result)
{
    numeric  result_var;
    int         sweight;
    int         rscale;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    /*
     * Unpack the argument and determine the result_var scale.  We choose a scale
     * to give at least NUMERIC_MIN_SIG_DIGITS significant digits; but in any
     * case not less than the input's dscale.
     */
    numeric_init(&result_var);

    /* Assume the input was normalized, so arg.weight is accurate */
    sweight = (num->weight + 1) * DEC_DIGITS / 2 - 1;

    rscale = NUMERIC_MIN_SIG_DIGITS - sweight;
    rscale = Max(rscale, num->dscale);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

    /*
     * Let sqrt_var() do the calculation and return the result_var.
     */
    errcode = sqrt_var(num, &result_var, rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_exp() -
 *
 *  Raise e to the power of x
 */
numeric_errcode_t
numeric_exp(const numeric *num, numeric *result)
{
    numeric  result_var;
    int         rscale;
    double      val;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    /*
     * Unpack the argument and determine the result_var scale.  We choose a scale
     * to give at least NUMERIC_MIN_SIG_DIGITS significant digits; but in any
     * case not less than the input's dscale.
     */
    numeric_init(&result_var);

    /* convert input to double, ignoring overflow */
    errcode = numericvar_to_double_no_overflow(num, &val);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    /*
     * log10(result_var) = num * log10(e), so this is approximately the decimal
     * weight of the result_var:
     */
    val *= 0.434294481903252;

    /* limit to something that won't cause integer overflow */
    val = Max(val, -NUMERIC_MAX_RESULT_SCALE);
    val = Min(val, NUMERIC_MAX_RESULT_SCALE);

    rscale = NUMERIC_MIN_SIG_DIGITS - (int) val;
    rscale = Max(rscale, num->dscale);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

    /*
     * Let exp_var() do the calculation and return the result_var.
     */
    errcode = exp_var(num, &result_var, rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_ln() -
 *
 *  Compute the natural logarithm of x
 */
numeric_errcode_t
numeric_ln(const numeric *num, numeric *result)
{
    numeric  result_var;
    int         dec_digits;
    int         rscale;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    numeric_init(&result_var);

    /* Approx decimal digits before decimal point */
    dec_digits = (num->weight + 1) * DEC_DIGITS;

    if (dec_digits > 1)
        rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(dec_digits - 1);
    else if (dec_digits < 1)
        rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(1 - dec_digits);
    else
        rscale = NUMERIC_MIN_SIG_DIGITS;

    rscale = Max(rscale, num->dscale);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

    errcode = ln_var(num, &result_var, rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * numeric_log10() -
 *
 *  Compute the logarithm of x in a base 10
 */
numeric_errcode_t
numeric_log10(const numeric *num, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num))
        return make_result(&const_nan, result);

    /*
     * Initialize things
     */
    numeric_init(&result_var);

    /*
     * Call log_var() to compute and return the result_var; note it handles scale
     * selection itself.
     */
    errcode = log_var(&const_ten, num, &result_var);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * numeric_power() -
 *
 *  Raise b to the power of x
 */
numeric_errcode_t
numeric_power(const numeric *num1, const numeric *num2, numeric *result)
{
    numeric  arg2_trunc;
    numeric  result_var;
    numeric_errcode_t errcode;

    /*
     * Handle NaN
     */
    if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
        return make_result(&const_nan, result);

    /*
     * Initialize things
     */
    numeric_init(&arg2_trunc);
    numeric_init(&result_var);

    set_var_from_var(num2, &arg2_trunc);

    trunc_var(&arg2_trunc, 0);

    /*
     * The SQL spec requires that we emit a particular SQLSTATE error code for
     * certain error conditions.  Specifically, we don't return a
     * divide-by-zero error code for 0 ^ -1.
     */
    if (cmp_var(num1, &const_zero) == 0 &&
        cmp_var(num2, &const_zero) < 0)
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    if (cmp_var(num1, &const_zero) < 0 &&
        cmp_var(num2, &arg2_trunc) != 0)
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    /*
     * Call power_var() to compute and return the result_var; note it handles
     * scale selection itself.
     */
    errcode = power_var(num1, num2, &result_var);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);
    numeric_dispose(&arg2_trunc);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/* ----------------------------------------------------------------------
 *
 * Type conversion functions
 *
 * ----------------------------------------------------------------------
 */


numeric_errcode_t
numeric_from_int32(int32_t val, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    numeric_init(&result_var);

    int64_to_numericvar((int64_t) val, &result_var);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_to_int32(const numeric *num, int32_t *result)
{
    numeric  x;
    numeric_errcode_t errcode;

    if (NUMERIC_IS_NAN(num))
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    /* Convert to variable format, then convert to int4 */
    numeric_init(&x);
    set_var_from_var(num, &x);
    errcode = numericvar_to_int32(&x, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;
    numeric_dispose(&x);
    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * Given a numeric, convert it to an int32. If the numeric
 * exceeds the range of an int32, raise the appropriate error via
 * ereport(). The input numeric is *not* free'd.
 */
static numeric_errcode_t
numericvar_to_int32(numeric *var, int32_t *result)
{
    int64_t     val;

    if (!numericvar_to_int64(var, &val))
        return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;

    /* Down-convert to int32_t */
    *result = (int32_t) val;

    /* Test for overflow by reverse-conversion. */
    if ((int64_t) result != val)
        return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;

    return NUMERIC_ERRCODE_NO_ERROR;
}

numeric_errcode_t
numeric_from_int64(int64_t val, numeric *result)
{
    numeric  result_var;
    numeric_errcode_t errcode;

    numeric_init(&result_var);

    int64_to_numericvar(val, &result_var);

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_to_int64(const numeric *num, int64_t *result)
{
    numeric  x;

    if (NUMERIC_IS_NAN(num))
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    /* Convert to variable format and thence to int8 */
    numeric_init(&x);
    set_var_from_var(num, &x);

    if (!numericvar_to_int64(&x, result))
        return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;

    numeric_dispose(&x);

    return NUMERIC_ERRCODE_NO_ERROR;
}



numeric_errcode_t
numeric_from_double(double val, numeric *result)
{
    numeric  result_var;
    char        buf[DBL_DIG + 100];
    numeric_errcode_t errcode;

    if (isnan(val))
        return make_result(&const_nan, result);

    sprintf(buf, "%.*g", DBL_DIG, val);

    numeric_init(&result_var);

    /* Assume we need not worry about leading/trailing spaces */
    errcode = set_var_from_str(buf, buf, &result_var, NULL);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_to_double(const numeric *num, double *result)
{
    char       *tmp;
    double      val;
    numeric_errcode_t errcode;

    if (NUMERIC_IS_NAN(num))
    {
        *result = get_double_nan();
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    errcode = numeric_out(num, &tmp);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = double_in(tmp, &val);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    free(tmp);

    *result = val;
    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_from_float(float val, numeric *result)
{
    numeric  result_var;
    char        buf[FLT_DIG + 100];
    numeric_errcode_t errcode;

    if (isnan(val))
        return make_result(&const_nan, result);

    sprintf(buf, "%.*g", FLT_DIG, val);

    numeric_init(&result_var);

    /* Assume we need not worry about leading/trailing spaces */
    errcode = set_var_from_str(buf, buf, &result_var, NULL);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = make_result(&result_var, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&result_var);

    return NUMERIC_ERRCODE_NO_ERROR;
}


numeric_errcode_t
numeric_to_float(const numeric *num, float *result)
{
    char       *tmp;
    numeric_errcode_t errcode;

    if (NUMERIC_IS_NAN(num))
    {
        *result = get_float_nan();
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    errcode = numeric_out(num, &tmp);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    errcode = float_in(tmp, result);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    free(tmp);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/* ----------------------------------------------------------------------
 *
 * Debug support
 *
 * ----------------------------------------------------------------------
 */

#ifdef NUMERIC_DEBUG
/*
 * dump_var() - Dump a value in the variable format for debugging
 */
static void
dump_var(const char *str, numeric *var)
{
    int         i;

    printf("%s: numeric w=%d d=%d ", str, var->weight, var->dscale);
    switch (var->sign)
    {
        case NUMERIC_POS:
            printf("POS");
            break;
        case NUMERIC_NEG:
            printf("NEG");
            break;
        case NUMERIC_NAN:
            printf("NaN");
            break;
        default:
            printf("SIGN=0x%x", var->sign);
            break;
    }

    for (i = 0; i < var->ndigits; i++)
        printf(" %0*d", DEC_DIGITS, var->digits[i]);

    printf("\n");
}
#endif   /* NUMERIC_DEBUG */


/* ----------------------------------------------------------------------
 *
 * Local functions follow
 *
 * In general, these do not support NaNs --- callers must eliminate
 * the possibility of NaN first.  (make_result() is an exception.)
 *
 * ----------------------------------------------------------------------
 */


/*
 * numeric_init() -
 *
 *  Initialize
 */
void
numeric_init(numeric *var)
{
    memset(var, 0, sizeof(numeric));
}

/*
 * numeric_dispose() -
 *
 *  Return the digit buffer of a variable to the free pool
 */
void
numeric_dispose(numeric *var)
{
    digitbuf_free(var->buf);
    var->buf = NULL;
    var->digits = NULL;
    var->sign = NUMERIC_NAN;
}

/*
 * alloc_var() -
 *
 *  Allocate a digit buffer of ndigits digits (plus a spare digit for rounding)
 */
static void
alloc_var(numeric *var, int ndigits)
{
    digitbuf_free(var->buf);
    var->buf = digitbuf_alloc(ndigits + 1);
    var->buf[0] = 0;            /* spare digit for rounding */
    var->digits = var->buf + 1;
    var->ndigits = ndigits;
}



/*
 * zero_var() -
 *
 *  Set a variable to ZERO.
 *  Note: its dscale is not touched.
 */
static void
zero_var(numeric *var)
{
    digitbuf_free(var->buf);
    var->buf = NULL;
    var->digits = NULL;
    var->ndigits = 0;
    var->weight = 0;            /* by convention; doesn't really matter */
    var->sign = NUMERIC_POS;    /* anything but NAN... */
}


/*
 * set_var_from_str()
 *
 *  Parse a string and put the number into a variable
 *
 * This function does not handle leading or trailing spaces, and it doesn't
 * accept "NaN" either.  It returns the end+1 position so that caller can
 * check for trailing spaces/garbage if deemed necessary.
 *
 * cp is the place to actually start parsing; str is what to use in error
 * reports.  (Typically cp would be the same except advanced over spaces.)
 */
static numeric_errcode_t
set_var_from_str(const char *str, const char *cp, numeric *dest,
    const char **result)
{
    bool        have_dp = false;
    int         i;
    unsigned char *decdigits;
    int         sign = NUMERIC_POS;
    int         dweight = -1;
    int         ddigits;
    int         dscale = 0;
    int         weight;
    int         ndigits;
    int         offset;
    NumericDigit *digits;

    /*
     * We first parse the string to extract decimal digits and determine the
     * correct decimal weight.  Then convert to NBASE representation.
     */
    switch (*cp)
    {
        case '+':
            sign = NUMERIC_POS;
            cp++;
            break;

        case '-':
            sign = NUMERIC_NEG;
            cp++;
            break;
    }

    if (*cp == '.')
    {
        have_dp = true;
        cp++;
    }

    if (!isdigit((unsigned char) *cp))
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    decdigits = (unsigned char *) malloc(strlen(cp) + DEC_DIGITS * 2);

    /* leading padding for digit alignment later */
    memset(decdigits, 0, DEC_DIGITS);
    i = DEC_DIGITS;

    while (*cp)
    {
        if (isdigit((unsigned char) *cp))
        {
            decdigits[i++] = *cp++ - '0';
            if (!have_dp)
                dweight++;
            else
                dscale++;
        }
        else if (*cp == '.')
        {
            if (have_dp)
                return NUMERIC_ERRCODE_INVALID_ARGUMENT;
            have_dp = true;
            cp++;
        }
        else
            break;
    }

    ddigits = i - DEC_DIGITS;
    /* trailing padding for digit alignment later */
    memset(decdigits + i, 0, DEC_DIGITS - 1);

    /* Handle exponent, if any */
    if (*cp == 'e' || *cp == 'E')
    {
        long        exponent;
        char       *endptr;

        cp++;
        exponent = strtol(cp, &endptr, 10);
        if (endptr == cp)
            return NUMERIC_ERRCODE_INVALID_ARGUMENT;
        cp = endptr;
        if (exponent > NUMERIC_MAX_PRECISION ||
            exponent < -NUMERIC_MAX_PRECISION)
            return NUMERIC_ERRCODE_INVALID_ARGUMENT;
        dweight += (int) exponent;
        dscale -= (int) exponent;
        if (dscale < 0)
            dscale = 0;
    }

    /*
     * Okay, convert pure-decimal representation to base NBASE.  First we need
     * to determine the converted weight and ndigits.  offset is the number of
     * decimal zeroes to insert before the first given digit to have a
     * correctly aligned first NBASE digit.
     */
    if (dweight >= 0)
        weight = (dweight + 1 + DEC_DIGITS - 1) / DEC_DIGITS - 1;
    else
        weight = -((-dweight - 1) / DEC_DIGITS + 1);
    offset = (weight + 1) * DEC_DIGITS - (dweight + 1);
    ndigits = (ddigits + offset + DEC_DIGITS - 1) / DEC_DIGITS;

    alloc_var(dest, ndigits);
    dest->sign = sign;
    dest->weight = weight;
    dest->dscale = dscale;

    i = DEC_DIGITS - offset;
    digits = dest->digits;

    while (ndigits-- > 0)
    {
#if DEC_DIGITS == 4
        *digits++ = ((decdigits[i] * 10 + decdigits[i + 1]) * 10 +
                     decdigits[i + 2]) * 10 + decdigits[i + 3];
#elif DEC_DIGITS == 2
        *digits++ = decdigits[i] * 10 + decdigits[i + 1];
#elif DEC_DIGITS == 1
        *digits++ = decdigits[i];
#else
#error unsupported NBASE
#endif
        i += DEC_DIGITS;
    }

    free(decdigits);

    /* Strip any leading/trailing zeroes, and normalize weight if zero */
    strip_var(dest);

    /* Return end+1 position for caller */
    if (result)
        *result = cp;
    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * set_var_from_var() -
 *
 *  Copy one variable into another with an extra digit space for carry.
 */
static void
set_var_from_var(const numeric *value, numeric *dest)
{
    NumericDigit *newbuf;

    newbuf = digitbuf_alloc(value->ndigits + 1);
    newbuf[0] = 0;              /* spare digit for rounding */
    memcpy(newbuf + 1, value->digits, value->ndigits * sizeof(NumericDigit));

    digitbuf_free(dest->buf);

    memmove(dest, value, sizeof(numeric));
    dest->buf = newbuf;
    dest->digits = newbuf + 1;
}

/*
 * copy_var() -
 *
 *  Copy one variable into another without an extra digit space for carry.
 */
static void
copy_var(const numeric *value, numeric *dest)
{
    NumericDigit *newbuf;

    newbuf = digitbuf_alloc(value->ndigits);
    memcpy(newbuf, value->digits, value->ndigits * sizeof(NumericDigit));

    digitbuf_free(dest->buf);

    memmove(dest, value, sizeof(numeric));
    dest->buf = newbuf;
    dest->digits = newbuf;
}


/*
 * get_str_from_var() -
 *
 *  Convert a var to text representation (guts of numeric_out).
 *  CAUTION: var's contents may be modified by rounding!
 *  Returns a malloc'd string.
 */
static char *
get_str_from_var(numeric *var, int dscale)
{
    char       *str;
    char       *cp;
    char       *endcp;
    int         i;
    int         d;
    NumericDigit dig;

#if DEC_DIGITS > 1
    NumericDigit d1;
#endif

    if (dscale < 0)
        dscale = 0;

    /*
     * Check if we must round up before printing the value and do so.
     */
    round_var(var, dscale);

    /*
     * Allocate space for the result.
     *
     * i is set to to # of decimal digits before decimal point. dscale is the
     * # of decimal digits we will print after decimal point. We may generate
     * as many as DEC_DIGITS-1 excess digits at the end, and in addition we
     * need room for sign, decimal point, null terminator.
     */
    i = (var->weight + 1) * DEC_DIGITS;
    if (i <= 0)
        i = 1;

    str = malloc(i + dscale + DEC_DIGITS + 2);
    cp = str;

    /*
     * Output a dash for negative values
     */
    if (var->sign == NUMERIC_NEG)
        *cp++ = '-';

    /*
     * Output all digits before the decimal point
     */
    if (var->weight < 0)
    {
        d = var->weight + 1;
        *cp++ = '0';
    }
    else
    {
        for (d = 0; d <= var->weight; d++)
        {
            dig = (d < var->ndigits) ? var->digits[d] : 0;
            /* In the first digit, suppress extra leading decimal zeroes */
#if DEC_DIGITS == 4
            {
                bool        putit = (d > 0);

                d1 = dig / 1000;
                dig -= d1 * 1000;
                putit |= (d1 > 0);
                if (putit)
                    *cp++ = d1 + '0';
                d1 = dig / 100;
                dig -= d1 * 100;
                putit |= (d1 > 0);
                if (putit)
                    *cp++ = d1 + '0';
                d1 = dig / 10;
                dig -= d1 * 10;
                putit |= (d1 > 0);
                if (putit)
                    *cp++ = d1 + '0';
                *cp++ = dig + '0';
            }
#elif DEC_DIGITS == 2
            d1 = dig / 10;
            dig -= d1 * 10;
            if (d1 > 0 || d > 0)
                *cp++ = d1 + '0';
            *cp++ = dig + '0';
#elif DEC_DIGITS == 1
            *cp++ = dig + '0';
#else
#error unsupported NBASE
#endif
        }
    }

    /*
     * If requested, output a decimal point and all the digits that follow it.
     * We initially put out a multiple of DEC_DIGITS digits, then truncate if
     * needed.
     */
    if (dscale > 0)
    {
        *cp++ = '.';
        endcp = cp + dscale;
        for (i = 0; i < dscale; d++, i += DEC_DIGITS)
        {
            dig = (d >= 0 && d < var->ndigits) ? var->digits[d] : 0;
#if DEC_DIGITS == 4
            d1 = dig / 1000;
            dig -= d1 * 1000;
            *cp++ = d1 + '0';
            d1 = dig / 100;
            dig -= d1 * 100;
            *cp++ = d1 + '0';
            d1 = dig / 10;
            dig -= d1 * 10;
            *cp++ = d1 + '0';
            *cp++ = dig + '0';
#elif DEC_DIGITS == 2
            d1 = dig / 10;
            dig -= d1 * 10;
            *cp++ = d1 + '0';
            *cp++ = dig + '0';
#elif DEC_DIGITS == 1
            *cp++ = dig + '0';
#else
#error unsupported NBASE
#endif
        }
        cp = endcp;
    }

    /*
     * terminate the string and return it
     */
    *cp = '\0';
    return str;
}

/*
 * get_str_from_var_sci() -
 *
 *  Convert a var to a normalised scientific notation text representation.
 *  This function does the heavy lifting for numeric_to_str_sci().
 *
 *  This notation has the general form a * 10^b, where a is known as the
 *  "significand" and b is known as the "exponent".
 *
 *  Because we can't do superscript in ASCII (and because we want to copy
 *  printf's behaviour) we display the exponent using E notation, with a
 *  minimum of two exponent digits.
 *
 *  For example, the value 1234 could be output as 1.2e+03.
 *
 *  We assume that the exponent can fit into an int32.
 *
 *  rscale is the number of decimal digits desired after the decimal point in
 *  the output, negative values will be treated as meaning zero.
 *
 *  CAUTION: var's contents may be modified by rounding!
 *
 *  Returns a malloc'd string.
 */
static char *
get_str_from_var_sci(numeric *var, int rscale)
{
    int32_t     exponent;
    numeric  denominator;
    numeric  significand;
    int         denom_scale;
    size_t      len;
    char       *str;
    char       *sig_out;

    if (rscale < 0)
        rscale = 0;

    /*
     * Determine the exponent of this number in normalised form.
     *
     * This is the exponent required to represent the number with only one
     * significant digit before the decimal place.
     */
    if (var->ndigits > 0)
    {
        exponent = (var->weight + 1) * DEC_DIGITS;

        /*
         * Compensate for leading decimal zeroes in the first numeric digit by
         * decrementing the exponent.
         */
        exponent -= DEC_DIGITS - (int) log10(var->digits[0]);
    }
    else
    {
        /*
         * If var has no digits, then it must be zero.
         *
         * Zero doesn't technically have a meaningful exponent in normalised
         * notation, but we just display the exponent as zero for consistency
         * of output.
         */
        exponent = 0;
    }

    /*
     * The denominator is set to 10 raised to the power of the exponent.
     *
     * We then divide var by the denominator to get the significand, rounding
     * to rscale decimal digits in the process.
     */
    if (exponent < 0)
        denom_scale = -exponent;
    else
        denom_scale = 0;

    numeric_init(&denominator);
    numeric_init(&significand);

    int64_to_numericvar((int64_t) 10, &denominator);
    power_var_int(&denominator, exponent, &denominator, denom_scale);
    div_var(var, &denominator, &significand, rscale, true);
    sig_out = get_str_from_var(&significand, rscale);

    numeric_dispose(&denominator);
    numeric_dispose(&significand);

    /*
     * Allocate space for the result.
     *
     * In addition to the significand, we need room for the exponent
     * decoration ("e"), the sign of the exponent, up to 10 digits for the
     * exponent itself, and of course the null terminator.
     */
    len = strlen(sig_out) + 13;
    str = malloc(len);
    snprintf(str, len, "%se%+03d", sig_out, exponent);

    free(sig_out);

    return str;
}


/*
 * make_result() -
 *
 *  Create the numeric for the result whose digit buf malloc()'d memory from
 *  a variable.
 */
static numeric_errcode_t
make_result(const numeric *var, numeric *result)
{
    NumericDigit *digits = var->digits;
    int         weight = var->weight;
    int         n = var->ndigits;
    NumericDigit *res_digits;

    if (NUMERIC_IS_NAN(var))
    {
        digitbuf_free(result->buf);
        *result = const_nan;
        dump_var("make_result()", result);
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /* truncate leading zeroes */
    while (n > 0 && *digits == 0)
    {
        digits++;
        weight--;
        n--;
    }
    /* truncate trailing zeroes */
    while (n > 0 && digits[n - 1] == 0)
        n--;

    /* If zero result, force to weight=0 and positive sign */
    if (n == 0)
    {
        zero_var(result);
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /* Check for overflow of int16 fields */
    if (weight < INT16_MIN || INT16_MAX < weight
        || var->dscale < INT16_MIN || INT16_MAX < var->dscale)
        return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;

    /* Build the result */
    res_digits = (NumericDigit *)malloc(n * sizeof(NumericDigit));
    if (!res_digits)
        return NUMERIC_ERRCODE_OUT_OF_MEMORY;

    memcpy(res_digits, digits, n * sizeof(NumericDigit));

    digitbuf_free(result->buf);
    result->ndigits = n;
    result->weight = weight;
    result->sign = var->sign;
    result->dscale = var->dscale;
    result->buf = result->digits = res_digits;

    dump_var("make_result()", result);
    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * check_bounds_and_round() -
 *
 *  Do bounds checking and rounding according to the precision and scale.
 */
static numeric_errcode_t
check_bounds_and_round(numeric *var, int precision, int scale)
{
    int         maxdigits;
    int         ddigits;
    int         i;

    /* Do nothing if we have a default precision (-1) */
    if (precision < 0)
        return NUMERIC_ERRCODE_NO_ERROR;

    maxdigits = precision - scale;

    /* Round to target scale (and set var->dscale) */
    round_var(var, scale);

    /*
     * Check for overflow - note we can't do this before rounding, because
     * rounding could raise the weight.  Also note that the var's weight could
     * be inflated by leading zeroes, which will be stripped before storage
     * but perhaps might not have been yet. In any case, we must recognize a
     * true zero, whose weight doesn't mean anything.
     */
    ddigits = (var->weight + 1) * DEC_DIGITS;
    if (ddigits > maxdigits)
    {
        /* Determine true weight; and check for all-zero result */
        for (i = 0; i < var->ndigits; i++)
        {
            NumericDigit dig = var->digits[i];

            if (dig)
            {
                /* Adjust for any high-order decimal zero digits */
#if DEC_DIGITS == 4
                if (dig < 10)
                    ddigits -= 3;
                else if (dig < 100)
                    ddigits -= 2;
                else if (dig < 1000)
                    ddigits -= 1;
#elif DEC_DIGITS == 2
                if (dig < 10)
                    ddigits -= 1;
#elif DEC_DIGITS == 1
                /* no adjustment */
#else
#error unsupported NBASE
#endif
                if (ddigits > maxdigits)
                    return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;
                break;
            }
            ddigits -= DEC_DIGITS;
        }
    }
    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * Convert numeric to int8, rounding if needed.
 *
 * If overflow, return false (no error is raised).  Return true if okay.
 *
 *  CAUTION: var's contents may be modified by rounding!
 */
static bool
numericvar_to_int64(numeric *var, int64_t *result)
{
    NumericDigit *digits;
    int         ndigits;
    int         weight;
    int         i;
    int64_t       val,
                oldval;
    bool        neg;

    /* Round to nearest integer */
    round_var(var, 0);

    /* Check for zero input */
    strip_var(var);
    ndigits = var->ndigits;
    if (ndigits == 0)
    {
        *result = 0;
        return true;
    }

    /*
     * For input like 10000000000, we must treat stripped digits as real. So
     * the loop assumes there are weight+1 digits before the decimal point.
     */
    weight = var->weight;
    Assert(weight >= 0 && ndigits <= weight + 1);

    /* Construct the result */
    digits = var->digits;
    neg = (var->sign == NUMERIC_NEG);
    val = digits[0];
    for (i = 1; i <= weight; i++)
    {
        oldval = val;
        val *= NBASE;
        if (i < ndigits)
            val += digits[i];

        /*
         * The overflow check is a bit tricky because we want to accept
         * INT64_MIN, which will overflow the positive accumulator.  We can
         * detect this case easily though because INT64_MIN is the only
         * nonzero value for which -val == val (on a two's complement machine,
         * anyway).
         */
        if ((val / NBASE) != oldval)    /* possible overflow? */
        {
            if (!neg || (-val) != val || val == 0 || oldval < 0)
                return false;
        }
    }

    *result = neg ? -val : val;
    return true;
}

/*
 * Convert int8 value to numeric.
 */
static void
int64_to_numericvar(int64_t val, numeric *var)
{
    uint64_t      uval,
                newuval;
    NumericDigit *ptr;
    int         ndigits;

    /* int8 can require at most 19 decimal digits; add one for safety */
    alloc_var(var, 20 / DEC_DIGITS);
    if (val < 0)
    {
        var->sign = NUMERIC_NEG;
        uval = -val;
    }
    else
    {
        var->sign = NUMERIC_POS;
        uval = val;
    }
    var->dscale = 0;
    if (val == 0)
    {
        var->ndigits = 0;
        var->weight = 0;
        return;
    }
    ptr = var->digits + var->ndigits;
    ndigits = 0;
    do
    {
        ptr--;
        ndigits++;
        newuval = uval / NBASE;
        *ptr = uval - newuval * NBASE;
        uval = newuval;
    } while (uval);
    var->digits = ptr;
    var->ndigits = ndigits;
    var->weight = ndigits - 1;
}

/* As above, but work from a numeric */
static numeric_errcode_t
numericvar_to_double_no_overflow(const numeric *var, double *result)
{
    numeric     num;
    char       *tmp;
    double      val;
    char       *endptr;

    numeric_init(&num);
    set_var_from_var(var, &num);
    tmp = get_str_from_var(&num, num.dscale);
    numeric_dispose(&num);

    /* unlike doublein, we ignore ERANGE from strtod */
    val = strtod(tmp, &endptr);
    if (*endptr != '\0')
    {
        /* shouldn't happen ... */
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;
    }

    free(tmp);

    *result = val;
    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * cmp_var() -
 *
 *  Compare two values on variable level.  We assume zeroes have been
 *  truncated to no digits.
 */
static int
cmp_var(const numeric *var1, const numeric *var2)
{
    return cmp_var_common(var1->digits, var1->ndigits,
                          var1->weight, var1->sign,
                          var2->digits, var2->ndigits,
                          var2->weight, var2->sign);
}

/*
 * cmp_var_common() -
 *
 *  Main routine of cmp_var(). This function can be used by both
 *  numeric and Numeric.
 */
static int
cmp_var_common(const NumericDigit *var1digits, int var1ndigits,
               int var1weight, int var1sign,
               const NumericDigit *var2digits, int var2ndigits,
               int var2weight, int var2sign)
{
    if (var1ndigits == 0)
    {
        if (var2ndigits == 0)
            return 0;
        if (var2sign == NUMERIC_NEG)
            return 1;
        return -1;
    }
    if (var2ndigits == 0)
    {
        if (var1sign == NUMERIC_POS)
            return 1;
        return -1;
    }

    if (var1sign == NUMERIC_POS)
    {
        if (var2sign == NUMERIC_NEG)
            return 1;
        return cmp_abs_common(var1digits, var1ndigits, var1weight,
                              var2digits, var2ndigits, var2weight);
    }

    if (var2sign == NUMERIC_POS)
        return -1;

    return cmp_abs_common(var2digits, var2ndigits, var2weight,
                          var1digits, var1ndigits, var1weight);
}


/*
 * add_var() -
 *
 *  Full version of add functionality on variable level (handling signs).
 *  result might point to one of the operands too without danger.
 */
static void
add_var(const numeric *var1, const numeric *var2, numeric *result)
{
    /*
     * Decide on the signs of the two variables what to do
     */
    if (var1->sign == NUMERIC_POS)
    {
        if (var2->sign == NUMERIC_POS)
        {
            /*
             * Both are positive result = +(ABS(var1) + ABS(var2))
             */
            add_abs(var1, var2, result);
            result->sign = NUMERIC_POS;
        }
        else
        {
            /*
             * var1 is positive, var2 is negative Must compare absolute values
             */
            switch (cmp_abs(var1, var2))
            {
                case 0:
                    /* ----------
                     * ABS(var1) == ABS(var2)
                     * result = ZERO
                     * ----------
                     */
                    zero_var(result);
                    result->dscale = Max(var1->dscale, var2->dscale);
                    break;

                case 1:
                    /* ----------
                     * ABS(var1) > ABS(var2)
                     * result = +(ABS(var1) - ABS(var2))
                     * ----------
                     */
                    sub_abs(var1, var2, result);
                    result->sign = NUMERIC_POS;
                    break;

                case -1:
                    /* ----------
                     * ABS(var1) < ABS(var2)
                     * result = -(ABS(var2) - ABS(var1))
                     * ----------
                     */
                    sub_abs(var2, var1, result);
                    result->sign = NUMERIC_NEG;
                    break;
            }
        }
    }
    else
    {
        if (var2->sign == NUMERIC_POS)
        {
            /* ----------
             * var1 is negative, var2 is positive
             * Must compare absolute values
             * ----------
             */
            switch (cmp_abs(var1, var2))
            {
                case 0:
                    /* ----------
                     * ABS(var1) == ABS(var2)
                     * result = ZERO
                     * ----------
                     */
                    zero_var(result);
                    result->dscale = Max(var1->dscale, var2->dscale);
                    break;

                case 1:
                    /* ----------
                     * ABS(var1) > ABS(var2)
                     * result = -(ABS(var1) - ABS(var2))
                     * ----------
                     */
                    sub_abs(var1, var2, result);
                    result->sign = NUMERIC_NEG;
                    break;

                case -1:
                    /* ----------
                     * ABS(var1) < ABS(var2)
                     * result = +(ABS(var2) - ABS(var1))
                     * ----------
                     */
                    sub_abs(var2, var1, result);
                    result->sign = NUMERIC_POS;
                    break;
            }
        }
        else
        {
            /* ----------
             * Both are negative
             * result = -(ABS(var1) + ABS(var2))
             * ----------
             */
            add_abs(var1, var2, result);
            result->sign = NUMERIC_NEG;
        }
    }
}


/*
 * sub_var() -
 *
 *  Full version of sub functionality on variable level (handling signs).
 *  result might point to one of the operands too without danger.
 */
static void
sub_var(const numeric *var1, const numeric *var2, numeric *result)
{
    /*
     * Decide on the signs of the two variables what to do
     */
    if (var1->sign == NUMERIC_POS)
    {
        if (var2->sign == NUMERIC_NEG)
        {
            /* ----------
             * var1 is positive, var2 is negative
             * result = +(ABS(var1) + ABS(var2))
             * ----------
             */
            add_abs(var1, var2, result);
            result->sign = NUMERIC_POS;
        }
        else
        {
            /* ----------
             * Both are positive
             * Must compare absolute values
             * ----------
             */
            switch (cmp_abs(var1, var2))
            {
                case 0:
                    /* ----------
                     * ABS(var1) == ABS(var2)
                     * result = ZERO
                     * ----------
                     */
                    zero_var(result);
                    result->dscale = Max(var1->dscale, var2->dscale);
                    break;

                case 1:
                    /* ----------
                     * ABS(var1) > ABS(var2)
                     * result = +(ABS(var1) - ABS(var2))
                     * ----------
                     */
                    sub_abs(var1, var2, result);
                    result->sign = NUMERIC_POS;
                    break;

                case -1:
                    /* ----------
                     * ABS(var1) < ABS(var2)
                     * result = -(ABS(var2) - ABS(var1))
                     * ----------
                     */
                    sub_abs(var2, var1, result);
                    result->sign = NUMERIC_NEG;
                    break;
            }
        }
    }
    else
    {
        if (var2->sign == NUMERIC_NEG)
        {
            /* ----------
             * Both are negative
             * Must compare absolute values
             * ----------
             */
            switch (cmp_abs(var1, var2))
            {
                case 0:
                    /* ----------
                     * ABS(var1) == ABS(var2)
                     * result = ZERO
                     * ----------
                     */
                    zero_var(result);
                    result->dscale = Max(var1->dscale, var2->dscale);
                    break;

                case 1:
                    /* ----------
                     * ABS(var1) > ABS(var2)
                     * result = -(ABS(var1) - ABS(var2))
                     * ----------
                     */
                    sub_abs(var1, var2, result);
                    result->sign = NUMERIC_NEG;
                    break;

                case -1:
                    /* ----------
                     * ABS(var1) < ABS(var2)
                     * result = +(ABS(var2) - ABS(var1))
                     * ----------
                     */
                    sub_abs(var2, var1, result);
                    result->sign = NUMERIC_POS;
                    break;
            }
        }
        else
        {
            /* ----------
             * var1 is negative, var2 is positive
             * result = -(ABS(var1) + ABS(var2))
             * ----------
             */
            add_abs(var1, var2, result);
            result->sign = NUMERIC_NEG;
        }
    }
}


/*
 * mul_var() -
 *
 *  Multiplication on variable level. Product of var1 * var2 is stored
 *  in result.  Result is rounded to no more than rscale fractional digits.
 */
static void
mul_var(const numeric *var1, const numeric *var2, numeric *result,
        int rscale)
{
    int         res_ndigits;
    int         res_sign;
    int         res_weight;
    int         maxdigits;
    int        *dig;
    int         carry;
    int         maxdig;
    int         newdig;
    NumericDigit *res_digits;
    int         i,
                ri,
                i1,
                i2;

    /* copy these values into local vars for speed in inner loop */
    int         var1ndigits = var1->ndigits;
    int         var2ndigits = var2->ndigits;
    NumericDigit *var1digits = var1->digits;
    NumericDigit *var2digits = var2->digits;

    if (var1ndigits == 0 || var2ndigits == 0)
    {
        /* one or both inputs is zero; so is result */
        zero_var(result);
        result->dscale = rscale;
        return;
    }

    /* Determine result sign and (maximum possible) weight */
    if (var1->sign == var2->sign)
        res_sign = NUMERIC_POS;
    else
        res_sign = NUMERIC_NEG;
    res_weight = var1->weight + var2->weight + 2;

    /*
     * Determine number of result digits to compute.  If the exact result
     * would have more than rscale fractional digits, truncate the computation
     * with MUL_GUARD_DIGITS guard digits.  We do that by pretending that one
     * or both inputs have fewer digits than they really do.
     */
    res_ndigits = var1ndigits + var2ndigits + 1;
    maxdigits = res_weight + 1 + (rscale * DEC_DIGITS) + MUL_GUARD_DIGITS;
    if (res_ndigits > maxdigits)
    {
        if (maxdigits < 3)
        {
            /* no useful precision at all in the result... */
            zero_var(result);
            result->dscale = rscale;
            return;
        }
        /* force maxdigits odd so that input ndigits can be equal */
        if ((maxdigits & 1) == 0)
            maxdigits++;
        if (var1ndigits > var2ndigits)
        {
            var1ndigits -= res_ndigits - maxdigits;
            if (var1ndigits < var2ndigits)
                var1ndigits = var2ndigits = (var1ndigits + var2ndigits) / 2;
        }
        else
        {
            var2ndigits -= res_ndigits - maxdigits;
            if (var2ndigits < var1ndigits)
                var1ndigits = var2ndigits = (var1ndigits + var2ndigits) / 2;
        }
        res_ndigits = maxdigits;
        Assert(res_ndigits == var1ndigits + var2ndigits + 1);
    }

    /*
     * We do the arithmetic in an array "dig[]" of signed int's.  Since
     * INT_MAX is noticeably larger than NBASE*NBASE, this gives us headroom
     * to avoid normalizing carries immediately.
     *
     * maxdig tracks the maximum possible value of any dig[] entry; when this
     * threatens to exceed INT_MAX, we take the time to propagate carries. To
     * avoid overflow in maxdig itself, it actually represents the max
     * possible value divided by NBASE-1.
     */
    dig = (int *) calloc(sizeof(int), res_ndigits);
    maxdig = 0;

    ri = res_ndigits - 1;
    for (i1 = var1ndigits - 1; i1 >= 0; ri--, i1--)
    {
        int         var1digit = var1digits[i1];

        if (var1digit == 0)
            continue;

        /* Time to normalize? */
        maxdig += var1digit;
        if (maxdig > INT_MAX / (NBASE - 1))
        {
            /* Yes, do it */
            carry = 0;
            for (i = res_ndigits - 1; i >= 0; i--)
            {
                newdig = dig[i] + carry;
                if (newdig >= NBASE)
                {
                    carry = newdig / NBASE;
                    newdig -= carry * NBASE;
                }
                else
                    carry = 0;
                dig[i] = newdig;
            }
            Assert(carry == 0);
            /* Reset maxdig to indicate new worst-case */
            maxdig = 1 + var1digit;
        }

        /* Add appropriate multiple of var2 into the accumulator */
        i = ri;
        for (i2 = var2ndigits - 1; i2 >= 0; i2--)
            dig[i--] += var1digit * var2digits[i2];
    }

    /*
     * Now we do a final carry propagation pass to normalize the result, which
     * we combine with storing the result digits into the output. Note that
     * this is still done at full precision w/guard digits.
     */
    alloc_var(result, res_ndigits);
    res_digits = result->digits;
    carry = 0;
    for (i = res_ndigits - 1; i >= 0; i--)
    {
        newdig = dig[i] + carry;
        if (newdig >= NBASE)
        {
            carry = newdig / NBASE;
            newdig -= carry * NBASE;
        }
        else
            carry = 0;
        res_digits[i] = newdig;
    }
    Assert(carry == 0);

    free(dig);

    /*
     * Finally, round the result to the requested precision.
     */
    result->weight = res_weight;
    result->sign = res_sign;

    /* Round to target rscale (and set result->dscale) */
    round_var(result, rscale);

    /* Strip leading and trailing zeroes */
    strip_var(result);
}


/*
 * div_var() -
 *
 *  Division on variable level. Quotient of var1 / var2 is stored in result.
 *  The quotient is figured to exactly rscale fractional digits.
 *  If round is true, it is rounded at the rscale'th digit; if false, it
 *  is truncated (towards zero) at that digit.
 */
static numeric_errcode_t
div_var(const numeric *var1, const numeric *var2, numeric *result,
        int rscale, bool round)
{
    int         div_ndigits;
    int         res_ndigits;
    int         res_sign;
    int         res_weight;
    int         carry;
    int         borrow;
    int         divisor1;
    int         divisor2;
    NumericDigit *dividend;
    NumericDigit *divisor;
    NumericDigit *res_digits;
    int         i;
    int         j;

    /* copy these values into local vars for speed in inner loop */
    int         var1ndigits = var1->ndigits;
    int         var2ndigits = var2->ndigits;

    /*
     * First of all division by zero check; we must not be handed an
     * unnormalized divisor.
     */
    if (var2ndigits == 0 || var2->digits[0] == 0)
        return NUMERIC_ERRCODE_DIVISION_BY_ZERO;

    /*
     * Now result zero check
     */
    if (var1ndigits == 0)
    {
        zero_var(result);
        result->dscale = rscale;
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /*
     * Determine the result sign, weight and number of digits to calculate.
     * The weight figured here is correct if the emitted quotient has no
     * leading zero digits; otherwise strip_var() will fix things up.
     */
    if (var1->sign == var2->sign)
        res_sign = NUMERIC_POS;
    else
        res_sign = NUMERIC_NEG;
    res_weight = var1->weight - var2->weight;
    /* The number of accurate result digits we need to produce: */
    res_ndigits = res_weight + 1 + (rscale + DEC_DIGITS - 1) / DEC_DIGITS;
    /* ... but always at least 1 */
    res_ndigits = Max(res_ndigits, 1);
    /* If rounding needed, figure one more digit to ensure correct result */
    if (round)
        res_ndigits++;

    /*
     * The working dividend normally requires res_ndigits + var2ndigits
     * digits, but make it at least var1ndigits so we can load all of var1
     * into it.  (There will be an additional digit dividend[0] in the
     * dividend space, but for consistency with Knuth's notation we don't
     * count that in div_ndigits.)
     */
    div_ndigits = res_ndigits + var2ndigits;
    div_ndigits = Max(div_ndigits, var1ndigits);

    /*
     * We need a workspace with room for the working dividend (div_ndigits+1
     * digits) plus room for the possibly-normalized divisor (var2ndigits
     * digits).  It is convenient also to have a zero at divisor[0] with the
     * actual divisor data in divisor[1 .. var2ndigits].  Transferring the
     * digits into the workspace also allows us to realloc the result (which
     * might be the same as either input var) before we begin the main loop.
     * Note that we use palloc0 to ensure that divisor[0], dividend[0], and
     * any additional dividend positions beyond var1ndigits, start out 0.
     */
    dividend = (NumericDigit *)
        calloc(sizeof(NumericDigit), div_ndigits + var2ndigits + 2);
    divisor = dividend + (div_ndigits + 1);
    memcpy(dividend + 1, var1->digits, var1ndigits * sizeof(NumericDigit));
    memcpy(divisor + 1, var2->digits, var2ndigits * sizeof(NumericDigit));

    /*
     * Now we can realloc the result to hold the generated quotient digits.
     */
    alloc_var(result, res_ndigits);
    res_digits = result->digits;

    if (var2ndigits == 1)
    {
        /*
         * If there's only a single divisor digit, we can use a fast path (cf.
         * Knuth section 4.3.1 exercise 16).
         */
        divisor1 = divisor[1];
        carry = 0;
        for (i = 0; i < res_ndigits; i++)
        {
            carry = carry * NBASE + dividend[i + 1];
            res_digits[i] = carry / divisor1;
            carry = carry % divisor1;
        }
    }
    else
    {
        /*
         * The full multiple-place algorithm is taken from Knuth volume 2,
         * Algorithm 4.3.1D.
         *
         * We need the first divisor digit to be >= NBASE/2.  If it isn't,
         * make it so by scaling up both the divisor and dividend by the
         * factor "d".  (The reason for allocating dividend[0] above is to
         * leave room for possible carry here.)
         */
        if (divisor[1] < HALF_NBASE)
        {
            int         d = NBASE / (divisor[1] + 1);

            carry = 0;
            for (i = var2ndigits; i > 0; i--)
            {
                carry += divisor[i] * d;
                divisor[i] = carry % NBASE;
                carry = carry / NBASE;
            }
            Assert(carry == 0);
            carry = 0;
            /* at this point only var1ndigits of dividend can be nonzero */
            for (i = var1ndigits; i >= 0; i--)
            {
                carry += dividend[i] * d;
                dividend[i] = carry % NBASE;
                carry = carry / NBASE;
            }
            Assert(carry == 0);
            Assert(divisor[1] >= HALF_NBASE);
        }
        /* First 2 divisor digits are used repeatedly in main loop */
        divisor1 = divisor[1];
        divisor2 = divisor[2];

        /*
         * Begin the main loop.  Each iteration of this loop produces the j'th
         * quotient digit by dividing dividend[j .. j + var2ndigits] by the
         * divisor; this is essentially the same as the common manual
         * procedure for long division.
         */
        for (j = 0; j < res_ndigits; j++)
        {
            /* Estimate quotient digit from the first two dividend digits */
            int         next2digits = dividend[j] * NBASE + dividend[j + 1];
            int         qhat;

            /*
             * If next2digits are 0, then quotient digit must be 0 and there's
             * no need to adjust the working dividend.  It's worth testing
             * here to fall out ASAP when processing trailing zeroes in a
             * dividend.
             */
            if (next2digits == 0)
            {
                res_digits[j] = 0;
                continue;
            }

            if (dividend[j] == divisor1)
                qhat = NBASE - 1;
            else
                qhat = next2digits / divisor1;

            /*
             * Adjust quotient digit if it's too large.  Knuth proves that
             * after this step, the quotient digit will be either correct or
             * just one too large.  (Note: it's OK to use dividend[j+2] here
             * because we know the divisor length is at least 2.)
             */
            while (divisor2 * qhat >
                   (next2digits - qhat * divisor1) * NBASE + dividend[j + 2])
                qhat--;

            /* As above, need do nothing more when quotient digit is 0 */
            if (qhat > 0)
            {
                /*
                 * Multiply the divisor by qhat, and subtract that from the
                 * working dividend.  "carry" tracks the multiplication,
                 * "borrow" the subtraction (could we fold these together?)
                 */
                carry = 0;
                borrow = 0;
                for (i = var2ndigits; i >= 0; i--)
                {
                    carry += divisor[i] * qhat;
                    borrow -= carry % NBASE;
                    carry = carry / NBASE;
                    borrow += dividend[j + i];
                    if (borrow < 0)
                    {
                        dividend[j + i] = borrow + NBASE;
                        borrow = -1;
                    }
                    else
                    {
                        dividend[j + i] = borrow;
                        borrow = 0;
                    }
                }
                Assert(carry == 0);

                /*
                 * If we got a borrow out of the top dividend digit, then
                 * indeed qhat was one too large.  Fix it, and add back the
                 * divisor to correct the working dividend.  (Knuth proves
                 * that this will occur only about 3/NBASE of the time; hence,
                 * it's a good idea to test this code with small NBASE to be
                 * sure this section gets exercised.)
                 */
                if (borrow)
                {
                    qhat--;
                    carry = 0;
                    for (i = var2ndigits; i >= 0; i--)
                    {
                        carry += dividend[j + i] + divisor[i];
                        if (carry >= NBASE)
                        {
                            dividend[j + i] = carry - NBASE;
                            carry = 1;
                        }
                        else
                        {
                            dividend[j + i] = carry;
                            carry = 0;
                        }
                    }
                    /* A carry should occur here to cancel the borrow above */
                    Assert(carry == 1);
                }
            }

            /* And we're done with this quotient digit */
            res_digits[j] = qhat;
        }
    }

    free(dividend);

    /*
     * Finally, round or truncate the result to the requested precision.
     */
    result->weight = res_weight;
    result->sign = res_sign;

    /* Round or truncate to target rscale (and set result->dscale) */
    if (round)
        round_var(result, rscale);
    else
        trunc_var(result, rscale);

    /* Strip leading and trailing zeroes */
    strip_var(result);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * div_var_fast() -
 *
 *  This has the same API as div_var, but is implemented using the division
 *  algorithm from the "FM" library, rather than Knuth's schoolbook-division
 *  approach.  This is significantly faster but can produce inaccurate
 *  results, because it sometimes has to propagate rounding to the left,
 *  and so we can never be entirely sure that we know the requested digits
 *  exactly.  We compute DIV_GUARD_DIGITS extra digits, but there is
 *  no certainty that that's enough.  We use this only in the transcendental
 *  function calculation routines, where everything is approximate anyway.
 */
static numeric_errcode_t
div_var_fast(numeric *var1, numeric *var2, numeric *result,
             int rscale, bool round)
{
    int         div_ndigits;
    int         res_sign;
    int         res_weight;
    int        *div;
    int         qdigit;
    int         carry;
    int         maxdiv;
    int         newdig;
    NumericDigit *res_digits;
    double      fdividend,
                fdivisor,
                fdivisorinverse,
                fquotient;
    int         qi;
    int         i;

    /* copy these values into local vars for speed in inner loop */
    int         var1ndigits = var1->ndigits;
    int         var2ndigits = var2->ndigits;
    NumericDigit *var1digits = var1->digits;
    NumericDigit *var2digits = var2->digits;

    /*
     * First of all division by zero check; we must not be handed an
     * unnormalized divisor.
     */
    if (var2ndigits == 0 || var2digits[0] == 0)
        return NUMERIC_ERRCODE_DIVISION_BY_ZERO;

    /*
     * Now result zero check
     */
    if (var1ndigits == 0)
    {
        zero_var(result);
        result->dscale = rscale;
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /*
     * Determine the result sign, weight and number of digits to calculate
     */
    if (var1->sign == var2->sign)
        res_sign = NUMERIC_POS;
    else
        res_sign = NUMERIC_NEG;
    res_weight = var1->weight - var2->weight + 1;
    /* The number of accurate result digits we need to produce: */
    div_ndigits = res_weight + 1 + (rscale + DEC_DIGITS - 1) / DEC_DIGITS;
    /* Add guard digits for roundoff error */
    div_ndigits += DIV_GUARD_DIGITS;
    if (div_ndigits < DIV_GUARD_DIGITS)
        div_ndigits = DIV_GUARD_DIGITS;
    /* Must be at least var1ndigits, too, to simplify data-loading loop */
    if (div_ndigits < var1ndigits)
        div_ndigits = var1ndigits;

    /*
     * We do the arithmetic in an array "div[]" of signed int's.  Since
     * INT_MAX is noticeably larger than NBASE*NBASE, this gives us headroom
     * to avoid normalizing carries immediately.
     *
     * We start with div[] containing one zero digit followed by the
     * dividend's digits (plus appended zeroes to reach the desired precision
     * including guard digits).  Each step of the main loop computes an
     * (approximate) quotient digit and stores it into div[], removing one
     * position of dividend space.  A final pass of carry propagation takes
     * care of any mistaken quotient digits.
     */
    div = (int *) calloc(sizeof(int), div_ndigits + 1);
    for (i = 0; i < var1ndigits; i++)
        div[i + 1] = var1digits[i];

    /*
     * We estimate each quotient digit using floating-point arithmetic, taking
     * the first four digits of the (current) dividend and divisor. This must
     * be float to avoid overflow.
     */
    fdivisor = (double) var2digits[0];
    for (i = 1; i < 4; i++)
    {
        fdivisor *= NBASE;
        if (i < var2ndigits)
            fdivisor += (double) var2digits[i];
    }
    fdivisorinverse = 1.0 / fdivisor;

    /*
     * maxdiv tracks the maximum possible absolute value of any div[] entry;
     * when this threatens to exceed INT_MAX, we take the time to propagate
     * carries.  To avoid overflow in maxdiv itself, it actually represents
     * the max possible abs. value divided by NBASE-1.
     */
    maxdiv = 1;

    /*
     * Outer loop computes next quotient digit, which will go into div[qi]
     */
    for (qi = 0; qi < div_ndigits; qi++)
    {
        /* Approximate the current dividend value */
        fdividend = (double) div[qi];
        for (i = 1; i < 4; i++)
        {
            fdividend *= NBASE;
            if (qi + i <= div_ndigits)
                fdividend += (double) div[qi + i];
        }
        /* Compute the (approximate) quotient digit */
        fquotient = fdividend * fdivisorinverse;
        qdigit = (fquotient >= 0.0) ? ((int) fquotient) :
            (((int) fquotient) - 1);    /* truncate towards -infinity */

        if (qdigit != 0)
        {
            /* Do we need to normalize now? */
            maxdiv += Abs(qdigit);
            if (maxdiv > INT_MAX / (NBASE - 1))
            {
                /* Yes, do it */
                carry = 0;
                for (i = div_ndigits; i > qi; i--)
                {
                    newdig = div[i] + carry;
                    if (newdig < 0)
                    {
                        carry = -((-newdig - 1) / NBASE) - 1;
                        newdig -= carry * NBASE;
                    }
                    else if (newdig >= NBASE)
                    {
                        carry = newdig / NBASE;
                        newdig -= carry * NBASE;
                    }
                    else
                        carry = 0;
                    div[i] = newdig;
                }
                newdig = div[qi] + carry;
                div[qi] = newdig;

                /*
                 * All the div[] digits except possibly div[qi] are now in the
                 * range 0..NBASE-1.
                 */
                maxdiv = Abs(newdig) / (NBASE - 1);
                maxdiv = Max(maxdiv, 1);

                /*
                 * Recompute the quotient digit since new info may have
                 * propagated into the top four dividend digits
                 */
                fdividend = (double) div[qi];
                for (i = 1; i < 4; i++)
                {
                    fdividend *= NBASE;
                    if (qi + i <= div_ndigits)
                        fdividend += (double) div[qi + i];
                }
                /* Compute the (approximate) quotient digit */
                fquotient = fdividend * fdivisorinverse;
                qdigit = (fquotient >= 0.0) ? ((int) fquotient) :
                    (((int) fquotient) - 1);    /* truncate towards -infinity */
                maxdiv += Abs(qdigit);
            }

            /* Subtract off the appropriate multiple of the divisor */
            if (qdigit != 0)
            {
                int         istop = Min(var2ndigits, div_ndigits - qi + 1);

                for (i = 0; i < istop; i++)
                    div[qi + i] -= qdigit * var2digits[i];
            }
        }

        /*
         * The dividend digit we are about to replace might still be nonzero.
         * Fold it into the next digit position.  We don't need to worry about
         * overflow here since this should nearly cancel with the subtraction
         * of the divisor.
         */
        div[qi + 1] += div[qi] * NBASE;

        div[qi] = qdigit;
    }

    /*
     * Approximate and store the last quotient digit (div[div_ndigits])
     */
    fdividend = (double) div[qi];
    for (i = 1; i < 4; i++)
        fdividend *= NBASE;
    fquotient = fdividend * fdivisorinverse;
    qdigit = (fquotient >= 0.0) ? ((int) fquotient) :
        (((int) fquotient) - 1);    /* truncate towards -infinity */
    div[qi] = qdigit;

    /*
     * Now we do a final carry propagation pass to normalize the result, which
     * we combine with storing the result digits into the output. Note that
     * this is still done at full precision w/guard digits.
     */
    alloc_var(result, div_ndigits + 1);
    res_digits = result->digits;
    carry = 0;
    for (i = div_ndigits; i >= 0; i--)
    {
        newdig = div[i] + carry;
        if (newdig < 0)
        {
            carry = -((-newdig - 1) / NBASE) - 1;
            newdig -= carry * NBASE;
        }
        else if (newdig >= NBASE)
        {
            carry = newdig / NBASE;
            newdig -= carry * NBASE;
        }
        else
            carry = 0;
        res_digits[i] = newdig;
    }
    Assert(carry == 0);

    free(div);

    /*
     * Finally, round the result to the requested precision.
     */
    result->weight = res_weight;
    result->sign = res_sign;

    /* Round to target rscale (and set result->dscale) */
    if (round)
        round_var(result, rscale);
    else
        trunc_var(result, rscale);

    /* Strip leading and trailing zeroes */
    strip_var(result);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * Default scale selection for division
 *
 * Returns the appropriate result scale for the division result.
 */
static int
select_div_scale(const numeric *var1, const numeric *var2)
{
    int         weight1,
                weight2,
                qweight,
                i;
    NumericDigit firstdigit1,
                firstdigit2;
    int         rscale;

    /*
     * The result scale of a division isn't specified in any SQL standard. For
     * PostgreSQL we select a result scale that will give at least
     * NUMERIC_MIN_SIG_DIGITS significant digits, so that numeric gives a
     * result no less accurate than double; but use a scale not less than
     * either input's display scale.
     */

    /* Get the actual (normalized) weight and first digit of each input */

    weight1 = 0;                /* values to use if var1 is zero */
    firstdigit1 = 0;
    for (i = 0; i < var1->ndigits; i++)
    {
        firstdigit1 = var1->digits[i];
        if (firstdigit1 != 0)
        {
            weight1 = var1->weight - i;
            break;
        }
    }

    weight2 = 0;                /* values to use if var2 is zero */
    firstdigit2 = 0;
    for (i = 0; i < var2->ndigits; i++)
    {
        firstdigit2 = var2->digits[i];
        if (firstdigit2 != 0)
        {
            weight2 = var2->weight - i;
            break;
        }
    }

    /*
     * Estimate weight of quotient.  If the two first digits are equal, we
     * can't be sure, but assume that var1 is less than var2.
     */
    qweight = weight1 - weight2;
    if (firstdigit1 <= firstdigit2)
        qweight--;

    /* Select result scale */
    rscale = NUMERIC_MIN_SIG_DIGITS - qweight * DEC_DIGITS;
    rscale = Max(rscale, var1->dscale);
    rscale = Max(rscale, var2->dscale);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

    return rscale;
}


/*
 * mod_var() -
 *
 *  Calculate the modulo of two numerics at variable level
 */
static numeric_errcode_t
mod_var(const numeric *var1, const numeric *var2, numeric *result)
{
    numeric  tmp;
    numeric_errcode_t errcode;

    numeric_init(&tmp);

    /* ---------
     * We do this using the equation
     *      mod(x,y) = x - trunc(x/y)*y
     * div_var can be persuaded to give us trunc(x/y) directly.
     * ----------
     */
    errcode = div_var(var1, var2, &tmp, 0, false);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    mul_var(var2, &tmp, &tmp, var2->dscale);

    sub_var(var1, &tmp, result);

    numeric_dispose(&tmp);
    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * ceil_var() -
 *
 *  Return the smallest integer greater than or equal to the argument
 *  on variable level
 */
static void
ceil_var(const numeric *var, numeric *result)
{
    numeric  tmp;

    numeric_init(&tmp);
    set_var_from_var(var, &tmp);

    trunc_var(&tmp, 0);

    if (var->sign == NUMERIC_POS && cmp_var(var, &tmp) != 0)
        add_var(&tmp, &const_one, &tmp);

    set_var_from_var(&tmp, result);
    numeric_dispose(&tmp);
}


/*
 * floor_var() -
 *
 *  Return the largest integer equal to or less than the argument
 *  on variable level
 */
static void
floor_var(const numeric *var, numeric *result)
{
    numeric  tmp;

    numeric_init(&tmp);
    set_var_from_var(var, &tmp);

    trunc_var(&tmp, 0);

    if (var->sign == NUMERIC_NEG && cmp_var(var, &tmp) != 0)
        sub_var(&tmp, &const_one, &tmp);

    set_var_from_var(&tmp, result);
    numeric_dispose(&tmp);
}


/*
 * sqrt_var() -
 *
 *  Compute the square root of x using Newton's algorithm
 */
static numeric_errcode_t
sqrt_var(const numeric *arg, numeric *result, int rscale)
{
    numeric  tmp_arg;
    numeric  tmp_val;
    numeric  last_val;
    int         local_rscale;
    int         stat;

    local_rscale = rscale + 8;

    stat = cmp_var(arg, &const_zero);
    if (stat == 0)
    {
        zero_var(result);
        result->dscale = rscale;
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    /*
     * SQL2003 defines sqrt() in terms of power, so we need to emit the right
     * SQLSTATE error code if the operand is negative.
     */
    if (stat < 0)
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    numeric_init(&tmp_arg);
    numeric_init(&tmp_val);
    numeric_init(&last_val);

    /* Copy arg in case it is the same var as result */
    set_var_from_var(arg, &tmp_arg);

    /*
     * Initialize the result to the first guess
     */
    alloc_var(result, 1);
    result->digits[0] = tmp_arg.digits[0] / 2;
    if (result->digits[0] == 0)
        result->digits[0] = 1;
    result->weight = tmp_arg.weight / 2;
    result->sign = NUMERIC_POS;

    set_var_from_var(result, &last_val);

    for (;;)
    {
        div_var_fast(&tmp_arg, result, &tmp_val, local_rscale, true);

        add_var(result, &tmp_val, result);
        mul_var(result, &const_zero_point_five, result, local_rscale);

        if (cmp_var(&last_val, result) == 0)
            break;
        set_var_from_var(result, &last_val);
    }

    numeric_dispose(&last_val);
    numeric_dispose(&tmp_val);
    numeric_dispose(&tmp_arg);

    /* Round to requested precision */
    round_var(result, rscale);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * exp_var() -
 *
 *  Raise e to the power of x
 */
static numeric_errcode_t
exp_var(const numeric *arg, numeric *result, int rscale)
{
    numeric  x;
    int         xintval;
    bool        xneg = false;
    int         local_rscale;

    /*----------
     * We separate the integral and fraction parts of x, then compute
     *      e^x = e^xint * e^xfrac
     * where e = exp(1) and e^xfrac = exp(xfrac) are computed by
     * exp_var_internal; the limited range of inputs allows that routine
     * to do a good job with a simple Taylor series.  Raising e^xint is
     * done by repeated multiplications in power_var_int.
     *----------
     */
    numeric_init(&x);

    set_var_from_var(arg, &x);

    if (x.sign == NUMERIC_NEG)
    {
        xneg = true;
        x.sign = NUMERIC_POS;
    }

    /* Extract the integer part, remove it from x */
    xintval = 0;
    while (x.weight >= 0)
    {
        xintval *= NBASE;
        if (x.ndigits > 0)
        {
            xintval += x.digits[0];
            x.digits++;
            x.ndigits--;
        }
        x.weight--;
        /* Guard against overflow */
        if (xintval >= NUMERIC_MAX_RESULT_SCALE * 3)
            return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;
    }

    /* Select an appropriate scale for internal calculation */
    local_rscale = rscale + MUL_GUARD_DIGITS * 2;

    /* Compute e^xfrac */
    exp_var_internal(&x, result, local_rscale);

    /* If there's an integer part, multiply by e^xint */
    if (xintval > 0)
    {
        numeric  e;

        numeric_init(&e);
        exp_var_internal(&const_one, &e, local_rscale);
        power_var_int(&e, xintval, &e, local_rscale);
        mul_var(&e, result, result, local_rscale);
        numeric_dispose(&e);
    }

    /* Compensate for input sign, and round to requested rscale */
    if (xneg)
        div_var_fast(&const_one, result, result, rscale, true);
    else
        round_var(result, rscale);

    numeric_dispose(&x);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * exp_var_internal() -
 *
 *  Raise e to the power of x, where 0 <= x <= 1
 *
 * NB: the result should be good to at least rscale digits, but it has
 * *not* been rounded off; the caller must do that if wanted.
 */
static void
exp_var_internal(const numeric *arg, numeric *result, int rscale)
{
    numeric  x;
    numeric  xpow;
    numeric  ifac;
    numeric  elem;
    numeric  ni;
    int         ndiv2 = 0;
    int         local_rscale;

    numeric_init(&x);
    numeric_init(&xpow);
    numeric_init(&ifac);
    numeric_init(&elem);
    numeric_init(&ni);

    set_var_from_var(arg, &x);

    Assert(x.sign == NUMERIC_POS);

    local_rscale = rscale + 8;

    /* Reduce input into range 0 <= x <= 0.01 */
    while (cmp_var(&x, &const_zero_point_01) > 0)
    {
        ndiv2++;
        local_rscale++;
        mul_var(&x, &const_zero_point_five, &x, x.dscale + 1);
    }

    /*
     * Use the Taylor series
     *
     * exp(x) = 1 + x + x^2/2! + x^3/3! + ...
     *
     * Given the limited range of x, this should converge reasonably quickly.
     * We run the series until the terms fall below the local_rscale limit.
     */
    add_var(&const_one, &x, result);
    set_var_from_var(&x, &xpow);
    set_var_from_var(&const_one, &ifac);
    set_var_from_var(&const_one, &ni);

    for (;;)
    {
        add_var(&ni, &const_one, &ni);
        mul_var(&xpow, &x, &xpow, local_rscale);
        mul_var(&ifac, &ni, &ifac, 0);
        div_var_fast(&xpow, &ifac, &elem, local_rscale, true);

        if (elem.ndigits == 0)
            break;

        add_var(result, &elem, result);
    }

    /* Compensate for argument range reduction */
    while (ndiv2-- > 0)
        mul_var(result, result, result, local_rscale);

    numeric_dispose(&x);
    numeric_dispose(&xpow);
    numeric_dispose(&ifac);
    numeric_dispose(&elem);
    numeric_dispose(&ni);
}


/*
 * ln_var() -
 *
 *  Compute the natural log of x
 */
static numeric_errcode_t
ln_var(const numeric *arg, numeric *result, int rscale)
{
    numeric  x;
    numeric  xx;
    numeric  ni;
    numeric  elem;
    numeric  fact;
    int         local_rscale;
    int         cmp;

    cmp = cmp_var(arg, &const_zero);
    if (cmp <= 0)
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    local_rscale = rscale + 8;

    numeric_init(&x);
    numeric_init(&xx);
    numeric_init(&ni);
    numeric_init(&elem);
    numeric_init(&fact);

    set_var_from_var(arg, &x);
    set_var_from_var(&const_two, &fact);

    /* Reduce input into range 0.9 < x < 1.1 */
    while (cmp_var(&x, &const_zero_point_nine) <= 0)
    {
        local_rscale++;
        sqrt_var(&x, &x, local_rscale);
        mul_var(&fact, &const_two, &fact, 0);
    }
    while (cmp_var(&x, &const_one_point_one) >= 0)
    {
        local_rscale++;
        sqrt_var(&x, &x, local_rscale);
        mul_var(&fact, &const_two, &fact, 0);
    }

    /*
     * We use the Taylor series for 0.5 * ln((1+z)/(1-z)),
     *
     * z + z^3/3 + z^5/5 + ...
     *
     * where z = (x-1)/(x+1) is in the range (approximately) -0.053 .. 0.048
     * due to the above range-reduction of x.
     *
     * The convergence of this is not as fast as one would like, but is
     * tolerable given that z is small.
     */
    sub_var(&x, &const_one, result);
    add_var(&x, &const_one, &elem);
    div_var_fast(result, &elem, result, local_rscale, true);
    set_var_from_var(result, &xx);
    mul_var(result, result, &x, local_rscale);

    set_var_from_var(&const_one, &ni);

    for (;;)
    {
        add_var(&ni, &const_two, &ni);
        mul_var(&xx, &x, &xx, local_rscale);
        div_var_fast(&xx, &ni, &elem, local_rscale, true);

        if (elem.ndigits == 0)
            break;

        add_var(result, &elem, result);

        if (elem.weight < (result->weight - local_rscale * 2 / DEC_DIGITS))
            break;
    }

    /* Compensate for argument range reduction, round to requested rscale */
    mul_var(result, &fact, result, rscale);

    numeric_dispose(&x);
    numeric_dispose(&xx);
    numeric_dispose(&ni);
    numeric_dispose(&elem);
    numeric_dispose(&fact);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * log_var() -
 *
 *  Compute the logarithm of num in a given base.
 *
 *  Note: this routine chooses dscale of the result.
 */
static numeric_errcode_t
log_var(const numeric *base, const numeric *num, numeric *result)
{
    numeric  ln_base;
    numeric  ln_num;
    int         dec_digits;
    int         rscale;
    int         local_rscale;
    numeric_errcode_t errcode;

    numeric_init(&ln_base);
    numeric_init(&ln_num);

    /* Set scale for ln() calculations --- compare numeric_ln() */

    /* Approx decimal digits before decimal point */
    dec_digits = (num->weight + 1) * DEC_DIGITS;

    if (dec_digits > 1)
        rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(dec_digits - 1);
    else if (dec_digits < 1)
        rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(1 - dec_digits);
    else
        rscale = NUMERIC_MIN_SIG_DIGITS;

    rscale = Max(rscale, base->dscale);
    rscale = Max(rscale, num->dscale);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

    local_rscale = rscale + 8;

    /* Form natural logarithms */
    errcode = ln_var(base, &ln_base, local_rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;
    errcode = ln_var(num, &ln_num, local_rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    ln_base.dscale = rscale;
    ln_num.dscale = rscale;

    /* Select scale for division result */
    rscale = select_div_scale(&ln_num, &ln_base);

    div_var_fast(&ln_num, &ln_base, result, rscale, true);

    numeric_dispose(&ln_num);
    numeric_dispose(&ln_base);

    return NUMERIC_ERRCODE_NO_ERROR;
}


/*
 * power_var() -
 *
 *  Raise base to the power of exp
 *
 *  Note: this routine chooses dscale of the result.
 */
static numeric_errcode_t
power_var(const numeric *base, const numeric *exp, numeric *result)
{
    numeric  ln_base;
    numeric  ln_num;
    int         dec_digits;
    int         rscale;
    int         local_rscale;
    double      val;
    numeric_errcode_t errcode;

    /* If exp can be represented as an integer, use power_var_int */
    if (exp->ndigits == 0 || exp->ndigits <= exp->weight + 1)
    {
        /* exact integer, but does it fit in int? */
        numeric  x;
        int64_t       expval64;

        /* must copy because numericvar_to_int64() scribbles on input */
        numeric_init(&x);
        set_var_from_var(exp, &x);
        if (numericvar_to_int64(&x, &expval64))
        {
            int         expval = (int) expval64;

            /* Test for overflow by reverse-conversion. */
            if ((int64_t) expval == expval64)
            {
                /* Okay, select rscale */
                rscale = NUMERIC_MIN_SIG_DIGITS;
                rscale = Max(rscale, base->dscale);
                rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
                rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

                power_var_int(base, expval, result, rscale);

                numeric_dispose(&x);
                return NUMERIC_ERRCODE_NO_ERROR;
            }
        }
        numeric_dispose(&x);
    }

    /*
     * This avoids log(0) for cases of 0 raised to a non-integer. 0 ^ 0
     * handled by power_var_int().
     */
    if (cmp_var(base, &const_zero) == 0)
    {
        set_var_from_var(&const_zero, result);
        result->dscale = NUMERIC_MIN_SIG_DIGITS;        /* no need to round */
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    numeric_init(&ln_base);
    numeric_init(&ln_num);

    /* Set scale for ln() calculation --- need extra accuracy here */

    /* Approx decimal digits before decimal point */
    dec_digits = (base->weight + 1) * DEC_DIGITS;

    if (dec_digits > 1)
        rscale = NUMERIC_MIN_SIG_DIGITS * 2 - (int) log10(dec_digits - 1);
    else if (dec_digits < 1)
        rscale = NUMERIC_MIN_SIG_DIGITS * 2 - (int) log10(1 - dec_digits);
    else
        rscale = NUMERIC_MIN_SIG_DIGITS * 2;

    rscale = Max(rscale, base->dscale * 2);
    rscale = Max(rscale, exp->dscale * 2);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE * 2);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE * 2);

    local_rscale = rscale + 8;

    errcode = ln_var(base, &ln_base, local_rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    mul_var(&ln_base, exp, &ln_num, local_rscale);

    /* Set scale for exp() -- compare numeric_exp() */

    /* convert input to double, ignoring overflow */
    errcode = numericvar_to_double_no_overflow(&ln_num, &val);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    /*
     * log10(result) = num * log10(e), so this is approximately the weight:
     */
    val *= 0.434294481903252;

    /* limit to something that won't cause integer overflow */
    val = Max(val, -NUMERIC_MAX_RESULT_SCALE);
    val = Min(val, NUMERIC_MAX_RESULT_SCALE);

    rscale = NUMERIC_MIN_SIG_DIGITS - (int) val;
    rscale = Max(rscale, base->dscale);
    rscale = Max(rscale, exp->dscale);
    rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

    errcode = exp_var(&ln_num, result, rscale);
    if (errcode != NUMERIC_ERRCODE_NO_ERROR)
        return errcode;

    numeric_dispose(&ln_num);
    numeric_dispose(&ln_base);

    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 * power_var_int() -
 *
 *  Raise base to the power of exp, where exp is an integer.
 */
static void
power_var_int(const numeric *base, int exp, numeric *result, int rscale)
{
    bool        neg;
    numeric  base_prod;
    int         local_rscale;

    switch (exp)
    {
        case 0:

            /*
             * While 0 ^ 0 can be either 1 or indeterminate (error), we treat
             * it as 1 because most programming languages do this. SQL:2003
             * also requires a return value of 1.
             * http://en.wikipedia.org/wiki/Exponentiation#Zero_to_the_zero_pow
             * er
             */
            set_var_from_var(&const_one, result);
            result->dscale = rscale;    /* no need to round */
            return;
        case 1:
            set_var_from_var(base, result);
            round_var(result, rscale);
            return;
        case -1:
            div_var(&const_one, base, result, rscale, true);
            return;
        case 2:
            mul_var(base, base, result, rscale);
            return;
        default:
            break;
    }

    /*
     * The general case repeatedly multiplies base according to the bit
     * pattern of exp.  We do the multiplications with some extra precision.
     */
    neg = (exp < 0);
    exp = Abs(exp);

    local_rscale = rscale + MUL_GUARD_DIGITS * 2;

    numeric_init(&base_prod);
    set_var_from_var(base, &base_prod);

    if (exp & 1)
        set_var_from_var(base, result);
    else
        set_var_from_var(&const_one, result);

    while ((exp >>= 1) > 0)
    {
        mul_var(&base_prod, &base_prod, &base_prod, local_rscale);
        if (exp & 1)
            mul_var(&base_prod, result, result, local_rscale);
    }

    numeric_dispose(&base_prod);

    /* Compensate for input sign, and round to requested rscale */
    if (neg)
        div_var_fast(&const_one, result, result, rscale, true);
    else
        round_var(result, rscale);
}


/* ----------------------------------------------------------------------
 *
 * Following are the lowest level functions that operate unsigned
 * on the variable level
 *
 * ----------------------------------------------------------------------
 */


/* ----------
 * cmp_abs() -
 *
 *  Compare the absolute values of var1 and var2
 *  Returns:    -1 for ABS(var1) < ABS(var2)
 *              0  for ABS(var1) == ABS(var2)
 *              1  for ABS(var1) > ABS(var2)
 * ----------
 */
static int
cmp_abs(const numeric *var1, const numeric *var2)
{
    return cmp_abs_common(var1->digits, var1->ndigits, var1->weight,
                          var2->digits, var2->ndigits, var2->weight);
}

/* ----------
 * cmp_abs_common() -
 *
 *  Main routine of cmp_abs(). This function can be used by both
 *  numeric and Numeric.
 * ----------
 */
static int
cmp_abs_common(const NumericDigit *var1digits, int var1ndigits, int var1weight,
             const NumericDigit *var2digits, int var2ndigits, int var2weight)
{
    int         i1 = 0;
    int         i2 = 0;

    /* Check any digits before the first common digit */

    while (var1weight > var2weight && i1 < var1ndigits)
    {
        if (var1digits[i1++] != 0)
            return 1;
        var1weight--;
    }
    while (var2weight > var1weight && i2 < var2ndigits)
    {
        if (var2digits[i2++] != 0)
            return -1;
        var2weight--;
    }

    /* At this point, either w1 == w2 or we've run out of digits */

    if (var1weight == var2weight)
    {
        while (i1 < var1ndigits && i2 < var2ndigits)
        {
            int         stat = var1digits[i1++] - var2digits[i2++];

            if (stat)
            {
                if (stat > 0)
                    return 1;
                return -1;
            }
        }
    }

    /*
     * At this point, we've run out of digits on one side or the other; so any
     * remaining nonzero digits imply that side is larger
     */
    while (i1 < var1ndigits)
    {
        if (var1digits[i1++] != 0)
            return 1;
    }
    while (i2 < var2ndigits)
    {
        if (var2digits[i2++] != 0)
            return -1;
    }

    return 0;
}


/*
 * add_abs() -
 *
 *  Add the absolute values of two variables into result.
 *  result might point to one of the operands without danger.
 */
static void
add_abs(const numeric *var1, const numeric *var2, numeric *result)
{
    NumericDigit *res_buf;
    NumericDigit *res_digits;
    int         res_ndigits;
    int         res_weight;
    int         res_rscale,
                rscale1,
                rscale2;
    int         res_dscale;
    int         i,
                i1,
                i2;
    int         carry = 0;

    /* copy these values into local vars for speed in inner loop */
    int         var1ndigits = var1->ndigits;
    int         var2ndigits = var2->ndigits;
    NumericDigit *var1digits = var1->digits;
    NumericDigit *var2digits = var2->digits;

    res_weight = Max(var1->weight, var2->weight) + 1;

    res_dscale = Max(var1->dscale, var2->dscale);

    /* Note: here we are figuring rscale in base-NBASE digits */
    rscale1 = var1->ndigits - var1->weight - 1;
    rscale2 = var2->ndigits - var2->weight - 1;
    res_rscale = Max(rscale1, rscale2);

    res_ndigits = res_rscale + res_weight + 1;
    if (res_ndigits <= 0)
        res_ndigits = 1;

    res_buf = digitbuf_alloc(res_ndigits + 1);
    res_buf[0] = 0;             /* spare digit for later rounding */
    res_digits = res_buf + 1;

    i1 = res_rscale + var1->weight + 1;
    i2 = res_rscale + var2->weight + 1;
    for (i = res_ndigits - 1; i >= 0; i--)
    {
        i1--;
        i2--;
        if (i1 >= 0 && i1 < var1ndigits)
            carry += var1digits[i1];
        if (i2 >= 0 && i2 < var2ndigits)
            carry += var2digits[i2];

        if (carry >= NBASE)
        {
            res_digits[i] = carry - NBASE;
            carry = 1;
        }
        else
        {
            res_digits[i] = carry;
            carry = 0;
        }
    }

    Assert(carry == 0);         /* else we failed to allow for carry out */

    digitbuf_free(result->buf);
    result->ndigits = res_ndigits;
    result->buf = res_buf;
    result->digits = res_digits;
    result->weight = res_weight;
    result->dscale = res_dscale;

    /* Remove leading/trailing zeroes */
    strip_var(result);
}


/*
 * sub_abs()
 *
 *  Subtract the absolute value of var2 from the absolute value of var1
 *  and store in result. result might point to one of the operands
 *  without danger.
 *
 *  ABS(var1) MUST BE GREATER OR EQUAL ABS(var2) !!!
 */
static void
sub_abs(const numeric *var1, const numeric *var2, numeric *result)
{
    NumericDigit *res_buf;
    NumericDigit *res_digits;
    int         res_ndigits;
    int         res_weight;
    int         res_rscale,
                rscale1,
                rscale2;
    int         res_dscale;
    int         i,
                i1,
                i2;
    int         borrow = 0;

    /* copy these values into local vars for speed in inner loop */
    int         var1ndigits = var1->ndigits;
    int         var2ndigits = var2->ndigits;
    NumericDigit *var1digits = var1->digits;
    NumericDigit *var2digits = var2->digits;

    res_weight = var1->weight;

    res_dscale = Max(var1->dscale, var2->dscale);

    /* Note: here we are figuring rscale in base-NBASE digits */
    rscale1 = var1->ndigits - var1->weight - 1;
    rscale2 = var2->ndigits - var2->weight - 1;
    res_rscale = Max(rscale1, rscale2);

    res_ndigits = res_rscale + res_weight + 1;
    if (res_ndigits <= 0)
        res_ndigits = 1;

    res_buf = digitbuf_alloc(res_ndigits + 1);
    res_buf[0] = 0;             /* spare digit for later rounding */
    res_digits = res_buf + 1;

    i1 = res_rscale + var1->weight + 1;
    i2 = res_rscale + var2->weight + 1;
    for (i = res_ndigits - 1; i >= 0; i--)
    {
        i1--;
        i2--;
        if (i1 >= 0 && i1 < var1ndigits)
            borrow += var1digits[i1];
        if (i2 >= 0 && i2 < var2ndigits)
            borrow -= var2digits[i2];

        if (borrow < 0)
        {
            res_digits[i] = borrow + NBASE;
            borrow = -1;
        }
        else
        {
            res_digits[i] = borrow;
            borrow = 0;
        }
    }

    Assert(borrow == 0);        /* else caller gave us var1 < var2 */

    digitbuf_free(result->buf);
    result->ndigits = res_ndigits;
    result->buf = res_buf;
    result->digits = res_digits;
    result->weight = res_weight;
    result->dscale = res_dscale;

    /* Remove leading/trailing zeroes */
    strip_var(result);
}

/*
 * round_var
 *
 * Round the value of a variable to no more than rscale decimal digits
 * after the decimal point.  NOTE: we allow rscale < 0 here, implying
 * rounding before the decimal point.
 */
static void
round_var(numeric *var, int rscale)
{
    NumericDigit *digits = var->digits;
    int         di;
    int         ndigits;
    int         carry;

    var->dscale = rscale;

    /* decimal digits wanted */
    di = (var->weight + 1) * DEC_DIGITS + rscale;

    /*
     * If di = 0, the value loses all digits, but could round up to 1 if its
     * first extra digit is >= 5.  If di < 0 the result must be 0.
     */
    if (di < 0)
    {
        var->ndigits = 0;
        var->weight = 0;
        var->sign = NUMERIC_POS;
    }
    else
    {
        /* NBASE digits wanted */
        ndigits = (di + DEC_DIGITS - 1) / DEC_DIGITS;

        /* 0, or number of decimal digits to keep in last NBASE digit */
        di %= DEC_DIGITS;

        if (ndigits < var->ndigits ||
            (ndigits == var->ndigits && di > 0))
        {
            var->ndigits = ndigits;

#if DEC_DIGITS == 1
            /* di must be zero */
            carry = (digits[ndigits] >= HALF_NBASE) ? 1 : 0;
#else
            if (di == 0)
                carry = (digits[ndigits] >= HALF_NBASE) ? 1 : 0;
            else
            {
                /* Must round within last NBASE digit */
                int         extra,
                            pow10;

#if DEC_DIGITS == 4
                pow10 = round_powers[di];
#elif DEC_DIGITS == 2
                pow10 = 10;
#else
#error unsupported NBASE
#endif
                extra = digits[--ndigits] % pow10;
                digits[ndigits] -= extra;
                carry = 0;
                if (extra >= pow10 / 2)
                {
                    pow10 += digits[ndigits];
                    if (pow10 >= NBASE)
                    {
                        pow10 -= NBASE;
                        carry = 1;
                    }
                    digits[ndigits] = pow10;
                }
            }
#endif

            /* Propagate carry if needed */
            while (carry)
            {
                carry += digits[--ndigits];
                if (carry >= NBASE)
                {
                    digits[ndigits] = carry - NBASE;
                    carry = 1;
                }
                else
                {
                    digits[ndigits] = carry;
                    carry = 0;
                }
            }

            if (ndigits < 0)
            {
                Assert(ndigits == -1);  /* better not have added > 1 digit */
                Assert(var->digits > var->buf);
                var->digits--;
                var->ndigits++;
                var->weight++;
            }
        }
    }
}

/*
 * trunc_var
 *
 * Truncate (towards zero) the value of a variable at rscale decimal digits
 * after the decimal point.  NOTE: we allow rscale < 0 here, implying
 * truncation before the decimal point.
 */
static void
trunc_var(numeric *var, int rscale)
{
    int         di;
    int         ndigits;

    var->dscale = rscale;

    /* decimal digits wanted */
    di = (var->weight + 1) * DEC_DIGITS + rscale;

    /*
     * If di <= 0, the value loses all digits.
     */
    if (di <= 0)
    {
        var->ndigits = 0;
        var->weight = 0;
        var->sign = NUMERIC_POS;
    }
    else
    {
        /* NBASE digits wanted */
        ndigits = (di + DEC_DIGITS - 1) / DEC_DIGITS;

        if (ndigits <= var->ndigits)
        {
            var->ndigits = ndigits;

#if DEC_DIGITS == 1
            /* no within-digit stuff to worry about */
#else
            /* 0, or number of decimal digits to keep in last NBASE digit */
            di %= DEC_DIGITS;

            if (di > 0)
            {
                /* Must truncate within last NBASE digit */
                NumericDigit *digits = var->digits;
                int         extra,
                            pow10;

#if DEC_DIGITS == 4
                pow10 = round_powers[di];
#elif DEC_DIGITS == 2
                pow10 = 10;
#else
#error unsupported NBASE
#endif
                extra = digits[--ndigits] % pow10;
                digits[ndigits] -= extra;
            }
#endif
        }
    }
}

/*
 * strip_var
 *
 * Strip any leading and trailing zeroes from a numeric variable
 */
static void
strip_var(numeric *var)
{
    NumericDigit *digits = var->digits;
    int         ndigits = var->ndigits;

    /* Strip leading zeroes */
    while (ndigits > 0 && *digits == 0)
    {
        digits++;
        var->weight--;
        ndigits--;
    }

    /* Strip trailing zeroes */
    while (ndigits > 0 && digits[ndigits - 1] == 0)
        ndigits--;

    /* If it's zero, normalize the sign and weight */
    if (ndigits == 0)
    {
        var->sign = NUMERIC_POS;
        var->weight = 0;
    }

    var->digits = digits;
    var->ndigits = ndigits;
}
