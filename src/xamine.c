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
#include <glob.h>
#include <libxml/parser.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "strsplit.h"
#include "xamine.h"

const char *XAMINE_PATH_DEFAULT = "/usr/include/X11/XCB:"
                                  "/usr/include/X11/XCB/extensions";
const char *XAMINE_PATH_DELIM = ":";
const char *XAMINE_PATH_GLOB = "/*.xml";

/* Concrete definitions for opaque and private structure types. */
typedef struct XamineEvent
{
    unsigned char number;
    XamineDefinition *definition;
    struct XamineEvent *next;
} XamineEvent;

typedef struct XamineError
{
    unsigned char number;
    XamineDefinition *definition;
    struct XamineError *next;
} XamineError;

typedef struct XamineExtension
{
    char *name;
    char *xname;
    XamineEvent *events;
    XamineError *errors;
    struct XamineExtension *next;
} XamineExtension;

struct XamineState
{
    unsigned char host_is_le;
    XamineDefinition *definitions;
    XamineDefinition *core_events[64];  /* Core events 2-63 (0-1 unused) */
    XamineDefinition *core_errors[128]; /* Core errors 0-127             */
    XamineExtension  *extensions;
};

struct XamineConversation
{
    XamineState *state;
    unsigned char is_le;
    XamineDefinition *extension_events[64];  /* Extension events 64-127  */
    XamineDefinition *extension_errors[128]; /* Extension errors 128-255 */
    XamineExtension *extensions[128];        /* Extensions 128-255       */
};

static const XamineDefinition core_type_definitions[] =
{
    { "char",   XAMINE_CHAR,     1 },
    { "BOOL",   XAMINE_BOOLEAN,  1 },
    { "BYTE",   XAMINE_UNSIGNED, 1 },
    { "CARD8",  XAMINE_UNSIGNED, 1 },
    { "CARD16", XAMINE_UNSIGNED, 2 },
    { "CARD32", XAMINE_UNSIGNED, 4 },
    { "INT8",   XAMINE_SIGNED,   1 },
    { "INT16",  XAMINE_SIGNED,   2 },
    { "INT32",  XAMINE_SIGNED,   4 }
};

static void xamine_parse_xmlxcb_file(XamineState *state, char *filename);
static char* xamine_make_name(XamineExtension *extension, char *name);
static XamineDefinition *xamine_find_type(XamineState *state, char *name);
static xmlNode *xamine_xml_next_elem(xmlNode *elem);
static XamineFieldDefinition *xamine_parse_fields(XamineState *state,
                                                  xmlNode *elem);
static XamineExpression *xamine_parse_expression(XamineState *state,
                                                 xmlNode *elem);
static XaminedItem *xamine_definition(XamineConversation *conversation,
                                      void **data, unsigned int *size,
                                      unsigned int *offset,
                                      XamineDefinition *definition,
                                      XaminedItem *parent);
static XaminedItem *xamine_field_definition(XamineConversation *conversation,
                                            void **data, unsigned int *size,
                                            unsigned int *offset,
                                            XamineFieldDefinition *definition,
                                            XaminedItem *parent);

/* Initialization and cleanup */
XamineState *xamine_init()
{
    int i;
    const char *xamine_path_env;
    char **xamine_path;
    char **iter;
    glob_t xml_files;
    XamineState *state = calloc(1, sizeof(XamineState));

    if(!state)
        return NULL;

    {
        unsigned long l = 1;
        state->host_is_le = *(unsigned char*)&l;
    }

    /* Add definitions of core types. */
    for(i = 0; i < sizeof(core_type_definitions)/sizeof(XamineDefinition); i++)
    {
        XamineDefinition *temp = calloc(1, sizeof(XamineDefinition));
        if(!temp)
        {
            xamine_cleanup(state);
            return NULL;
        }

        *temp = core_type_definitions[i];
        
        temp->next = state->definitions;
        state->definitions = temp;
        
        temp->name = strdup(core_type_definitions[i].name);
        if(!temp->name)
        {
            xamine_cleanup(state);
            return NULL;
        }
    }

    /* Set up the search path for XML-XCB descriptions. */
    xamine_path_env = getenv("XAMINE_PATH");
    if(!xamine_path_env)
        xamine_path_env = XAMINE_PATH_DEFAULT;
    xamine_path = strsplit(xamine_path_env, XAMINE_PATH_DELIM);
    if(!xamine_path)
    {
        xamine_cleanup(state);
        return NULL;
    }

    /* Find all the XML files on the search path. */
    xml_files.gl_pathv = NULL;
    for(iter = xamine_path; *iter; iter++)
    {
        char *pattern = malloc(1+strlen(*iter)+strlen(XAMINE_PATH_GLOB));
        if(!pattern)
        {
            if(xml_files.gl_pathv)
                globfree(&xml_files);
            strsplit_free(xamine_path);
            xamine_cleanup(state);
            return NULL;
        }

        strcpy(pattern, *iter);
        strcat(pattern, XAMINE_PATH_GLOB);
        
        glob(pattern, (xml_files.gl_pathv ? GLOB_APPEND : 0), NULL,
             &xml_files);
    }

    strsplit_free(xamine_path);

    /* Parse the XML files. */
    if(xml_files.gl_pathv)
    {
        for(iter = xml_files.gl_pathv; *iter; iter++)
            xamine_parse_xmlxcb_file(state, *iter);
    }

    globfree(&xml_files);

    return state;
}

