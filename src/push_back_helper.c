#ifndef WALKER_PUSH_BACK_H
#define WALKER_PUSH_BACK_H

#include "walker.c" // In a real build system this would be structured differently

/*
 * This file is a temporary helper to inject `push_back` into walker.c
 * without completely rewriting the file or doing complex edits.
 * In a real scenario, we would add this function properly to walker.c.
 */

// Implementation of push_back that inserts at the START of the stack (Bottom) instead of End (Top).
// But wait, our stack is an array.
// stack_push pushes to st->items[st->count++]. If we pop from end, that is LIFO (Stack).
// If we want something to execute LATER, we should push it EARLIER (deeper in stack).
// BUT, the main loop processes the TOP frame (st->items[st->count-1]).
// So:
// - Pushing to TOP (push_next) means "Do this NEXT" (Pre-order / Immediate)
// - Pushing to BOTTOM (push_back) means "Do this LAST" (Post-order / Delayed)

// To implement "push_back" on a simple array stack, we need to shift all elements up.
// This is O(N) but N (stack depth) is usually small.

static void stack_push_back(exec_stack_t *st, rbc_segment_t *seg, segment_stack_t *stack_ptr, bool from_wildcard, bool post_recursive, const char *path, size_t path_len, exec_ctx_t *ctx);

static bool push_back(exec_stack_t *st, char *path, size_t path_len, rbc_segment_t *current_seg, segment_stack_t *stack_ptr, exec_ctx_t *ctx, bool from_wildcard, bool post_recursive)
{
    if (current_seg && current_seg->next)
    {
        stack_push_back(st, current_seg->next, stack_ptr, from_wildcard, post_recursive, path, path_len, ctx);
        return true;
    }
    else if (stack_ptr)
    {
        stack_push_back(st, stack_ptr->seg, stack_ptr->next, from_wildcard, post_recursive, path, path_len, ctx);
        return true;
    }
    else
    {
        // Leaf - invoke callback immediately, this doesn't go on stack.
        // Wait, if we want to delay the callback (e.g. "a.txt" after "a/"),
        // we MUST push a frame that simply calls the callback when popped.
        // But our walker logic usually calls match callback inside ST_INIT if !seg.
        // Let's create a dummy frame with seg=NULL that triggers callback in ST_INIT.

        stack_push_back(st, NULL, NULL, from_wildcard, post_recursive, path, path_len, ctx);
        return true;
    }
}

static void stack_push_back(exec_stack_t *st, rbc_segment_t *seg, segment_stack_t *stack_ptr, bool from_wildcard, bool post_recursive, const char *path, size_t path_len, exec_ctx_t *ctx)
{
    if (st->count == st->capacity)
    {
        size_t new_cap = st->capacity ? st->capacity * 2 : 16;
        st->items = realloc(st->items, new_cap * sizeof(frame_t));
        st->capacity = new_cap;
    }

    // Shift all elements by 1
    if (st->count > 0)
    {
        memmove(&st->items[1], &st->items[0], st->count * sizeof(frame_t));
    }

    st->count++;

    // Insert at index 0 (Bottom of stack)
    frame_t *f = &st->items[0];
    memset(f, 0, sizeof(frame_t));
    f->seg = seg;
    f->stack_ptr = stack_ptr;
    f->from_wildcard = from_wildcard;
    f->post_recursive = post_recursive;
    f->state = ST_INIT;

    if (path)
    {
        f->path = rbc_arena_alloc(ctx->arena, path_len + 1);
        memcpy(f->path, path, path_len);
        f->path[path_len] = '\0';
        f->path_len = path_len;
    }
}
#endif
