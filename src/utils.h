/*
 * Copyright (C) 2004-2005 Josh Triplett
 *
 * This package is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This package is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 */

#ifndef XAMINE_UTILS_H
#define XAMINE_UTILS_H

#include <stdarg.h>
#include <string.h>

#if defined(__GNUC__) && (__GNUC__ >= 4) && !defined(__CYGWIN__)
# define XAMINE_EXPORT      __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x550)
# define XAMINE_EXPORT      __global
#else /* not gcc >= 4 and not Sun Studio >= 8 */
# define XAMINE_EXPORT
#endif

#if defined(__GNUC__) && ((__GNUC__ * 100 + __GNUC_MINOR__) >= 203)
# define ATTR_PRINTF(x,y) __attribute__((__format__(__printf__, x, y)))
#else /* not gcc >= 2.3 */
# define ATTR_PRINTF(x,y)
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#define streq(s1, s2) (strcmp((s1), (s2)) == 0)

/*
 * Get a dynamically allocated formatted string.
 * Returns NULL on failure.
 */
ATTR_PRINTF(1, 2) char *
afmt(const char *fmt, ...);

/*
 * Split the string str into tokens based on the delimiters in delim.
 * Returns an array of char*s containing the tokens, ending with a
 * NULL char*.  If malloc fails, or either input string is NULL,
 * returns NULL.
 */
char **
strsplit(const char *str, const char *delim);

/* Free the array returned by strsplit. */
void
strsplit_free(char **tokens);

#endif