void xamine_cleanup(XamineState *state)
{
    XamineDefinition *temp;
    while(state->definitions)
    {
        temp = state->definitions;
        state->definitions = state->definitions->next;
        free(temp);
    }
    /* FIXME: incomplete */
}

/* Retrieval of the type definitions. */
XamineDefinition *xamine_get_definitions(XamineState *state)
{
    if(state == NULL)
        return NULL;

    return state->definitions;
}

/* Creation and destruction of conversations. */
XamineConversation *xamine_create_conversation(XamineState *state)
{
    XamineConversation *conversation = calloc(1, sizeof(XamineConversation));
    if(conversation == NULL)
        return NULL;
    conversation->state = state;
    /* FIXME */
    conversation->is_le = state->host_is_le;
    return conversation;
}

void xamine_free_conversation(XamineConversation *conversation)
{
    free(conversation);
}

/* Analysis */
XaminedItem *xamine(XamineConversation *conversation, XamineDirection dir,
                     void *data, unsigned int size)
{
    XamineDefinition *definition = NULL;
    
    if(dir == XAMINE_REQUEST)
    {
        /* Request layout:
         * 1-byte major opcode
         * 1 byte of request-specific data
         * 2-byte length (0 if big request)
         * If 2-byte length is zero, 4-byte length.
         * Rest of request-specific data
         */
        return NULL; /* Not yet implemented. */
    }
    else if(dir == XAMINE_RESPONSE)
    {
        unsigned char response_type;
        
        if(size < 32)
            return NULL;

        response_type = *(unsigned char *)data;
        if(response_type == 0)      /* Error */
        {
            unsigned char error_code = *(unsigned char*)(data+1);
            if(error_code < 128)
                definition = conversation->state->core_errors[error_code];
            else
                definition = conversation->extension_errors[error_code-128];
        }
        else if(response_type == 1) /* Reply */
            return NULL;            /* Not yet implemented. */
        else                        /* Event */
        {
            /* Turn off SendEvent flag before looking up by event number. */
            unsigned char event_code = response_type & ~0x80;
            if(event_code < 64)
                definition = conversation->state->core_events[event_code];
            else
                definition = conversation->extension_events[event_code-64];
        }
    }

    if(definition == NULL)
        return NULL;

    /* Dissect the data based on the definition. */
    int offset = 0;
    return xamine_definition(conversation, &data, &size, &offset,
                             definition, NULL);
}

void xamine_free(XaminedItem *item)
{
    if(item)
    {
        xamine_free(item->child);
        xamine_free(item->next);
        free(item);
    }
}

/********** Private functions **********/

