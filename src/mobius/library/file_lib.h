#ifndef MOBIUS_LIBRARY_FILE_H
#define MOBIUS_LIBRARY_FILE_H

#include "library/library.h"

int lib_readfile(MobiusState* state, int arg_count);
int lib_writefile(MobiusState* state, int arg_count);
int lib_appendfile(MobiusState* state, int arg_count);
int lib_file_exists(MobiusState* state, int arg_count);
int lib_readlines(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_FILE_H
