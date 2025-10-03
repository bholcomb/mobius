// Darray.h
// Include this header multiple times to implement a
// simplistic dynamic array.  Before inclusion define at
// least DARRAY_T to the type the dynamic array can hold.
// See DARRAY_NAME, DARRAY_PREFIX and DARRAY_LINKAGE for
// other customization points.
//
// If you define DARRAY_DECLS_ONLY, only the declarations
// of the type and its function will be declared.
//

#ifndef DARRAY_HEADER_H
#define DARRAY_HEADER_H
// Inline functions, #defines and includes that will be
// needed for all instantiations can go up here.
#include <stdlib.h> // realloc, size_t
#include <stdio.h> // for printf
#include <string.h> // for memset

#define DARRAY_IMPL(word) DARRAY_COMB1(DARRAY_PREFIX,word)
#define DARRAY_COMB1(pre, word) DARRAY_COMB2(pre, word)
#define DARRAY_COMB2(pre, word) pre##word

#endif // DARRAY_HEADER_H

// NOTE: this section is *not* guarded as it is intended
// to be included multiple times.

#ifndef DARRAY_T
#error "DARRAY_T must be defined"
#endif

// The name of the data type to be generated.
// If not given, will expand to something like
// `darray_int` for an `int`.
#ifndef DARRAY_NAME
#define DARRAY_NAME DARRAY_COMB1(DARRAY_COMB1(darray,_), DARRAY_T)
#endif

// Prefix for generated functions.
#ifndef DARRAY_PREFIX
#define DARRAY_PREFIX DARRAY_COMB1(DARRAY_NAME, _)
#endif

// Customize the linkage of the function.
#ifndef DARRAY_LINKAGE
#define DARRAY_LINKAGE static inline
#endif

typedef struct DARRAY_NAME DARRAY_NAME;
struct DARRAY_NAME {
    DARRAY_T* items;
    size_t count;
    size_t capacity;
};

#define DARRAY_push DARRAY_IMPL(push)
#define DARRAY_pop DARRAY_IMPL(pop)
#define DARRAY_reserve DARRAY_IMPL(reserve)

#ifdef DARRAY_DECLS_ONLY

DARRAY_LINKAGE
void
DARRAY_push(DARRAY_NAME* array, DARRAY_T item);

DARRAY_LINKAGE
DARRAY_T item
DARRAY_pop(DARRAY_NAME* array);

DARRAY_LINKAGE
void
DARRAY_reserve(DARRAY_NAME* array, size_t count);


#else

DARRAY_LINKAGE
void
DARRAY_push(DARRAY_NAME* array, DARRAY_T item){
    if(array->count >= array->capacity){
        size_t old_cap = array->capacity;
        size_t new_cap = old_cap ? old_cap * 2 : 4;
        size_t new_size = new_cap * sizeof(DARRAY_T);
        array->items = realloc(array->items, new_size);
        array->capacity = new_cap;
    }
    array->items[array->count++] = item;
}

DARRAY_LINKAGE
DARRAY_T*
DARRAY_pop(DARRAY_NAME* array){
    if(array->count == 0)
    {
        printf("Cannot pop an empty array");
        exit(-1);
    }

    DARRAY_T* val = &(array->items[array->count--]);
    return val;
}

DARRAY_LINKAGE
void
DARRAY_reserve(DARRAY_NAME* array, size_t count){
    if(array->count != 0)
    {
        printf("Cannot reserve an array that is not empty");
        exit(-1);
    }

    array->items = malloc(count * sizeof(DARRAY_T));
    memset(array->items, 0, count * sizeof(DARRAY_T));
    array->capacity = count;
    array->count = 0;
}
#endif

// Cleanup
// These need to be undef'ed so they can be redefined the
// next time you need to instantiate this template.
#undef DARRAY_T
#undef DARRAY_PREFIX
#undef DARRAY_NAME
#undef DARRAY_LINKAGE
#undef DARRAY_push
#undef DARRAY_pop
#undef DARRAY_reserve
#ifdef DARRAY_DECLS_ONLY
#undef DARRAY_DECLS_ONLY
#endif