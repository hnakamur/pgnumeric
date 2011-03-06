#include <cutter.h>
#include "numeric.h"

void test_numeric_from_str(void)
{
    Numeric x;
    char *str;

    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_from_str("0.0", 2, 1, &x));
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_to_str(x, -1, &str));
    cut_assert_equal_string("0.0", str);
    free(str);
    free(x);

    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_from_str("0.1", 2, 1, &x));
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_to_str(x, -1, &str));
    cut_assert_equal_string("0.1", str);
    free(str);
    free(x);

    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_from_str("0.12", -1, -1, &x));
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_to_str(x, -1, &str));
    cut_assert_equal_string("0.12", str);
    free(str);
    free(x);
}

void test_numeric_to_str_sci(void)
{
    Numeric x;
    char *str;

    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_from_str("0.12", 3, 2, &x));

    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR,
        numeric_to_str_sci(x, 1, &str));
    cut_assert_equal_string("1.2e-01", str);
    free(str);
    free(x);
}

#if 1
#define TEST_UNARY(expected, func, arg) \
do { \
    Numeric x; \
    Numeric r = NULL; \
    Numeric expected_num; \
    char *str; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((expected), -1, -1, &expected_num)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg), -1, -1, &x)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        (func)(x, &r)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_to_str(r, -1, &str)); \
    cut_assert_equal_string((expected), str); \
    free(str); \
    if (r) { \
        free(r); \
    } \
    free(x); \
    free(expected_num); \
} while (0)
#else
#define TEST_UNARY(expected, func, arg) \
do { \
    Numeric x; \
    Numeric r = NULL; \
    Numeric expected_num; \
    char *str; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((expected), -1, -1, &expected_num)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg), -1, -1, &x)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        (func)(x, &r)); \
    if (numeric_cmp(expected_num, r) != 0) { \
        cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
            numeric_to_str(r, -1, &str)); \
        cut_assert_equal_string((expected), str); \
        free(str); \
    } \
    if (r) { \
        free(r); \
    } \
    free(x); \
    free(expected_num); \
} while (0)
#endif

#define TEST_UNARY_ERR(expected, func, arg) \
do { \
    Numeric x; \
    Numeric r = NULL; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg), -1, -1, &x)); \
    cut_assert_equal_int((expected), \
        (func)(x, &r)); \
    if (r) { \
        free(r); \
    } \
    free(x); \
} while (0)

void test_numeric_abs(void)
{
    TEST_UNARY("7.5", numeric_abs, "-7.5");
    TEST_UNARY("7.5", numeric_abs, "7.5");
    TEST_UNARY("0.0", numeric_abs, "0.0");
    TEST_UNARY("NaN", numeric_abs, "NaN");
}

void test_numeric_minus(void)
{
    TEST_UNARY("7.5", numeric_minus, "-7.5");
    TEST_UNARY("-7.5", numeric_minus, "7.5");
    TEST_UNARY("0.0", numeric_minus, "0.0");
    TEST_UNARY("NaN", numeric_minus, "NaN");
}

void test_numeric_plus(void)
{
    TEST_UNARY("-7.5", numeric_plus, "-7.5");
    TEST_UNARY("7.5", numeric_plus, "7.5");
    TEST_UNARY("0.0", numeric_plus, "0.0");
    TEST_UNARY("NaN", numeric_plus, "NaN");
}

void test_numeric_sign(void)
{
    TEST_UNARY("-1", numeric_sign, "-7.5");
    TEST_UNARY("1", numeric_sign, "7.5");
    TEST_UNARY("0", numeric_sign, "0.0");
    TEST_UNARY("NaN", numeric_sign, "NaN");
}

#define TEST_SCALE(expected, func, arg, scale) \
do { \
    Numeric x; \
    Numeric r; \
    char *str; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg), -1, -1, &x)); \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        (func)(x, (scale), &r)); \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_to_str(r, -1, &str)); \
    cut_assert_equal_string((expected), str); \
    free(str); \
    free(r); \
    free(x); \
} while (0)