static void xamine_parse_xmlxcb_file(XamineState *state, char *filename)
{
    xmlDoc  *doc;
    xmlNode *root, *elem;
    char *extension_xname;
    XamineExtension *extension = NULL;
    
    /* FIXME: Remove this. */
    printf("DEBUG: Parsing file \"%s\"\n", filename);
    
    /* Ignore text nodes consisting entirely of whitespace. */
    xmlKeepBlanksDefault(0); 
    
    doc = xmlParseFile(filename);
    if(!doc)
        return;

    root = xmlDocGetRootElement(doc);
    if(!root)
        return;

    extension_xname = xmlGetProp(root, "extension-xname");

    if(extension_xname)
    {
        /* FIXME: Remove this. */
        printf("Extension: %s\n", extension_xname);

        for(extension = state->extensions;
            extension != NULL;
            extension = extension->next)
        {
            if(strcmp(extension->xname, extension_xname) == 0)
                break;
        }
        
        if(extension == NULL)
        {
            extension = calloc(1, sizeof(XamineExtension));
            extension->name = strdup(xmlGetProp(root, "extension-name"));
            extension->xname = strdup(extension_xname);
            extension->next = state->extensions;
            state->extensions = extension;
        }
    }
    else                           /* FIXME: Remove this. */
        printf("Core Protocol\n");
    
    for(elem = root->children; elem != NULL;
        elem = xamine_xml_next_elem(elem->next))
    {
        /* FIXME: Remove this */
        {
            char *name = xmlGetProp(elem, "name");
            printf("DEBUG:    Parsing element \"%s\", name=\"%s\"\n",
                   elem->name, name ? name : "<not present>");
        }
        
        if(strcmp(elem->name, "request") == 0)
        {
            /* Not yet implemented. */
        }
        else if(strcmp(elem->name, "event") == 0)
        {
            char *no_sequence_number;
            XamineDefinition *def;
            XamineFieldDefinition *fields;
            int number = atoi(xmlGetProp(elem, "number"));
            if(number > 64)
                continue;
            def = calloc(1, sizeof(XamineDefinition));
            def->name = xamine_make_name(extension, xmlGetProp(elem, "name"));
            def->type = XAMINE_STRUCT;
            
            fields = xamine_parse_fields(state, elem);
            if(fields == NULL)
            {
                fields = calloc(1, sizeof(XamineFieldDefinition));
                fields->name = strdup("pad");
                fields->definition = xamine_find_type(state, "CARD8");
            }
            def->fields = calloc(1, sizeof(XamineFieldDefinition));
            def->fields->name = strdup("response_type");
            def->fields->definition = xamine_find_type(state, "BYTE");
            def->fields->next = fields;
            fields = fields->next;
            no_sequence_number = xmlGetProp(elem, "no-sequence-number");
            if(no_sequence_number && strcmp(no_sequence_number, "true") == 0)
            {
                def->fields->next->next = fields;
            }
            else
            {
                def->fields->next->next =
                    calloc(1, sizeof(XamineFieldDefinition));
                def->fields->next->next->name = strdup("sequence");
                def->fields->next->next->definition =
                    xamine_find_type(state, "CARD16");
                def->fields->next->next->next = fields;
            }
            def->next = state->definitions;
            state->definitions = def;
            
            if(extension)
            {
                XamineEvent *event = calloc(1, sizeof(XamineEvent));
                event->number = number;
                event->definition = def;
                event->next = extension->events;
            }
            else
                state->core_events[number] = def;
        }
        else if(strcmp(elem->name, "eventcopy") == 0)
        {
            XamineDefinition *def;
            int number = atoi(xmlGetProp(elem, "number"));
            if(number > 64)
                continue;
            def = calloc(1, sizeof(XamineDefinition));
            def->name = strdup(xmlGetProp(elem, "name"));
            def->type = XAMINE_TYPEDEF;
            def->ref = xamine_find_type(state, xmlGetProp(elem, "ref"));
            
            if(extension)
            {
                XamineEvent *event = calloc(1, sizeof(XamineEvent));
                event->number = number;
                event->definition = def;
                event->next = extension->events;
            }
            else
                state->core_events[number] = def;
        }
        else if(strcmp(elem->name, "error") == 0)
        {
        }
        else if(strcmp(elem->name, "errorcopy") == 0)
        {
        }
        else if(strcmp(elem->name, "struct") == 0)
        {
            XamineDefinition *def = calloc(1, sizeof(XamineDefinition));
            def->name = xamine_make_name(extension, xmlGetProp(elem, "name"));
            def->type = XAMINE_STRUCT;
            def->fields = xamine_parse_fields(state, elem);
            def->next = state->definitions;
            state->definitions = def;
        }
        else if(strcmp(elem->name, "union") == 0)
        {
        }
        else if(strcmp(elem->name, "xidtype") == 0)
        {
            XamineDefinition *def = calloc(1, sizeof(XamineDefinition));
            def->name = xamine_make_name(extension, xmlGetProp(elem, "name"));
            def->type = XAMINE_UNSIGNED;
            def->size = 4;
            def->next = state->definitions;
            state->definitions = def;
        }
        else if(strcmp(elem->name, "enum") == 0)
        {
        }
        else if(strcmp(elem->name, "typedef") == 0)
        {
            XamineDefinition *def = calloc(1, sizeof(XamineDefinition));
            def->name = xamine_make_name(extension,
                                         xmlGetProp(elem, "newname"));
            def->type = XAMINE_TYPEDEF;
            def->ref = xamine_find_type(state, xmlGetProp(elem, "oldname"));
            def->next = state->definitions;
            state->definitions = def;
        }
        else if(strcmp(elem->name, "import") == 0)
        {
        }
    }
}

