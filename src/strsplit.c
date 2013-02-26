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

#include <stdlib.h>
#include <string.h>

#include "strsplit.h"

char **
strsplit(const char *input, const char *delim)
{
    /*
     * Rather than allocating a string for each token, strsplit duplicates the
     * input string, replaces the delimiters with '\0', and creates an array
     * of pointers into this string.
     */

    char *str;                   /* Copy of the input string */
    size_t len;                  /* Length of the input string */
    int token_count;             /* Number of tokens in the input string */
    int token;                   /* Current token */
    enum { TOKEN, DELIM } state; /* Type of the previous character */
    char **tokens;               /* Array of tokens */

    if (!input || !delim)
        return NULL;

    len = strlen(input);

    /* Duplicate the input string. */
    str = strdup(input);
    if (!str)
        return NULL;

    /* Count the number of tokens, and replace the delimiters with '\0'. */
    token_count = 0;
    state = DELIM;
    for (int i = 0; i < len; i++) {
        if (strchr(delim, str[i])) {
            str[i] = '\0';
            state = DELIM;
        }
        else if (state == DELIM) {
            state = TOKEN;
            token_count++;
        }
    }

    /* Allocate the array of tokens. */
    tokens = malloc((token_count + 1) * sizeof(char *));
    if (!tokens) {
        free(str);
        return NULL;
    }

    /* Set each element in tokens to point to the beginning of the
     * corresponding token in str. */
    token = 0;
    state = DELIM;
    for (int i = 0; i < len; i++) {
        if(str[i] == '\0') {
            state = DELIM;
        }
        else if (state == DELIM) {
            state = TOKEN;
            tokens[token++] = &(str[i]);
        }
    }
    tokens[token_count] = NULL;

    return tokens;
}

void
strsplit_free(char **tokens)
{
    free(*tokens);
    free(tokens);
}
