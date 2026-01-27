#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Test segment parsing
typedef enum
{
    RBC_SEG_LITERAL,
    RBC_SEG_DOT,
    RBC_SEG_DOTDOT,
    RBC_SEG_ANY,
    RBC_SEG_RECURSIVE,
    RBC_SEG_MAGICAL,
    RBC_SEG_ROOT,
} rbc_segment_type_t;

typedef struct
{
    const char *start;
    size_t len;
    rbc_segment_type_t type;
    bool starts_with_dot;
    size_t trailing_slashes;
    bool is_last_segment;
} rbc_segment_t;

#define SEG_HAS_STAR 0x01
#define SEG_HAS_QUESTION 0x02
#define SEG_HAS_BRACKET 0x04
#define SEG_HAS_ESCAPE 0x08
#define SEG_HAS_REGULAR 0x10

static bool rbc_glob_next_segment(const char **pattern, unsigned flags, rbc_segment_t *seg)
{
    const char *p = *pattern;
    if (*p == '\0')
        return false;

    if (*p == '/')
    {
        seg->start = p;
        seg->len = 1;
        seg->type = RBC_SEG_ROOT;
        seg->starts_with_dot = false;
        seg->trailing_slashes = 0;
        p++;
        *pattern = p;
        return true;
    }

    const char *seg_start = p;
    seg->start = seg_start;
    seg->len = 0;
    seg->type = RBC_SEG_LITERAL;
    seg->starts_with_dot = false;
    seg->trailing_slashes = 0;
    seg->is_last_segment = false;

    if (*p == '.')
    {
        p++;
        seg->starts_with_dot = true;

        if (*p == '\0' || *p == '/')
        {
            seg->type = RBC_SEG_DOT;
            goto segment_end;
        }
        else if (*p == '.' && (*(p + 1) == '\0' || *(p + 1) == '/'))
        {
            seg->type = RBC_SEG_DOTDOT;
            goto segment_end;
        }
    }

    const char *pattern_part = p;
    unsigned char char_flags = 0;
    int in_bracket = 0;

    while (*p)
    {
        switch (*p)
        {
        case '/':
            if (in_bracket == 0)
                goto segment_end;
            break;
        case '*':
            char_flags |= SEG_HAS_STAR;
            break;
        case '?':
            char_flags |= SEG_HAS_QUESTION;
            break;
        case '[':
            char_flags |= SEG_HAS_BRACKET;
            in_bracket++;
            break;
        case ']':
            if (in_bracket > 0)
                in_bracket--;
            else
                char_flags |= SEG_HAS_REGULAR;
            break;
        default:
            char_flags |= SEG_HAS_REGULAR;
            break;
        }
        p++;
    }

segment_end:
    seg->len = p - seg_start;
    size_t pattern_len = p - pattern_part;

    if (char_flags == SEG_HAS_STAR && pattern_len == 2)
    {
        if (*p == '\0' || seg->starts_with_dot)
            seg->type = RBC_SEG_ANY;
        else
            seg->type = RBC_SEG_RECURSIVE;
    }
    else if (char_flags == SEG_HAS_STAR && pattern_len >= 1)
        seg->type = RBC_SEG_ANY;
    else if (char_flags == SEG_HAS_REGULAR || char_flags == 0)
        seg->type = RBC_SEG_LITERAL;
    else
        seg->type = RBC_SEG_MAGICAL;

    while (*p == '/')
    {
        seg->trailing_slashes++;
        p++;
    }

    seg->is_last_segment = (*p == '\0');

    *pattern = p;
    return true;
}

int main()
{
    const char *test_patterns[] = {
        "**",
        "**/.*",
        ".*",
        ".**",
    };

    for (int i = 0; i < 4; i++)
    {
        printf("\n=== Pattern: %s ===\n", test_patterns[i]);
        const char *p = test_patterns[i];
        rbc_segment_t seg;
        int seg_num = 0;

        while (rbc_glob_next_segment(&p, 0, &seg))
        {
            printf("Segment %d:\n", seg_num++);
            printf("  Text: %.*s\n", (int)seg.len, seg.start);
            printf("  Type: %d (0=LIT,1=DOT,2=DOTDOT,3=ANY,4=REC,5=MAG,6=ROOT)\n", seg.type);
            printf("  starts_with_dot: %d\n", seg.starts_with_dot);
            printf("  trailing_slashes: %zu\n", seg.trailing_slashes);
            printf("  is_last_segment: %d\n", seg.is_last_segment);
        }
    }

    return 0;
}