static char* xamine_make_name(XamineExtension *extension, char *name)
{
    if(extension)
    {
        char *temp = malloc(strlen(extension->name) + strlen(name) + 1);
        if(temp == NULL)
            return NULL;
        strcpy(temp, extension->name);
        strcat(temp, name);
        return temp;
    }
    else
        return strdup(name);
}

static XamineDefinition *xamine_find_type(XamineState *state, char *name)
{
    XamineDefinition *def;
    for(def = state->definitions; def != NULL; def = def->next)
    {
        /* FIXME: does not work for extension types. */
        if(strcmp(def->name, name) == 0)
            return def;
    }
    return NULL;
}

static xmlNode *xamine_xml_next_elem(xmlNode *elem)
{
    while(elem && elem->type != XML_ELEMENT_NODE)
        elem = elem->next;
    return elem;
}

static XamineFieldDefinition *xamine_parse_fields(XamineState *state,
                                                  xmlNode *elem)
{
    xmlNode *cur;
    XamineFieldDefinition *head;
    XamineFieldDefinition **tail = &head;
    for(cur = elem->children; cur!=NULL; cur = xamine_xml_next_elem(cur->next))
    {
        /* FIXME: handle elements other than "field", "pad", and "list". */
        *tail = calloc(1, sizeof(XamineFieldDefinition));
        if(strcmp(cur->name, "pad") == 0)
        {
            (*tail)->name = "pad";
            (*tail)->definition = xamine_find_type(state, "CARD8");
            (*tail)->length = calloc(1, sizeof(XamineExpression));
            (*tail)->length->type = XAMINE_VALUE;
            (*tail)->length->value = atoi(xmlGetProp(cur, "bytes"));
        }
        else
        {
            (*tail)->name = strdup(xmlGetProp(cur, "name"));
            (*tail)->definition = xamine_find_type(state,
                                               xmlGetProp(cur, "type"));
            /* FIXME: handle missing length expressions. */
            if(strcmp(cur->name, "list") == 0)
                (*tail)->length = xamine_parse_expression(state,
                                                          cur->children);
        }
        tail = &((*tail)->next);
    }
    
    *tail = NULL;
    return head;
}

static XamineExpression *xamine_parse_expression(XamineState *state,
                                                 xmlNode *elem)
{
    XamineExpression *e = calloc(1, sizeof(XamineExpression));
    elem = xamine_xml_next_elem(elem);
    if(strcmp(elem->name, "op") == 0)
    {
        char *temp = xmlGetProp(elem, "op");
        e->type = XAMINE_OP;
        if(strcmp(temp, "+") == 0)
            e->op = XAMINE_ADD;
        else if(strcmp(temp, "-") == 0)
            e->op = XAMINE_SUBTRACT;
        else if(strcmp(temp, "*") == 0)
            e->op = XAMINE_MULTIPLY;
        else if(strcmp(temp, "/") == 0)
            e->op = XAMINE_DIVIDE;
        else if(strcmp(temp, "<<") == 0)
            e->op = XAMINE_LEFT_SHIFT;
        else if(strcmp(temp, "&") == 0)
            e->op = XAMINE_BITWISE_AND;
        elem = xamine_xml_next_elem(elem->children);
        e->left = xamine_parse_expression(state, elem);
        elem = xamine_xml_next_elem(elem->next);
        e->right = xamine_parse_expression(state, elem);
    }
    else if(strcmp(elem->name, "value") == 0)
    {
        e->type = XAMINE_VALUE;
        e->value = strtol(elem->children->content, NULL, 0);
    }
    else if(strcmp(elem->name, "fieldref") == 0)
    {
        e->type = XAMINE_FIELDREF;
        e->field = strdup(elem->children->content);
    }
    return e;
}

