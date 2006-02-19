/* strsplit.c - Split a string into an array of tokens
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

#if !defined(STRSPLIT_H)
#define STRSPLIT_H

/* strsplit splits the string str into tokens based on the delimiters in
 * delim.  It returns an array of char*s containing the tokens, ending with a
 * NULL char*.  If malloc fails, or either input string is NULL, strsplit
 * returns NULL.  */
char ** strsplit(const char *str, const char *delim);

/* strsplit_free frees the array returned by strsplit. */
void strsplit_free(char **tokens);

#endif /* !defined(STRSPLIT_H) */