void test_numeric_round(void)
{
    TEST_SCALE("12.3", numeric_round, "12.345", 1);
    TEST_SCALE("12.34", numeric_round, "12.3449", 2);
    TEST_SCALE("12.35", numeric_round, "12.345", 2);
    TEST_SCALE("12.36", numeric_round, "12.355", 2);
    TEST_SCALE("12", numeric_round, "12.355", 0);
    TEST_SCALE("10", numeric_round, "12.355", -1);
    TEST_SCALE("-12.3", numeric_round, "-12.345", 1);
    TEST_SCALE("-12.34", numeric_round, "-12.3449", 2);
    TEST_SCALE("-12.35", numeric_round, "-12.345", 2);
    TEST_SCALE("-12.36", numeric_round, "-12.355", 2);
    TEST_SCALE("-12", numeric_round, "-12.355", 0);
    TEST_SCALE("-10", numeric_round, "-12.355", -1);
    TEST_SCALE("NaN", numeric_round, "NaN", 1);
}

void test_numeric_trunc(void)
{
    TEST_SCALE("12.3", numeric_trunc, "12.345", 1);
    TEST_SCALE("12.34", numeric_trunc, "12.3449", 2);
    TEST_SCALE("12.34", numeric_trunc, "12.345", 2);
    TEST_SCALE("12.35", numeric_trunc, "12.355", 2);
    TEST_SCALE("12", numeric_trunc, "12.355", 0);
    TEST_SCALE("10", numeric_trunc, "12.355", -1);
    TEST_SCALE("-12.3", numeric_trunc, "-12.345", 1);
    TEST_SCALE("-12.34", numeric_trunc, "-12.3449", 2);
    TEST_SCALE("-12.34", numeric_trunc, "-12.345", 2);
    TEST_SCALE("-12.35", numeric_trunc, "-12.355", 2);
    TEST_SCALE("-12", numeric_trunc, "-12.355", 0);
    TEST_SCALE("-10", numeric_trunc, "-12.355", -1);
    TEST_SCALE("NaN", numeric_trunc, "NaN", 1);
}

void test_numeric_ceil(void)
{
    TEST_UNARY("13", numeric_ceil, "12.345");
    TEST_UNARY("1", numeric_ceil, "1.0");
    TEST_UNARY("1", numeric_ceil, "0.01");
    TEST_UNARY("0", numeric_ceil, "0");
    TEST_UNARY("0", numeric_ceil, "-0.01");
    TEST_UNARY("-12", numeric_ceil, "-12.345");
    TEST_UNARY("NaN", numeric_ceil, "NaN");
}

void test_numeric_floor(void)
{
    TEST_UNARY("12", numeric_floor, "12.345");
    TEST_UNARY("1", numeric_floor, "1.0");
    TEST_UNARY("0", numeric_floor, "0.01");
    TEST_UNARY("0", numeric_floor, "0");
    TEST_UNARY("-1", numeric_floor, "-0.01");
    TEST_UNARY("-13", numeric_floor, "-12.345");
    TEST_UNARY("NaN", numeric_floor, "NaN");
}

#define TEST_CMP(expected, func, arg1, arg2) \
do { \
    Numeric x; \
    Numeric y; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg1), -1, -1, &x)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg2), -1, -1, &y)); \
 \
    cut_assert_equal_int((expected), \
        (func)(x, y)); \
 \
    free(x); \
    free(y); \
} while (0)

void test_numeric_cmp(void)
{
    TEST_CMP(-1, numeric_cmp, "12.344", "12.345");
    TEST_CMP(0, numeric_cmp, "12.345", "12.345");
    TEST_CMP(1, numeric_cmp, "12.346", "12.345");
    TEST_CMP(-1, numeric_cmp, "12.345", "NaN");
    TEST_CMP(1, numeric_cmp, "NaN", "12.345");
    TEST_CMP(0, numeric_cmp, "NaN", "NaN");
}

void test_numeric_eq(void)
{
    TEST_CMP(false, numeric_eq, "12.344", "12.345");
    TEST_CMP(true, numeric_eq, "12.345", "12.345");
    TEST_CMP(false, numeric_eq, "12.346", "12.345");
    TEST_CMP(false, numeric_eq, "12.345", "NaN");
    TEST_CMP(false, numeric_eq, "NaN", "12.345");
    TEST_CMP(true, numeric_eq, "NaN", "NaN");
}

