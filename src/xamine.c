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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <libxml/parser.h>

#include "utils.h"
#include "xamine.h"

const char *XAMINE_PATH_DEFAULT = "/usr/share/xcb";
const char *XAMINE_PATH_DELIM = ":";
const char *XAMINE_PATH_GLOB = "/*.xml";

/* Concrete definitions for opaque and private structure types. */
struct xamine_event {
    unsigned char number;
    const struct xamine_definition *definition;
    struct xamine_event *next;
};

struct xamine_error {
    unsigned char number;
    const struct xamine_definition *definition;
    struct xamine_error *next;
};

struct xamine_extension {
    char *name;
    char *xname;
    struct xamine_event *events;
    struct xamine_error *errors;
    struct xamine_extension *next;
};

struct xamine_context {
    int refcnt;
    enum xamine_context_flags flags;

    unsigned char host_is_le;
    struct xamine_definition *definitions;
    struct xamine_definition *core_events[64];  /* Core events 2-63 (0-1 unused) */
    struct xamine_definition *core_errors[128]; /* Core errors 0-127             */
    struct xamine_extension *extensions;
};

struct xamine_conversation {
    struct xamine_context *ctx;
    int refcnt;
    enum xamine_conversation_flags flags;

    unsigned char is_le;
    struct xamine_definition *extension_events[64];  /* Extension events 64-127  */
    struct xamine_definition *extension_errors[128]; /* Extension errors 128-255 */
    struct xamine_extension *extensions[128];        /* Extensions 128-255       */
};

/********** Private functions **********/

/* Helper function to avoid casting. */
static char *
xamine_xml_get_prop(xmlNode *node, const char *name)
{
    return (char *) xmlGetProp(node, (const xmlChar *) name);
}

/* Helper function to avoid casting. */
static const char *
xamine_xml_get_node_name(xmlNode *node)
{
    return (const char *) node->name;
}

/* Helper function to avoid casting. */
static char *
xamine_xml_get_node_content(xmlNode *node)
{
    return (char *) xmlNodeGetContent(node);
}

static xmlNode *
xamine_xml_next_elem(xmlNode *elem)
{
    while (elem && elem->type != XML_ELEMENT_NODE)
        elem = elem->next;
    return elem;
}

static char *
xamine_make_name(struct xamine_extension *extension, char *name)
{
    return afmt("%s%s", extension ? extension->name : "", name);
}

static const struct xamine_definition *
xamine_find_type(struct xamine_context *ctx, const char *name)
{
    /* FIXME: does not work for extension types. */
    for (const struct xamine_definition *def = ctx->definitions; def; def = def->next)
        if (streq(def->name, name))
            return def;
    return NULL;
}

static struct xamine_expression *
xamine_parse_expression(struct xamine_context *ctx, xmlNode *elem)
{
    struct xamine_expression *e = calloc(1, sizeof(*e));

    elem = xamine_xml_next_elem(elem);
    if (streq(xamine_xml_get_node_name(elem), "op")) {
        char *temp = xamine_xml_get_prop(elem, "op");
        e->type = XAMINE_OP;
        if (streq(temp, "+"))
            e->u.op.op = XAMINE_ADD;
        else if (streq(temp, "-"))
            e->u.op.op = XAMINE_SUBTRACT;
        else if (streq(temp, "*"))
            e->u.op.op = XAMINE_MULTIPLY;
        else if (streq(temp, "/"))
            e->u.op.op = XAMINE_DIVIDE;
        else if (streq(temp, "<<"))
            e->u.op.op = XAMINE_LEFT_SHIFT;
        else if (streq(temp, "&"))
            e->u.op.op = XAMINE_BITWISE_AND;
        elem = xamine_xml_next_elem(elem->children);
        e->u.op.left = xamine_parse_expression(ctx, elem);
        elem = xamine_xml_next_elem(elem->next);
        e->u.op.right = xamine_parse_expression(ctx, elem);
    }
    else if (streq(xamine_xml_get_node_name(elem), "value")) {
        e->type = XAMINE_VALUE;
        e->u.value = strtol(xamine_xml_get_node_content(elem), NULL, 0);
    }
    else if (streq(xamine_xml_get_node_name(elem), "fieldref")) {
        e->type = XAMINE_FIELDREF;
        e->u.field = strdup(xamine_xml_get_node_content(elem));
    }

    return e;
}

