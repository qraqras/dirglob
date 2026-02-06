#include <ruby.h>
#include <rbc/rbc.h>
#include <stdlib.h>

static VALUE mRBCGlob;

/*
 * call-seq:
 *   RBCGlob.glob(pattern, flags=0, base: nil) -> Array
 *
 * rbc_globを使ってファイルパスを取得
 */
static VALUE rbcglob_glob(int argc, VALUE *argv, VALUE self)
{
    VALUE pattern, flags_val, kwargs;
    rb_scan_args(argc, argv, "1:", &pattern, &kwargs);

    Check_Type(pattern, T_STRING);

    // フラグの取得
    unsigned flags = 0;
    const char *base = NULL;

    if (!NIL_P(kwargs))
    {
        VALUE flags_v = rb_hash_aref(kwargs, ID2SYM(rb_intern("flags")));
        VALUE base_v = rb_hash_aref(kwargs, ID2SYM(rb_intern("base")));

        if (!NIL_P(flags_v))
        {
            flags = NUM2UINT(flags_v);
        }
        if (!NIL_P(base_v))
        {
            Check_Type(base_v, T_STRING);
            base = StringValueCStr(base_v);
        }
    }

    const char *pat = StringValueCStr(pattern);
    const char *patterns[] = {pat};

    char **out = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    bool success = rbc_glob(patterns, 1, flags, base, true, &out, &count, &lengths);

    if (!success)
    {
        return rb_ary_new();
    }

    VALUE result = rb_ary_new_capa(count);
    for (size_t i = 0; i < count; i++)
    {
        rb_ary_push(result, rb_str_new(out[i], lengths[i]));
    }

    rbc_globfree(out, count, lengths);

    return result;
}

void Init_rbcglob(void)
{
    mRBCGlob = rb_define_module("RBCGlob");
    rb_define_singleton_method(mRBCGlob, "glob", rbcglob_glob, -1);
}