static long xamine_evaluate_expression(XamineExpression *expression,
                                       XaminedItem *parent)
{
    switch(expression->type)
    {
    case XAMINE_VALUE:
        return expression->value;
        
    case XAMINE_FIELDREF:
    {
        XaminedItem *cur;
        for(cur = parent->child; cur != NULL; cur = cur->next)
            if(strcmp(cur->name, expression->field) == 0)
                switch(cur->definition->type)
                {
                case XAMINE_BOOLEAN: return cur->bool_value;
                case XAMINE_CHAR: return cur->char_value;
                case XAMINE_SIGNED: return cur->signed_value;
                case XAMINE_UNSIGNED: return cur->unsigned_value;
                }
        /* FIXME: handle not found or wrong type */
    }

    case XAMINE_OP:
    {
        long left  = xamine_evaluate_expression(expression->left, parent);
        long right = xamine_evaluate_expression(expression->right, parent);
        switch(expression->op)
        {
        case XAMINE_ADD:         return left+right;
        case XAMINE_SUBTRACT:    return left-right;
        case XAMINE_MULTIPLY:    return left*right;
        case XAMINE_DIVIDE:      return left/right; /* FIXME: divide by zero */
        case XAMINE_LEFT_SHIFT:  return left<<right;
        case XAMINE_BITWISE_AND: return left&right;
        }
    }
    }
}

static XaminedItem *xamine_definition(XamineConversation *conversation,
                                      void **data, unsigned int *size,
                                      unsigned int *offset,
                                      XamineDefinition *definition,
                                      XaminedItem *parent)
{
    XaminedItem *xamined;

    if(definition->type == XAMINE_TYPEDEF)
    {
        xamined = xamine_definition(conversation, data, size, offset,
                                    definition->ref, parent);
        xamined->definition = definition;
        return xamined;
    }

    xamined = calloc(1, sizeof(XaminedItem));
    xamined->definition = definition;
    if(definition->type == XAMINE_STRUCT)
    {
        XaminedItem **end = &(xamined->child);
        XamineFieldDefinition *child;
        for(child = definition->fields; child != NULL; child = child->next)
        {
            *end = xamine_field_definition(conversation, data, size, offset,
                                           child, xamined);
            end = &((*end)->next);
        }
        *end = NULL;
    }
    else
    {
        switch(definition->type)
        {
        case XAMINE_BOOLEAN:
            /* FIXME: field->definition->size must be 1 */
            xamined->bool_value = *(unsigned char*)(*data) ? 1 : 0;
            break;
        
        case XAMINE_CHAR:
            /* FIXME: field->definition->size must be 1 */
            xamined->char_value = *(char *)(*data);
            break;
            
        case XAMINE_SIGNED:
        case XAMINE_UNSIGNED:
        {
            unsigned char *dest = definition->type == XAMINE_SIGNED
                                ? (unsigned char *)&(xamined->signed_value)
                                : (unsigned char *)&(xamined->unsigned_value);
            unsigned char *src = (unsigned char*)(*data);
            if(definition->size == 1
              || conversation->is_le == conversation->state->host_is_le)
                memcpy(dest, src, definition->size);
            else
            {
                int i;
                dest += definition->size-1;
                for(i = 0; i < definition->size; i++)
                    *dest-- = *src++;
            }
        }
        }
        *data   += definition->size;
        *size   -= definition->size;
        *offset += definition->size;
    }

    return xamined;
}

static XaminedItem *xamine_field_definition(XamineConversation *conversation,
                                            void **data, unsigned int *size,
                                            unsigned int *offset,
                                            XamineFieldDefinition *field,
                                            XaminedItem *parent)
{
    XaminedItem *xamined;
    
    if(field->length)
    {
        xamined = calloc(1, sizeof(XaminedItem));
        xamined->name = field->name;
        xamined->definition = field->definition;
        xamined->offset = *offset;
        XaminedItem **end = &(xamined->child);
        unsigned long length = xamine_evaluate_expression(field->length,
                                                          parent);
        unsigned long i;
        for(i = 0; i < length; i++)
        {
            *end = xamine_definition(conversation, data, size, offset,
                                     field->definition, parent);
            (*end)->name = malloc(23); /* '[', length of 2**64, ']', '\0' */
            sprintf((*end)->name, "[%lu]", i);
            end = &((*end)->next);
        }
        *end = NULL;
    }
    else
    {
        xamined = xamine_definition(conversation, data, size, offset,
                                    field->definition, parent);
        xamined->name = field->name;
    }
    
    return xamined;
}
