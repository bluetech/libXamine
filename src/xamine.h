/* Xamine - X Protocol Analyzer
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

typedef enum
{
    XAMINE_BOOLEAN,
    XAMINE_CHAR,
    XAMINE_SIGNED,
    XAMINE_UNSIGNED,
    XAMINE_STRUCT,
    XAMINE_UNION,
    XAMINE_TYPEDEF
} XamineType;

typedef enum
{
    XAMINE_REQUEST,
    XAMINE_RESPONSE
} XamineDirection;

typedef struct XamineDefinition
{
    char *name;
    XamineType type;
    union
    {
        unsigned int size;                    /* base types */
        struct XamineFieldDefinition *fields; /* struct, union */
        struct XamineDefinition *ref;         /* typedef */
    };
    struct XamineDefinition *next;
} XamineDefinition;

typedef enum XamineExpressionType
{
    XAMINE_FIELDREF,
    XAMINE_VALUE,
    XAMINE_OP
} XamineExpressionType;

typedef enum XamineOp
{
    XAMINE_ADD,
    XAMINE_SUBTRACT,
    XAMINE_MULTIPLY,
    XAMINE_DIVIDE,
    XAMINE_LEFT_SHIFT,
    XAMINE_BITWISE_AND
} XamineOp;

typedef struct XamineExpression
{
    XamineExpressionType type;
    union
    {
        char *field;         /* Field name for XAMINE_FIELDREF */
        unsigned long value; /* Value for XAMINE_VALUE */
        struct               /* Operator and operands for XAMINE_OP */
        {
            XamineOp op;
            struct XamineExpression *left;
            struct XamineExpression *right;
        };
    };
} XamineExpression;

typedef struct XamineFieldDefinition
{
    char *name;
    XamineDefinition *definition;
    XamineExpression *length;           /* List length; NULL for non-list */
    struct XamineFieldDefinition *next;
} XamineFieldDefinition;

typedef struct XaminedItem
{
    char *name;
    XamineDefinition *definition;
    unsigned int offset;
    union
    {
        unsigned char bool_value;
        char          char_value;
        signed long   signed_value;
        unsigned long unsigned_value;
    };
    struct XaminedItem *child;
    struct XaminedItem *next;
} XaminedItem;

/* Opaque types for an Xamine library state and a conversation-specific
 * state. */
typedef struct XamineState XamineState;
typedef struct XamineConversation XamineConversation;

/* Initialization and cleanup */
extern XamineState * xamine_init();
extern void xamine_cleanup(XamineState *state);

/* Retrieval of the type definitions. */
extern XamineDefinition * xamine_get_definitions(XamineState *state);

/* Creation and destruction of conversations. */
extern XamineConversation * xamine_create_conversation(XamineState *state);
extern void xamine_free_conversation(XamineConversation *conversation);

/* Analysis */
extern XaminedItem * xamine(XamineConversation *conversation,
                            XamineDirection dir,
                            void *data, unsigned int size);
extern void xamine_free(XaminedItem *item);

#endif /* XAMINE_H */
