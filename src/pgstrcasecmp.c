/*-------------------------------------------------------------------------
 *
 * pgstrcasecmp.c
 *	   Portable SQL-like case-independent comparisons and conversions.
 *
 * SQL99 specifies Unicode-aware case normalization, which we don't yet
 * have the infrastructure for.  Instead we use tolower() to provide a
 * locale-aware translation.  However, there are some locales where this
 * is not right either (eg, Turkish may do strange things with 'i' and
 * 'I').  Our current compromise is to use tolower() for characters with
 * the high bit set, and use an ASCII-only downcasing for 7-bit
 * characters.
 *
 * Modified in 2011 for standalone use by Hiroaki Nakamura.
 * Portions Copyright (c) 1996-2010, PostgreSQL Global Development Group
 *
 * $PostgreSQL: pgsql/src/port/pgstrcasecmp.c,v 1.12 2010/01/02 16:58:13 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>

/* msb for char */
#define HIGHBIT                 (0x80)
#define IS_HIGHBIT_SET(ch)      ((unsigned char)(ch) & HIGHBIT)

/*
 * Case-independent comparison of two not-necessarily-null-terminated strings.
 * At most n bytes will be examined from each string.
 */
int
pg_strncasecmp(const char *s1, const char *s2, size_t n)
{
	while (n-- > 0)
	{
		unsigned char ch1 = (unsigned char) *s1++;
		unsigned char ch2 = (unsigned char) *s2++;

		if (ch1 != ch2)
		{
			if (ch1 >= 'A' && ch1 <= 'Z')
				ch1 += 'a' - 'A';
			else if (IS_HIGHBIT_SET(ch1) && isupper(ch1))
				ch1 = tolower(ch1);

			if (ch2 >= 'A' && ch2 <= 'Z')
				ch2 += 'a' - 'A';
			else if (IS_HIGHBIT_SET(ch2) && isupper(ch2))
				ch2 = tolower(ch2);

			if (ch1 != ch2)
				return (int) ch1 - (int) ch2;
		}
		if (ch1 == 0)
			break;
	}
	return 0;
}
