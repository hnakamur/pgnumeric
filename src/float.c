/*-------------------------------------------------------------------------
 *
 * float.c
 *    Functions for the built-in floating-point types.
 *
 * Modified in 2011 for standalone use by Hiroaki Nakamura.
 * Portions Copyright (c) 1996-2010, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *    $PostgreSQL: pgsql/src/backend/utils/adt/float.c,v 1.166 2010/02/27 21:53:21 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <limits.h>

#include "numeric.h"

extern int pg_strncasecmp(const char *s1, const char *s2, size_t n);

/* Visual C++ etc lacks NAN, and won't accept 0.0/0.0.  NAN definition from
 * http://msdn.microsoft.com/library/default.asp?url=/library/en-us/vclang/html/vclrfNotNumberNANItems.asp
 */
#if defined(WIN32) && !defined(NAN)
static const uint32_t nan[2] = {0xffffffff, 0x7fffffff};

#define NAN (*(const double *) nan)
#endif

/* not sure what the following should be, but better to make it over-sufficient */
#define MAXFLOATWIDTH   64
#define MAXDOUBLEWIDTH  128

/*
 * check to see if a float/double val has underflowed or overflowed
 */
#define CHECKFLOATVAL(val, inf_is_valid, zero_is_valid)         \
do {                                                            \
    if (isinf(val) && !(inf_is_valid))                          \
        return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;      \
                                                                \
    if ((val) == 0.0 && !(zero_is_valid))                       \
        return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;      \
} while(0)


/* ========== USER I/O ROUTINES ========== */


/* Configurable GUC parameter */
static int extra_float_digits = 0;      /* Added to DBL_DIG or FLT_DIG */


/*
 * Routines to provide reasonably platform-independent handling of
 * infinity and NaN.  We assume that isinf() and isnan() are available
 * and work per spec.  (On some platforms, we have to supply our own;
 * see src/port.)  However, generating an Infinity or NaN in the first
 * place is less well standardized; pre-C99 systems tend not to have C99's
 * INFINITY and NAN macros.  We centralize our workarounds for this here.
 */

double
get_double_infinity(void)
{
#ifdef INFINITY
	/* C99 standard way */
	return (double) INFINITY;
#else

	/*
	 * On some platforms, HUGE_VAL is an infinity, elsewhere it's just the
	 * largest normal double.  We assume forcing an overflow will get us a
	 * true infinity.
	 */
	return (double) (HUGE_VAL * HUGE_VAL);
#endif
}

float
get_float_infinity(void)
{
#ifdef INFINITY
	/* C99 standard way */
	return (float) INFINITY;
#else

	/*
	 * On some platforms, HUGE_VAL is an infinity, elsewhere it's just the
	 * largest normal double.  We assume forcing an overflow will get us a
	 * true infinity.
	 */
	return (float) (HUGE_VAL * HUGE_VAL);
#endif
}

double
get_double_nan(void)
{
    /* (double) NAN doesn't work on some NetBSD/MIPS releases */
#if defined(NAN) && !(defined(__NetBSD__) && defined(__mips__))
    /* C99 standard way */
    return (double) NAN;
#else
    /* Assume we can get a NAN via zero divide */
    return (double) (0.0 / 0.0);
#endif
}

float
get_float_nan(void)
{
#ifdef NAN
    /* C99 standard way */
    return (float) NAN;
#else
    /* Assume we can get a NAN via zero divide */
    return (float) (0.0 / 0.0);
#endif
}



/*
 * Returns -1 if 'val' represents negative infinity, 1 if 'val'
 * represents (positive) infinity, and 0 otherwise. On some platforms,
 * this is equivalent to the isinf() macro, but not everywhere: C99
 * does not specify that isinf() needs to distinguish between positive
 * and negative infinity.
 */
int
is_infinite(double val)
{
	int			inf = isinf(val);

	if (inf == 0)
		return 0;
	else if (val > 0)
		return 1;
	else
		return -1;
}

/*
 *      float_in        - converts "num" to float
 *                        restricted syntax:
 *                        {<sp>} [+|-] {digit} [.{digit}] [<exp>]
 *                        where <sp> is a space, digit is 0-9,
 *                        <exp> is "e" or "E" followed by an integer.
 */
