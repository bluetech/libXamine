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

#ifndef XAMINE_H
#define XAMINE_H

enum xamine_type {
    XAMINE_BOOL,
    XAMINE_CHAR,
    XAMINE_SIGNED,
    XAMINE_UNSIGNED,
    XAMINE_STRUCT,
    XAMINE_UNION,
    XAMINE_TYPEDEF
};

struct xamine_definition {
    char *name;
    enum xamine_type type;
    union {
        size_t size;                            /* base types */
        struct xamine_field_definition *fields; /* struct, union */
        const struct xamine_definition *ref;    /* typedef */
    } u;
    struct xamine_definition *next;
};

struct xamine_field_definition {
    char *name;
    const struct xamine_definition *definition;
    struct xamine_expression *length;       /* List length; NULL for non-list */
    struct xamine_field_definition *next;
};

enum xamine_expression_type {
    XAMINE_FIELDREF,
    XAMINE_VALUE,
    XAMINE_OP
};

enum xamine_op {
    XAMINE_ADD,
    XAMINE_SUBTRACT,
    XAMINE_MULTIPLY,
    XAMINE_DIVIDE,
    XAMINE_LEFT_SHIFT,
    XAMINE_BITWISE_AND
};

struct xamine_expression {
    enum xamine_expression_type type;
    union {
        char *field;                        /* Field name for XAMINE_FIELDREF */
        unsigned long value;                /* Value for XAMINE_VALUE */
        struct {                            /* Operator and operands for XAMINE_OP */
            enum xamine_op op;
            struct xamine_expression *left;
            struct xamine_expression *right;
        } op;
    } u;
};

/* Context */

struct xamine_context;

enum xamine_context_flags {
    XAMINE_CONTEXT_NO_FLAGS = 0
};

struct xamine_context *
xamine_context_new(enum xamine_context_flags flags);

struct xamine_context *
xamine_context_ref(struct xamine_context *context);

struct xamine_context *
xamine_context_unref(struct xamine_context *context);

const struct xamine_definition *
xamine_get_definitions(struct xamine_context *state);

/* Conversation */

struct xamine_conversation;

enum xamine_conversation_flags {
    XAMINE_CONVERSATION_NO_FLAGS = 0
};

struct xamine_conversation *
xamine_conversation_new(struct xamine_context *context,
                        enum xamine_conversation_flags flags);

struct xamine_conversation *
xamine_conversation_ref(struct xamine_conversation *conversation);

struct xamine_conversation *
xamine_conversation_unref(struct xamine_conversation *conversation);

/* Analysis */

struct xamine_item {
    char *name;
    const struct xamine_definition *definition;
    size_t offset;
    union {
        unsigned char bool_value;
        char          char_value;
        signed long   signed_value;
        unsigned long unsigned_value;
    } u;
    struct xamine_item *child;
    struct xamine_item *next;
};

enum xamine_direction {
    XAMINE_REQUEST,
    XAMINE_RESPONSE
};

struct xamine_item *
xamine_examine(const struct xamine_conversation *conversation,
               enum xamine_direction direction,
               const void *data, size_t size);

void
xamine_item_free(struct xamine_item *item);

#endif /* XAMINE_H */
