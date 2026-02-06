#include <ruby.h>
#include <rbc/rbc.h>
#include <stdlib.h>
#include <string.h>

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

    rbc_glob_result_t result = {0};

    rbc_glob_status_t status = rbc_glob(patterns, 1, flags, base, true, &result, NULL, NULL);

    if (status != RBC_GLOB_SUCCESS)
    {
        return rb_ary_new();
    }

    VALUE rb_result = rb_ary_new_capa(result.count);
    for (size_t i = 0; i < result.count; i++)
    {
        rb_ary_push(rb_result, rb_str_new_cstr(result.paths[i]));
    }

    rbc_globfree(&result);

    return rb_result;
}

void Init_rbcglob(void)
{
    mRBCGlob = rb_define_module("RBCGlob");
    rb_define_singleton_method(mRBCGlob, "glob", rbcglob_glob, -1);
}
