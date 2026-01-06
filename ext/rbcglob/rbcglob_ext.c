#include <ruby.h>
#include <rbcglob/rbcglob.h>

static VALUE rb_rbcglob_glob(int argc, VALUE *argv, VALUE self)
{
    VALUE pattern_arg, flags_arg, base_arg, sort_arg;
    rb_scan_args(argc, argv, "13", &pattern_arg, &flags_arg, &base_arg, &sort_arg);

    /* Convert pattern to C string */
    Check_Type(pattern_arg, T_STRING);
    const char *pattern = StringValueCStr(pattern_arg);

    /* Parse flags */
    unsigned int flags = 0;
    if (!NIL_P(flags_arg))
    {
        flags = NUM2UINT(flags_arg);
    }

    /* Parse base */
    const char *base = NULL;
    if (!NIL_P(base_arg))
    {
        Check_Type(base_arg, T_STRING);
        base = StringValueCStr(base_arg);
    }

    /* Parse sort flag (default: true) */
    int sort = 1;
    if (!NIL_P(sort_arg))
    {
        sort = RTEST(sort_arg) ? 1 : 0;
    }

    /* Call dirglob */
    char **results = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    const char *patterns[1] = {pattern};

    if (!dirglob(patterns, 1, flags, base, sort, &results, &count, &lengths))
    {
        rb_raise(rb_eRuntimeError, "dirglob failed");
    }

    /* Convert results to Ruby array - zero-copy optimization */
    VALUE ary = rb_ary_new_capa(count);
    for (size_t i = 0; i < count; i++)
    {
        /* Create frozen strings to reduce GC pressure */
        VALUE str = rb_str_new(results[i], lengths[i]);
        OBJ_FREEZE(str);
        rb_ary_store(ary, i, str);
    }

    /* Free results */
    rbcglob_free(results, count, lengths);

    return ary;
}

void Init_rbcglob(void)
{
    VALUE mRbcglob = rb_define_module("Rbcglob");
    rb_define_module_function(mRbcglob, "glob", rb_rbcglob_glob, -1);

    /* Define flag constants compatible with File::FNM_* */
    rb_define_const(mRbcglob, "FNM_NOESCAPE", INT2NUM(1 << 0));
    rb_define_const(mRbcglob, "FNM_PATHNAME", INT2NUM(1 << 1));
    rb_define_const(mRbcglob, "FNM_CASEFOLD", INT2NUM(1 << 2));
    rb_define_const(mRbcglob, "FNM_DOTMATCH", INT2NUM(1 << 3));
    rb_define_const(mRbcglob, "FNM_EXTGLOB", INT2NUM(1 << 4));
}