void test_numeric_ne(void)
{
    TEST_CMP(true, numeric_ne, "12.344", "12.345");
    TEST_CMP(false, numeric_ne, "12.345", "12.345");
    TEST_CMP(true, numeric_ne, "12.346", "12.345");
    TEST_CMP(true, numeric_ne, "12.345", "NaN");
    TEST_CMP(true, numeric_ne, "NaN", "12.345");
    TEST_CMP(false, numeric_ne, "NaN", "NaN");
}


void test_numeric_gt(void)
{
    TEST_CMP(false, numeric_gt, "12.344", "12.345");
    TEST_CMP(false, numeric_gt, "12.345", "12.345");
    TEST_CMP(true, numeric_gt, "12.346", "12.345");
    TEST_CMP(false, numeric_gt, "12.345", "NaN");
    TEST_CMP(true, numeric_gt, "NaN", "12.345");
    TEST_CMP(false, numeric_gt, "NaN", "NaN");
}

void test_numeric_ge(void)
{
    TEST_CMP(false, numeric_ge, "12.344", "12.345");
    TEST_CMP(true, numeric_ge, "12.345", "12.345");
    TEST_CMP(true, numeric_ge, "12.346", "12.345");
    TEST_CMP(false, numeric_ge, "12.345", "NaN");
    TEST_CMP(true, numeric_ge, "NaN", "12.345");
    TEST_CMP(true, numeric_ge, "NaN", "NaN");
}

void test_numeric_lt(void)
{
    TEST_CMP(true, numeric_lt, "12.344", "12.345");
    TEST_CMP(false, numeric_lt, "12.345", "12.345");
    TEST_CMP(false, numeric_lt, "12.346", "12.345");
    TEST_CMP(true, numeric_lt, "12.345", "NaN");
    TEST_CMP(false, numeric_lt, "NaN", "12.345");
    TEST_CMP(false, numeric_lt, "NaN", "NaN");
}

void test_numeric_le(void)
{
    TEST_CMP(true, numeric_le, "12.344", "12.345");
    TEST_CMP(true, numeric_le, "12.345", "12.345");
    TEST_CMP(false, numeric_le, "12.346", "12.345");
    TEST_CMP(true, numeric_le, "12.345", "NaN");
    TEST_CMP(false, numeric_le, "NaN", "12.345");
    TEST_CMP(true, numeric_le, "NaN", "NaN");
}

#define TEST_BINARY(expected, func, arg1, arg2) \
do { \
    Numeric x; \
    Numeric y; \
    Numeric r = NULL; \
    char *str; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg1), -1, -1, &x)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg2), -1, -1, &y)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        (func)(x, y, &r)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_to_str(r, -1, &str)); \
    cut_assert_equal_string((expected), str); \
    free(str); \
    if (r) { \
        free(r); \
    } \
    free(y); \
    free(x); \
} while (0)

#define TEST_BINARY_ERR(expected, func, arg1, arg2) \
do { \
    Numeric x; \
    Numeric y; \
    Numeric r = NULL; \
 \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg1), -1, -1, &x)); \
    cut_assert_equal_int(NUMERIC_ERRCODE_NO_ERROR, \
        numeric_from_str((arg2), -1, -1, &y)); \
    cut_assert_equal_int((expected), \
        (func)(x, y, &r)); \
    if (r) { \
        free(r); \
    } \
    free(y); \
    free(x); \
} while (0)

void test_numeric_add(void)
{
    TEST_BINARY("1.23", numeric_add, "1.13", "0.1");
    TEST_BINARY("0.00", numeric_add, "1.13", "-1.13");
    TEST_BINARY("0.10", numeric_add, "1.13", "-1.03");
    TEST_BINARY("1.23", numeric_add, "0.1", "1.13");
    TEST_BINARY("1.03", numeric_add, "1.13", "-0.1");
    TEST_BINARY("NaN", numeric_add, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_add, "NaN", "1.13");
}

