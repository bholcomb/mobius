#ifndef MOBIUS_LIBRARY_FILE_H
#define MOBIUS_LIBRARY_FILE_H

#include "library/library.h"

int lib_readfile(MobiusState* state, int arg_count, void* /*userdata*/);
int lib_writefile(MobiusState* state, int arg_count, void* /*userdata*/);
int lib_appendfile(MobiusState* state, int arg_count, void* /*userdata*/);
int lib_file_exists(MobiusState* state, int arg_count, void* /*userdata*/);
int lib_readlines(MobiusState* state, int arg_count, void* /*userdata*/);

#endif // MOBIUS_LIBRARY_FILE_H