static struct xamine_field_definition *
xamine_parse_fields(struct xamine_context *ctx, xmlNode *elem)
{
    xmlNode *cur;
    struct xamine_field_definition *head;
    struct xamine_field_definition **tail = &head;

    for (cur = elem->children; cur; cur = xamine_xml_next_elem(cur->next)) {
        /* FIXME: handle elements other than "field", "pad", "doc" and "list". */

        if (streq(xamine_xml_get_node_name(cur), "doc"))
            continue;

        *tail = calloc(1, sizeof(**tail));
        if (streq(xamine_xml_get_node_name(cur), "pad")) {
            (*tail)->name = strdup("pad");
            (*tail)->definition = xamine_find_type(ctx, "CARD8");
            (*tail)->length = calloc(1, sizeof(*(*tail)->length));
            (*tail)->length->type = XAMINE_VALUE;
            (*tail)->length->u.value = atoi(xamine_xml_get_prop(cur, "bytes"));
        }
        else {
            (*tail)->name = strdup(xamine_xml_get_prop(cur, "name"));
            (*tail)->definition = xamine_find_type(ctx, xamine_xml_get_prop(cur, "type"));
            /* FIXME: handle missing length expressions. */
            if (streq(xamine_xml_get_node_name(cur), "list"))
                (*tail)->length = xamine_parse_expression(ctx, cur->children);
        }
        tail = &((*tail)->next);
    }

    *tail = NULL;
    return head;
}

static void
xamine_parse_xmlxcb_file(struct xamine_context *ctx,
                         const char *filename)
{
    xmlDoc *doc;
    xmlNode *root, *elem;
    char *extension_xname;
    struct xamine_extension *extension = NULL;

    /* Ignore text nodes consisting entirely of whitespace. */
    xmlKeepBlanksDefault(0);

    doc = xmlParseFile(filename);
    if (!doc)
        return;

    root = xmlDocGetRootElement(doc);
    if (!root)
        return;

    extension_xname = xamine_xml_get_prop(root, "extension-xname");
    if (extension_xname) {
        for (extension = ctx->extensions; extension; extension = extension->next)
            if (streq(extension->xname, extension_xname))
                break;

        if (extension) {
            extension = calloc(1, sizeof(*extension));
            extension->name = strdup(xamine_xml_get_prop(root, "extension-name"));
            extension->xname = strdup(extension_xname);
            extension->next = ctx->extensions;
            ctx->extensions = extension;
        }
    }

    for (elem = root->children; elem; elem = xamine_xml_next_elem(elem->next)) {
        if (streq(xamine_xml_get_node_name(elem), "request")) {
            /* Not yet implemented. */
        }
        else if (streq(xamine_xml_get_node_name(elem), "event")) {
            char *no_sequence_number;
            struct xamine_definition *def;
            struct xamine_field_definition *fields;
            int number;

            number = atoi(xamine_xml_get_prop(elem, "number"));
            if (number > 64)
                continue;

            def = calloc(1, sizeof(*def));
            def->name = xamine_make_name(extension, xamine_xml_get_prop(elem, "name"));
            def->type = XAMINE_STRUCT;

            fields = xamine_parse_fields(ctx, elem);
            if (!fields) {
                fields = calloc(1, sizeof(*fields));
                fields->name = strdup("pad");
                fields->definition = xamine_find_type(ctx, "CARD8");
            }

            def->u.fields = calloc(1, sizeof(*def->u.fields));
            def->u.fields->name = strdup("response_type");
            def->u.fields->definition = xamine_find_type(ctx, "BYTE");
            def->u.fields->next = fields;
            fields = fields->next;
            no_sequence_number = xamine_xml_get_prop(elem, "no-sequence-number");
            if (no_sequence_number && streq(no_sequence_number, "true")) {
                def->u.fields->next->next = fields;
            }
            else {
                def->u.fields->next->next = calloc(1, sizeof(*def->u.fields->next->next));
                def->u.fields->next->next->name = strdup("sequence");
                def->u.fields->next->next->definition = xamine_find_type(ctx, "CARD16");
                def->u.fields->next->next->next = fields;
            }
            def->next = ctx->definitions;
            ctx->definitions = def;

            if (extension) {
                struct xamine_event *event = calloc(1, sizeof(*event));
                event->number = number;
                event->definition = def;
                event->next = extension->events;
            }
            else {
                ctx->core_events[number] = def;
            }
        }
        else if (streq(xamine_xml_get_node_name(elem), "eventcopy")) {
            struct xamine_definition *def;
            int number;

            number = atoi(xamine_xml_get_prop(elem, "number"));
            if (number > 64)
                continue;

            def = calloc(1, sizeof(*def));
            def->name = strdup(xamine_xml_get_prop(elem, "name"));
            def->type = XAMINE_TYPEDEF;
            def->u.ref = xamine_find_type(ctx, xamine_xml_get_prop(elem, "ref"));

            if (extension) {
                struct xamine_event *event = calloc(1, sizeof(*event));
                event->number = number;
                event->definition = def;
                event->next = extension->events;
            }
            else {
                ctx->core_events[number] = def;
            }
        }
        else if (streq(xamine_xml_get_node_name(elem), "error")) {
        }
        else if (streq(xamine_xml_get_node_name(elem), "errorcopy")) {
        }
        else if (streq(xamine_xml_get_node_name(elem), "struct")) {
            struct xamine_definition *def = calloc(1, sizeof(*def));
            def->name = xamine_make_name(extension, xamine_xml_get_prop(elem, "name"));
            def->type = XAMINE_STRUCT;
            def->u.fields = xamine_parse_fields(ctx, elem);
            def->next = ctx->definitions;
            ctx->definitions = def;
        }
        else if (streq(xamine_xml_get_node_name(elem), "union")) {
        }
        else if (streq(xamine_xml_get_node_name(elem), "xidtype")) {
            struct xamine_definition *def = calloc(1, sizeof(*def));
            def->name = xamine_make_name(extension, xamine_xml_get_prop(elem, "name"));
            def->type = XAMINE_UNSIGNED;
            def->u.size = 4;
            def->next = ctx->definitions;
            ctx->definitions = def;
        }
        else if (streq(xamine_xml_get_node_name(elem), "enum")) {
        }
        else if (streq(xamine_xml_get_node_name(elem), "typedef")) {
            struct xamine_definition *def = calloc(1, sizeof(*def));
            def->name = xamine_make_name(extension, xamine_xml_get_prop(elem, "newname"));
            def->type = XAMINE_TYPEDEF;
            def->u.ref = xamine_find_type(ctx, xamine_xml_get_prop(elem, "oldname"));
            def->next = ctx->definitions;
            ctx->definitions = def;
        }
        else if (streq(xamine_xml_get_node_name(elem), "import")) {
        }
    }
}

