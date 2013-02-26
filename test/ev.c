#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <libxml/parser.h>

#include "xamine.h"

#define AllEventsMask 0x01FFFFFF

static void
repeat(char c, int i)
{
    while (i--)
       putchar(c);
}

static void
print_tree(struct xamine_item *xamined, int depth)
{
    if (!xamined)
        return;

    repeat(' ', depth);
    printf("%s %s = ", xamined->definition->name, xamined->name);

    if (xamined->child) {
        printf("{\n");
        print_tree(xamined->child, depth + 4);
        repeat(' ', depth);
        printf("}\n");
    }
    else {
        switch(xamined->definition->type) {
        case XAMINE_BOOL:
            printf("%s\n", xamined->u.bool_value ? "true" : "false");
            break;

        case XAMINE_CHAR:
            printf("'%c'\n", xamined->u.char_value);
            break;

        case XAMINE_SIGNED:
            printf("%ld\n", xamined->u.signed_value);
            break;

        case XAMINE_UNSIGNED:
            printf("%lu\n", xamined->u.unsigned_value);
            break;

        /* TODO */
        case XAMINE_STRUCT:
            printf("<TODO STRUCT>\n");
            break;

        case XAMINE_UNION:
            printf("<TODO UNION>\n");
            break;

        case XAMINE_TYPEDEF:
            printf("<TODO TYPEDEF>\n");
            break;
        }
    }

    print_tree(xamined->next, depth);
}

int
main(void)
{
    xcb_connection_t *conn;
    xcb_screen_t *root;
    xcb_window_t window;
    uint32_t mask;
    uint32_t values[5];
    xcb_generic_event_t *event;
    struct xamine_context *ctx;
    struct xamine_conversation *conversation;

    conn = xcb_connect(NULL, NULL);
    root = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    window = xcb_generate_id(conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_BACKING_STORE | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    values[0] = root->white_pixel;
    values[1] = root->black_pixel;
    values[2] = XCB_BACKING_STORE_ALWAYS;
    values[3] = 0;
    values[4] = AllEventsMask;
    xcb_create_window(conn, 0, window, root->root, 0, 0, 256, 256, 10, XCB_WINDOW_CLASS_INPUT_OUTPUT, root->root_visual, mask, values);
    xcb_map_window(conn, window);
    xcb_flush(conn);

    ctx = xamine_context_new(0);
    conversation = xamine_conversation_new(ctx, 0);

    while ((event = xcb_wait_for_event(conn)) != NULL) {
        struct xamine_item *item = xamine_examine(conversation, XAMINE_RESPONSE, event, 32);
        free(event);

        print_tree(item, 0);

        /* Exit on ESC. */
        if (strcmp(item->definition->name, "KeyPress") == 0 &&
            item->child->next->u.unsigned_value == 9) {
            xamine_item_free(item);
            break;
        }

        xamine_item_free(item);
    }

    xamine_conversation_unref(conversation);
    xamine_context_unref(ctx);
    xcb_disconnect(conn);
    xmlCleanupParser();

    return 0;
}