void test_numeric_sub(void)
{
    TEST_BINARY("1.03", numeric_sub, "1.13", "0.1");
    TEST_BINARY("0.00", numeric_sub, "1.13", "1.13");
    TEST_BINARY("0.10", numeric_sub, "1.13", "1.03");
    TEST_BINARY("-1.03", numeric_sub, "0.1", "1.13");
    TEST_BINARY("1.23", numeric_sub, "1.13", "-0.1");
    TEST_BINARY("NaN", numeric_sub, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_sub, "NaN", "1.13");
}

void test_numeric_mul(void)
{
    TEST_BINARY("1.130", numeric_mul, "1.13", "1.0");
    TEST_BINARY("0.113", numeric_mul, "1.13", "0.1");
    TEST_BINARY("1.243", numeric_mul, "1.13", "1.1");
    TEST_BINARY("-56.088", numeric_mul, "12.3", "-4.56");
    TEST_BINARY("NaN", numeric_mul, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_mul, "NaN", "1.13");
}

void test_numeric_div(void)
{
    TEST_BINARY("1.13000000000000000000", numeric_div, "1.13", "1.0");
    TEST_BINARY("11.3000000000000000", numeric_div, "1.13", "0.1");
    TEST_BINARY("1.13000000000000000000", numeric_div, "1.243", "1.1");
    TEST_BINARY("6.2150000000000000", numeric_div, "1.243", "0.2");
    TEST_BINARY("0.33333333333333333333", numeric_div, "1", "3");
    TEST_BINARY("0.66666666666666666667", numeric_div, "2", "3");
    TEST_BINARY_ERR(NUMERIC_ERRCODE_DIVISION_BY_ZERO, numeric_div, "1.243", "0");
    TEST_BINARY("NaN", numeric_div, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_div, "NaN", "1.13");
}

void test_numeric_div_trunc(void)
{
    TEST_BINARY("1", numeric_div_trunc, "1.13", "1.0");
    TEST_BINARY("11", numeric_div_trunc, "1.13", "0.1");
    TEST_BINARY("1", numeric_div_trunc, "1.243", "1.1");
    TEST_BINARY("6", numeric_div_trunc, "1.243", "0.2");
    TEST_BINARY("0", numeric_div_trunc, "1", "3");
    TEST_BINARY("0", numeric_div_trunc, "2", "3");
    TEST_BINARY("3", numeric_div_trunc, "10", "3");
    TEST_BINARY("6", numeric_div_trunc, "20", "3");
    TEST_BINARY("2", numeric_div_trunc, "5", "2");
    TEST_BINARY_ERR(NUMERIC_ERRCODE_DIVISION_BY_ZERO, numeric_div_trunc, "1.243", "0");
    TEST_BINARY("NaN", numeric_div_trunc, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_div_trunc, "NaN", "1.13");
}

void test_numeric_mod(void)
{
    TEST_BINARY("0.13", numeric_mod, "1.13", "1.0");
    TEST_BINARY("0.03", numeric_mod, "1.13", "0.1");
    TEST_BINARY("0.143", numeric_mod, "1.243", "1.1");
    TEST_BINARY("0.043", numeric_mod, "1.243", "0.2");
    TEST_BINARY("1", numeric_mod, "1", "3");
    TEST_BINARY("2", numeric_mod, "2", "3");
    TEST_BINARY("1", numeric_mod, "5", "2");
    TEST_BINARY_ERR(NUMERIC_ERRCODE_DIVISION_BY_ZERO, numeric_mod, "1.243", "0");
    TEST_BINARY("NaN", numeric_mod, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_mod, "NaN", "1.13");
}

void test_numeric_min(void)
{
    TEST_BINARY("1.0", numeric_min, "1.13", "1.0");
    TEST_BINARY("0.1", numeric_min, "1.13", "0.1");
    TEST_BINARY("1.1", numeric_min, "1.243", "1.1");
    TEST_BINARY("-1.243", numeric_min, "-1.243", "0.2");
    TEST_BINARY("1.13", numeric_min, "1.13", "NaN");
    TEST_BINARY("1.13", numeric_min, "NaN", "1.13");
}

