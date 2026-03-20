#include "data/metamethods.h"
#include "internal/string_intern.h"

Metamethods::Metamethods(StringInternPool* pool) {
    add_      = pool->intern("__add");
    sub_      = pool->intern("__sub");
    mul_      = pool->intern("__mul");
    div_      = pool->intern("__div");
    mod_      = pool->intern("__mod");
    unm_      = pool->intern("__unm");

    band_     = pool->intern("__band");
    bor_      = pool->intern("__bor");
    bxor_     = pool->intern("__bxor");
    bnot_     = pool->intern("__bnot");
    shl_      = pool->intern("__shl");
    shr_      = pool->intern("__shr");

    eq_       = pool->intern("__eq");
    lt_       = pool->intern("__lt");
    le_       = pool->intern("__le");

    index_    = pool->intern("__index");
    newindex_ = pool->intern("__newindex");

    call_     = pool->intern("__call");
    tostring_ = pool->intern("__tostring");
}