numeric_errcode_t
float_in(const char *num, float *result)
{
    const char *orig_num;
    double      val;
    const char *endptr;

    /*
     * endptr points to the first character _after_ the sequence we recognized
     * as a valid floating point number. orig_num points to the original input
     * string.
     */
    orig_num = num;

    /*
     * Check for an empty-string input to begin with, to avoid the vagaries of
     * strtod() on different platforms.
     */
    if (*num == '\0')
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    /* skip leading whitespace */
    while (*num != '\0' && isspace((unsigned char) *num))
        num++;

    errno = 0;
    val = strtod(num, (char **)&endptr);

    /* did we not see anything that looks like a double? */
    if (endptr == num || errno != 0)
    {
        /*
         * C99 requires that strtod() accept NaN and [-]Infinity, but not all
         * platforms support that yet (and some accept them but set ERANGE
         * anyway...)  Therefore, we check for these inputs ourselves.
         */
        if (pg_strncasecmp(num, "NaN", 3) == 0)
        {
            val = get_float_nan();
            endptr = num + 3;
        }
        else if (pg_strncasecmp(num, "Infinity", 8) == 0)
        {
            val = get_float_infinity();
            endptr = num + 8;
        }
        else if (pg_strncasecmp(num, "-Infinity", 9) == 0)
        {
            val = -get_float_infinity();
            endptr = num + 9;
        }
        else if (errno == ERANGE)
            return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;
        else
            return NUMERIC_ERRCODE_INVALID_ARGUMENT;
    }
#ifdef HAVE_BUGGY_SOLARIS_STRTOD
    else
    {
        /*
         * Many versions of Solaris have a bug wherein strtod sets endptr to
         * point one byte beyond the end of the string when given "inf" or
         * "infinity".
         */
        if (endptr != num && endptr[-1] == '\0')
            endptr--;
    }
#endif   /* HAVE_BUGGY_SOLARIS_STRTOD */

#ifdef HAVE_BUGGY_IRIX_STRTOD

    /*
     * In some IRIX versions, strtod() recognizes only "inf", so if the input
     * is "infinity" we have to skip over "inity".  Also, it may return
     * positive infinity for "-inf".
     */
    if (isinf(val))
    {
        if (pg_strncasecmp(num, "Infinity", 8) == 0)
        {
            val = get_float_infinity();
            endptr = num + 8;
        }
        else if (pg_strncasecmp(num, "-Infinity", 9) == 0)
        {
            val = -get_float_infinity();
            endptr = num + 9;
        }
        else if (pg_strncasecmp(num, "-inf", 4) == 0)
        {
            val = -get_float_infinity();
            endptr = num + 4;
        }
    }
#endif   /* HAVE_BUGGY_IRIX_STRTOD */

    /* skip trailing whitespace */
    while (*endptr != '\0' && isspace((unsigned char) *endptr))
        endptr++;

    /* if there is any junk left at the end of the string, bail out */
    if (*endptr != '\0')
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    /*
     * if we get here, we have a legal double, still need to check to see if
     * it's a legal float
     */
    CHECKFLOATVAL((float) val, isinf(val), val == 0);

    *result = (float) val;
    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 *      float_out       - converts a float number to a string
 *                        using a standard output format
 */
numeric_errcode_t
float_out(float num, char **result)
{
    if (isnan(num))
    {
        *result = strdup("NaN");
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    switch (is_infinite(num))
    {
        case 1:
            *result = strdup("Infinity");
            break;
        case -1:
            *result = strdup("-Infinity");
            break;
        default:
            {
                int         ndig = FLT_DIG + extra_float_digits;
                char       *ascii = (char *) malloc(MAXFLOATWIDTH + 1);

                if (ndig < 1)
                    ndig = 1;

                snprintf(ascii, MAXFLOATWIDTH + 1, "%.*g", ndig, num);
                *result = ascii;
            }
    }

    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 *      double_in       - converts "num" to double
 *                        restricted syntax:
 *                        {<sp>} [+|-] {digit} [.{digit}] [<exp>]
 *                        where <sp> is a space, digit is 0-9,
 *                        <exp> is "e" or "E" followed by an integer.
 */
numeric_errcode_t
double_in(const char *num, double *result)
{
    const char *orig_num;
    double      val;
    const char *endptr;

    /*
     * endptr points to the first character _after_ the sequence we recognized
     * as a valid floating point number. orig_num points to the original input
     * string.
     */
    orig_num = num;

    /*
     * Check for an empty-string input to begin with, to avoid the vagaries of
     * strtod() on different platforms.
     */
    if (*num == '\0')
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    /* skip leading whitespace */
    while (*num != '\0' && isspace((unsigned char) *num))
        num++;

    errno = 0;
    val = strtod(num, (char **)&endptr);

    /* did we not see anything that looks like a double? */
    if (endptr == num || errno != 0)
    {
        /*
         * C99 requires that strtod() accept NaN and [-]Infinity, but not all
         * platforms support that yet (and some accept them but set ERANGE
         * anyway...)  Therefore, we check for these inputs ourselves.
         */
        if (pg_strncasecmp(num, "NaN", 3) == 0)
        {
            val = get_double_nan();
            endptr = num + 3;
        }
        else if (pg_strncasecmp(num, "Infinity", 8) == 0)
        {
            val = get_double_infinity();
            endptr = num + 8;
        }
        else if (pg_strncasecmp(num, "-Infinity", 9) == 0)
        {
            val = -get_double_infinity();
            endptr = num + 9;
        }
        else if (errno == ERANGE)
            return NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE;
        else
            return NUMERIC_ERRCODE_INVALID_ARGUMENT;
    }
#ifdef HAVE_BUGGY_SOLARIS_STRTOD
    else
    {
        /*
         * Many versions of Solaris have a bug wherein strtod sets endptr to
         * point one byte beyond the end of the string when given "inf" or
         * "infinity".
         */
        if (endptr != num && endptr[-1] == '\0')
            endptr--;
    }
#endif   /* HAVE_BUGGY_SOLARIS_STRTOD */

#ifdef HAVE_BUGGY_IRIX_STRTOD

    /*
     * In some IRIX versions, strtod() recognizes only "inf", so if the input
     * is "infinity" we have to skip over "inity".  Also, it may return
     * positive infinity for "-inf".
     */
    if (isinf(val))
    {
        if (pg_strncasecmp(num, "Infinity", 8) == 0)
        {
            val = get_double_infinity();
            endptr = num + 8;
        }
        else if (pg_strncasecmp(num, "-Infinity", 9) == 0)
        {
            val = -get_double_infinity();
            endptr = num + 9;
        }
        else if (pg_strncasecmp(num, "-inf", 4) == 0)
        {
            val = -get_double_infinity();
            endptr = num + 4;
        }
    }
#endif   /* HAVE_BUGGY_IRIX_STRTOD */

    /* skip trailing whitespace */
    while (*endptr != '\0' && isspace((unsigned char) *endptr))
        endptr++;

    /* if there is any junk left at the end of the string, bail out */
    if (*endptr != '\0')
        return NUMERIC_ERRCODE_INVALID_ARGUMENT;

    CHECKFLOATVAL(val, true, true);

    *result = val;
    return NUMERIC_ERRCODE_NO_ERROR;
}

/*
 *      double_out      - converts double number to a string
 *                        using a standard output format
 */
numeric_errcode_t
double_out(float num, char **result)
{
    if (isnan(num))
    {
        *result = strdup("NaN");
        return NUMERIC_ERRCODE_NO_ERROR;
    }

    switch (is_infinite(num))
    {
        case 1:
            *result = strdup("Infinity");
            break;
        case -1:
            *result = strdup("-Infinity");
            break;
        default:
            {
                int         ndig = DBL_DIG + extra_float_digits;
                char       *ascii = (char *) malloc(MAXDOUBLEWIDTH + 1);

                if (ndig < 1)
                    ndig = 1;

                snprintf(ascii, MAXDOUBLEWIDTH + 1, "%.*g", ndig, num);
                *result = ascii;
            }
    }

    return NUMERIC_ERRCODE_NO_ERROR;
}