void test_numeric_max(void)
{
    TEST_BINARY("1.13", numeric_max, "1.13", "1.0");
    TEST_BINARY("1.13", numeric_max, "1.13", "0.1");
    TEST_BINARY("1.243", numeric_max, "1.243", "1.1");
    TEST_BINARY("0.2", numeric_max, "-1.243", "0.2");
    TEST_BINARY("NaN", numeric_max, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_max, "NaN", "1.13");
}

void test_numeric_sqrt(void)
{
    TEST_UNARY("1.000000000000000", numeric_sqrt, "1");
    TEST_UNARY_ERR(NUMERIC_ERRCODE_INVALID_ARGUMENT, numeric_sqrt, "-1");
    TEST_UNARY("0.000000000000000", numeric_sqrt, "0");
    TEST_UNARY("2.000000000000000", numeric_sqrt, "4");
    TEST_UNARY("3.000000000000000", numeric_sqrt, "9");
    TEST_UNARY("1.414213562373095", numeric_sqrt, "2");
    TEST_UNARY("0.31622776601683793", numeric_sqrt, "0.1");
    TEST_UNARY("1.048808848170152", numeric_sqrt, "1.1");
    TEST_UNARY("NaN", numeric_sqrt, "NaN");
}

void test_numeric_exp(void)
{
    TEST_UNARY("1.0000000000000000", numeric_exp, "0");
    TEST_UNARY("2.7182818284590452", numeric_exp, "1");
    TEST_UNARY("2.0000000008801094", numeric_exp, "0.693147181");
    TEST_UNARY("22026.465794806717", numeric_exp, "10");
    TEST_UNARY("0.3678794411714423", numeric_exp, "-1");
    TEST_UNARY("0.00004539992976248485", numeric_exp, "-10");
    TEST_UNARY_ERR(NUMERIC_ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE, numeric_exp, "100000000");
    TEST_UNARY("NaN", numeric_exp, "NaN");
}

void test_numeric_ln(void)
{
    TEST_UNARY("0.0000000000000000", numeric_ln, "1");
    TEST_UNARY("0.4054651081081644", numeric_ln, "1.5");
    TEST_UNARY("0.6931471805599453", numeric_ln, "2");
    TEST_UNARY("0.9999999999999999", numeric_ln, "2.718281828459045");
    TEST_UNARY("1.0000000000000003", numeric_ln, "2.718281828459046");
    TEST_UNARY("1.0000000000000006", numeric_ln, "2.718281828459047");
    TEST_UNARY("2.3025850929940457", numeric_ln, "10");
    TEST_UNARY("2.3513752571634777", numeric_ln, "10.5");
    TEST_UNARY("9.2102403669758494", numeric_ln, "9999");
    TEST_UNARY_ERR(NUMERIC_ERRCODE_INVALID_ARGUMENT, numeric_ln, "0");
    TEST_UNARY_ERR(NUMERIC_ERRCODE_INVALID_ARGUMENT, numeric_ln, "-1");
#if 0
    TEST_UNARY("-0.000000089999999999999878", numeric_ln, "0.99999991000000405");
#else
    TEST_UNARY("-0.00000009000000000", numeric_ln, "0.99999991000000405");
#endif
    TEST_UNARY("2302.58509299404495", numeric_ln, "9.99999999999266E+999");

    TEST_UNARY("NaN", numeric_ln, "NaN");

}

void test_numeric_log10(void)
{
    TEST_UNARY("-3.0000000000000000", numeric_log10, "0.001");
    TEST_UNARY("0.00000000000000000000", numeric_log10, "1");
    TEST_UNARY("0.17609125905568124208", numeric_log10, "1.5");
    TEST_UNARY("0.30102999566398119521", numeric_log10, "2");
    TEST_UNARY("1.00000000000000000000", numeric_log10, "10");
    TEST_UNARY("1.02118929906993807279", numeric_log10, "10.5");
    TEST_UNARY("1.8450980400142568", numeric_log10, "70");
    TEST_UNARY("3.9999565683801925", numeric_log10, "9999");
    TEST_UNARY_ERR(NUMERIC_ERRCODE_INVALID_ARGUMENT, numeric_log10, "0");
    TEST_UNARY_ERR(NUMERIC_ERRCODE_INVALID_ARGUMENT, numeric_log10, "-1");
    TEST_UNARY("NaN", numeric_log10, "NaN");
}

