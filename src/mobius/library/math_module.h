#ifndef MOBIUS_LIBRARY_MATH_MODULE_H
#define MOBIUS_LIBRARY_MATH_MODULE_H

class MobiusState;
class Table;

// A new table of the math module (`import "math"`): its functions and
// constants. One of the built-in modules (plugin/module_registry.cpp).
Table* create_math_module(MobiusState* state);

#endif // MOBIUS_LIBRARY_MATH_MODULE_H