static long
xamine_evaluate_expression(const struct xamine_expression *expression,
                           const struct xamine_item *parent)
{
    switch (expression->type) {
    case XAMINE_VALUE:
        return expression->u.value;

    case XAMINE_FIELDREF:
        for (struct xamine_item *cur = parent->child; cur; cur = cur->next) {
            if (streq(cur->name, expression->u.field)) {
                switch (cur->definition->type) {
                case XAMINE_BOOL: return cur->u.bool_value;
                case XAMINE_CHAR: return cur->u.char_value;
                case XAMINE_SIGNED: return cur->u.signed_value;
                case XAMINE_UNSIGNED: return cur->u.unsigned_value;

                /* FIXME: Remove assert. */
                case XAMINE_STRUCT:
                case XAMINE_UNION:
                case XAMINE_TYPEDEF:
                    assert(!"unreachable");
                    return 0;
                }
            }
        }

    case XAMINE_OP:
    {
        long left  = xamine_evaluate_expression(expression->u.op.left, parent);
        long right = xamine_evaluate_expression(expression->u.op.right, parent);

        switch (expression->u.op.op) {
        case XAMINE_ADD:         return left + right;
        case XAMINE_SUBTRACT:    return left - right;
        case XAMINE_MULTIPLY:    return left * right;
        case XAMINE_DIVIDE:      return left / right; /* FIXME: divide by zero */
        case XAMINE_LEFT_SHIFT:  return left << right;
        case XAMINE_BITWISE_AND: return left & right;
        }
    }
    }

    /* FIXME: Remove assert. */
    assert(!"unreachable");
    return 0;
}

static struct xamine_item *
xamine_definition(const struct xamine_conversation *conversation,
                  const unsigned char **data, size_t *size, size_t *offset,
                  const struct xamine_definition *definition,
                  const struct xamine_item *parent);