void test_numeric_power(void)
{
#if 0
    TEST_BINARY("0.0000000000000000", numeric_power, "0", "0");
#else
    TEST_BINARY("1.0000000000000000", numeric_power, "0", "0");
#endif
    TEST_BINARY("0.0000000000000000", numeric_power, "0", "1");
    TEST_BINARY("0.0000000000000000", numeric_power, "0", "2");
    TEST_BINARY("1.0000000000000000", numeric_power, "1", "0");
    TEST_BINARY("1.0000000000000000", numeric_power, "1", "1");
    TEST_BINARY("1.0000000000000000", numeric_power, "1", "2");
    TEST_BINARY("1.0000000000000000", numeric_power, "2", "0");
    TEST_BINARY("2.0000000000000000", numeric_power, "2", "1");
    TEST_BINARY("4.0000000000000000", numeric_power, "2", "2");
    TEST_BINARY("8.0000000000000000", numeric_power, "2", "3");
    TEST_BINARY("16.0000000000000000", numeric_power, "2", "4");
    TEST_BINARY("32.0000000000000000", numeric_power, "2", "5");
    TEST_BINARY("2147483648.0000000000000000", numeric_power, "2", "31");
    TEST_BINARY("4294967296.0000000000000000", numeric_power, "2", "32");
    TEST_BINARY("1.0000000000000000", numeric_power, "10", "0");
    TEST_BINARY("10.0000000000000000", numeric_power, "10", "1");
    TEST_BINARY("100.0000000000000000", numeric_power, "10", "2");
    TEST_BINARY("1000.0000000000000000", numeric_power, "10", "3");
    TEST_BINARY("10000.0000000000000000", numeric_power, "10", "4");
    TEST_BINARY("100000.0000000000000000", numeric_power, "10", "5");
    TEST_BINARY("1000000.0000000000000000", numeric_power, "10", "6");
    TEST_BINARY("10000000.0000000000000000", numeric_power, "10", "7");
    TEST_BINARY("100000000.0000000000000000", numeric_power, "10", "8");
    TEST_BINARY("1.0000000000000000", numeric_power, "0.1", "0");
    TEST_BINARY("0.1000000000000000", numeric_power, "0.1", "1");
    TEST_BINARY("0.0100000000000000", numeric_power, "0.1", "2");
    TEST_BINARY("0.0010000000000000", numeric_power, "0.1", "3");
    TEST_BINARY("0.0001000000000000", numeric_power, "0.1", "4");
    TEST_BINARY("0.0000100000000000", numeric_power, "0.1", "5");
    TEST_BINARY("0.0000010000000000", numeric_power, "0.1", "6");
    TEST_BINARY("1.0000000000000000", numeric_power, "1", "-1");
    TEST_BINARY("0.3333333333333333", numeric_power, "3", "-1");
    TEST_BINARY("1.0005471142828335", numeric_power, "1.2", "0.003");
    TEST_BINARY("166.53672446385521", numeric_power, "71", "1.2");
    TEST_BINARY("0.0000000010000000000000000", numeric_power, "10E-19", "0.5");
#if 0
    TEST_BINARY("1.000001", numeric_power, "1.000001", "1e-101");
    TEST_BINARY("1.000001", numeric_power, "1.000001", "1e-95");
    TEST_BINARY("0.9999999", numeric_power, "0.9999999", "1e-101");
    TEST_BINARY("0.9999999", numeric_power, "0.9999999", "1e-95");
#else
    TEST_BINARY("1.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", numeric_power, "1.000001", "1e-101");
    TEST_BINARY("1.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", numeric_power, "1.000001", "1e-95");
    TEST_BINARY("1.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", numeric_power, "0.9999999", "1e-101");
    TEST_BINARY("1.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", numeric_power, "0.9999999", "1e-95");
#endif
    TEST_BINARY("NaN", numeric_power, "1.13", "NaN");
    TEST_BINARY("NaN", numeric_power, "NaN", "1.13");
}