static struct xamine_item *
xamine_field_definition(const struct xamine_conversation *conversation,
                        const unsigned char **data, size_t *size, size_t *offset,
                        const struct xamine_field_definition *field,
                        const struct xamine_item *parent)
{
    struct xamine_item *item;

    if (field->length) {
        struct xamine_item **end;
        size_t length;

        item = calloc(1, sizeof(*item));
        item->name = field->name;
        item->definition = field->definition;
        item->offset = *offset;

        end = &item->child;
        length = xamine_evaluate_expression(field->length, parent);
        for (size_t i = 0; i < length; i++) {
            *end = xamine_definition(conversation, data, size, offset, field->definition, parent);
            (*end)->name = afmt("[%lu]", i);
            end = &(*end)->next;
        }
        *end = NULL;
    }
    else {
        item = xamine_definition(conversation, data, size, offset, field->definition, parent);
        item->name = field->name;
    }

    return item;
}

static struct xamine_item *
xamine_definition(const struct xamine_conversation *conversation,
                  const unsigned char **data, size_t *size, size_t *offset,
                  const struct xamine_definition *definition,
                  const struct xamine_item *parent)
{
    struct xamine_item *item;

    if (definition->type == XAMINE_TYPEDEF) {
        item = xamine_definition(conversation, data, size, offset, definition->u.ref, parent);
        item->definition = definition;
        return item;
    }

    item = calloc(1, sizeof(*item));
    item->definition = definition;
    if (definition->type == XAMINE_STRUCT) {
        struct xamine_item **end = &item->child;

        for (struct xamine_field_definition *child = definition->u.fields; child; child = child->next) {
            *end = xamine_field_definition(conversation, data, size, offset, child, item);
            end = &(*end)->next;
        }
        *end = NULL;
    }
    else {
        switch (definition->type) {
        case XAMINE_BOOL:
            /* FIXME: field->definition->size must be 1 */
            item->u.bool_value = *(const unsigned char *) (*data) ? 1 : 0;
            break;

        case XAMINE_CHAR:
            /* FIXME: field->definition->size must be 1 */
            item->u.char_value = *(const char *) (*data);
            break;

        case XAMINE_SIGNED:
        case XAMINE_UNSIGNED:
        {
            unsigned char *dest = definition->type == XAMINE_SIGNED
                                ? (unsigned char *) &(item->u.signed_value)
                                : (unsigned char *) &(item->u.unsigned_value);
            const unsigned char *src = *data;
            if (definition->u.size == 1 || conversation->is_le == conversation->ctx->host_is_le) {
                memcpy(dest, src, definition->u.size);
            }
            else {
                dest += definition->u.size - 1;
                for (int i = 0; i < definition->u.size; i++)
                    *dest-- = *src++;
            }
            break;
        }

        /* FIXME: Remove assert. */
        case XAMINE_STRUCT:
        case XAMINE_UNION:
        case XAMINE_TYPEDEF:
            assert(!"unreachable");
            return 0;
        }
        *data += definition->u.size;
        *size -= definition->u.size;
        *offset += definition->u.size;
    }

    return item;
}

/********** Public functions **********/

XAMINE_EXPORT struct xamine_context *
xamine_context_new(enum xamine_context_flags flags)
{
    const char *xamine_path_env;
    char **xamine_path;
    char **iter;
    glob_t xml_files;
    struct xamine_context *ctx;
    static const struct {
        const char *name;
        enum xamine_type type;
        size_t size;
    } core_types[] = {
        { "char",   XAMINE_CHAR,     1 },
        { "BOOL",   XAMINE_BOOL,     1 },
        { "BYTE",   XAMINE_UNSIGNED, 1 },
        { "CARD8",  XAMINE_UNSIGNED, 1 },
        { "CARD16", XAMINE_UNSIGNED, 2 },
        { "CARD32", XAMINE_UNSIGNED, 4 },
        { "INT8",   XAMINE_SIGNED,   1 },
        { "INT16",  XAMINE_SIGNED,   2 },
        { "INT32",  XAMINE_SIGNED,   4 },
    };

    if (flags & ~XAMINE_CONTEXT_NO_FLAGS)
        return NULL;

    ctx = calloc(1, sizeof(*ctx));
    ctx->refcnt = 1;
    ctx->flags = flags;

    {
        unsigned long l = 1;
        ctx->host_is_le = *(unsigned char*) &l;
    }

    /* Add definitions of core types. */
    for (int i = 0; i < ARRAY_SIZE(core_types); i++) {
        struct xamine_definition *temp = calloc(1, sizeof(*temp));

        temp->name = strdup(core_types[i].name);
        temp->type = core_types[i].type;
        temp->u.size = core_types[i].size;

        temp->next = ctx->definitions;
        ctx->definitions = temp;
    }

    /* Set up the search path for XML-XCB descriptions. */
    xamine_path_env = getenv("XAMINE_PATH");
    if (!xamine_path_env)
        xamine_path_env = XAMINE_PATH_DEFAULT;

    xamine_path = strsplit(xamine_path_env, XAMINE_PATH_DELIM);

    /* Find all the XML files on the search path. */
    xml_files.gl_pathv = NULL;
    for (iter = xamine_path; *iter; iter++) {
        char *pattern = afmt("%s%s", *iter, XAMINE_PATH_GLOB);
        glob(pattern, (xml_files.gl_pathv ? GLOB_APPEND : 0), NULL, &xml_files);
        free(pattern);
    }

    strsplit_free(xamine_path);

    /* Parse the XML files. */
    if (xml_files.gl_pathv)
        for (iter = xml_files.gl_pathv; *iter; iter++)
            xamine_parse_xmlxcb_file(ctx, *iter);

    globfree(&xml_files);

    return ctx;
}

XAMINE_EXPORT struct xamine_context *
xamine_context_ref(struct xamine_context *ctx)
{
    ctx->refcnt++;
    return ctx;
}

XAMINE_EXPORT struct xamine_context *
xamine_context_unref(struct xamine_context *ctx)
{
    if (!ctx || --ctx->refcnt > 0)
        return ctx;

    while (ctx->definitions) {
        struct xamine_definition *temp = ctx->definitions;
        ctx->definitions = ctx->definitions->next;
        free(temp);
    }
    /* FIXME: incomplete */

    return NULL;
}

XAMINE_EXPORT const struct xamine_definition *
xamine_get_definitions(struct xamine_context *ctx)
{
    return ctx->definitions;
}

XAMINE_EXPORT struct xamine_conversation *
xamine_conversation_new(struct xamine_context *ctx,
                        enum xamine_conversation_flags flags)
{
    struct xamine_conversation *conversation;

    if (flags & ~XAMINE_CONVERSATION_NO_FLAGS)
        return NULL;

    conversation = calloc(1, sizeof(*conversation));
    conversation->refcnt = 1;
    conversation->flags = flags;
    conversation->ctx = xamine_context_ref(ctx);

    /* FIXME */
    conversation->is_le = ctx->host_is_le;

    return conversation;
}

XAMINE_EXPORT struct xamine_conversation *
xamine_conversation_ref(struct xamine_conversation *conversation)
{
    conversation->refcnt++;
    return conversation;
}

XAMINE_EXPORT struct xamine_conversation *
xamine_conversation_unref(struct xamine_conversation *conversation)
{
    if (!conversation || --conversation->refcnt > 0)
        return conversation;

    xamine_context_unref(conversation->ctx);
    free(conversation);
    return NULL;
}

XAMINE_EXPORT struct xamine_item *
xamine_examine(const struct xamine_conversation *conversation,
               enum xamine_direction direction,
               const void *data_void, size_t size)
{
    const struct xamine_definition *definition = NULL;
    size_t offset = 0;
    const unsigned char *data = data_void;

    if (direction == XAMINE_REQUEST) {
        /* Request layout:
         * 1-byte major opcode
         * 1 byte of request-specific data
         * 2-byte length (0 if big request)
         * If 2-byte length is zero, 4-byte length.
         * Rest of request-specific data
         */
        return NULL; /* Not yet implemented. */
    }
    else if (direction == XAMINE_RESPONSE) {
        unsigned char response_type;

        if (size < 32)
            return NULL;

        response_type = *data;
        if (response_type == 0) {      /* Error */
            unsigned char error_code = *(data + 1);
            if (error_code < 128)
                definition = conversation->ctx->core_errors[error_code];
            else
                definition = conversation->extension_errors[error_code - 128];
        }
        else if (response_type == 1) { /* Reply */
            return NULL;            /* Not yet implemented. */
        }
        else {                        /* Event */
            /* Turn off SendEvent flag before looking up by event number. */
            const unsigned char event_code = response_type & ~0x80;
            if (event_code < 64)
                definition = conversation->ctx->core_events[event_code];
            else
                definition = conversation->extension_events[event_code - 64];
        }
    }

    if (!definition)
        return NULL;

    /* Dissect the data based on the definition. */
    return xamine_definition(conversation, &data, &size, &offset, definition, NULL);
}

XAMINE_EXPORT void
xamine_item_free(struct xamine_item *item)
{
    if (!item)
        return;

    xamine_item_free(item->child);
    xamine_item_free(item->next);
    free(item);
}
