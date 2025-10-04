#ifndef MOBIUS_STACK_H
#define MOBIUS_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "data/value.h"  // For ValueType

// Forward declarations
typedef struct MobiusState MobiusState;

// ============================================================================
// STACK INSPECTION
// ============================================================================

/**
 * Get the current size of the stack
 * @param state The Mobius state
 * @return Number of values on the stack
 */
int mobius_stack_size(MobiusState* state);

/**
 * Get the type of value at stack index
 * @param state The Mobius state
 * @param idx Stack index (0-based, negative indices from top: -1 = top)
 * @return ValueType enum value, or VAL_NIL if index out of bounds
 */
ValueType mobius_stack_type(MobiusState* state, int idx);

/**
 * Check if value at index is a number (integer or float)
 */
bool mobius_stack_isNumber(MobiusState* state, int idx);

/**
 * Check if value at index is an integer
 */
bool mobius_stack_isInteger(MobiusState* state, int idx);

/**
 * Check if value at index is a float
 */
bool mobius_stack_isFloat(MobiusState* state, int idx);

/**
 * Check if value at index is a string
 */
bool mobius_stack_isString(MobiusState* state, int idx);

/**
 * Check if value at index is a boolean
 */
bool mobius_stack_isBool(MobiusState* state, int idx);

/**
 * Check if value at index is nil
 */
bool mobius_stack_isNil(MobiusState* state, int idx);

/**
 * Check if value at index is a table
 */
bool mobius_stack_isTable(MobiusState* state, int idx);

/**
 * Check if value at index is an array
 */
bool mobius_stack_isArray(MobiusState* state, int idx);

/**
 * Check if value at index is a function
 */
bool mobius_stack_isFunction(MobiusState* state, int idx);

// ============================================================================
// STACK GETTERS - PERMISSIVE (always convert)
// ============================================================================

/**
 * Get value as int8 with automatic conversion
 * Converts: other integers, floats (truncate), strings (parse), bool (1/0)
 * Prints error and exits if conversion impossible or index invalid
 */
int8_t mobius_stack_asInt8(MobiusState* state, int idx);
uint8_t mobius_stack_asUInt8(MobiusState* state, int idx);
int16_t mobius_stack_asInt16(MobiusState* state, int idx);
uint16_t mobius_stack_asUInt16(MobiusState* state, int idx);
int32_t mobius_stack_asInt32(MobiusState* state, int idx);
uint32_t mobius_stack_asUInt32(MobiusState* state, int idx);
int64_t mobius_stack_asInt64(MobiusState* state, int idx);
uint64_t mobius_stack_asUInt64(MobiusState* state, int idx);

/**
 * Get value as float with automatic conversion
 * Converts: integers, other floats, strings (parse)
 */
float mobius_stack_asFloat32(MobiusState* state, int idx);
double mobius_stack_asFloat64(MobiusState* state, int idx);

/**
 * Get value as boolean with automatic conversion
 * nil/0/empty string -> false, everything else -> true
 */
bool mobius_stack_asBool(MobiusState* state, int idx);

/**
 * Get value as string with automatic conversion
 * Returns borrowed pointer (valid until stack modified - caller must copy if needed)
 * Converts: numbers to string, bool to "true"/"false", etc.
 */
const char* mobius_stack_asString(MobiusState* state, int idx);

// ============================================================================
// STACK GETTERS - STRICT (respects strict_types pragma)
// ============================================================================

/**
 * Get value as int32, failing if conversion needed in strict mode
 * In non-strict mode, behaves like mobius_stack_asInt32
 * In strict mode, prints error and exits if type doesn't match exactly
 */
int8_t mobius_stack_getInt8(MobiusState* state, int idx);
uint8_t mobius_stack_getUInt8(MobiusState* state, int idx);
int16_t mobius_stack_getInt16(MobiusState* state, int idx);
uint16_t mobius_stack_getUInt16(MobiusState* state, int idx);
int32_t mobius_stack_getInt32(MobiusState* state, int idx);
uint32_t mobius_stack_getUInt32(MobiusState* state, int idx);
int64_t mobius_stack_getInt64(MobiusState* state, int idx);
uint64_t mobius_stack_getUInt64(MobiusState* state, int idx);
float mobius_stack_getFloat32(MobiusState* state, int idx);
double mobius_stack_getFloat64(MobiusState* state, int idx);
bool mobius_stack_getBool(MobiusState* state, int idx);
const char* mobius_stack_getString(MobiusState* state, int idx);

// ============================================================================
// STACK PUSH OPERATIONS
// ============================================================================

void mobius_stack_pushInt8(MobiusState* state, int8_t value);
void mobius_stack_pushUInt8(MobiusState* state, uint8_t value);
void mobius_stack_pushInt16(MobiusState* state, int16_t value);
void mobius_stack_pushUInt16(MobiusState* state, uint16_t value);
void mobius_stack_pushInt32(MobiusState* state, int32_t value);
void mobius_stack_pushUInt32(MobiusState* state, uint32_t value);
void mobius_stack_pushInt64(MobiusState* state, int64_t value);
void mobius_stack_pushUInt64(MobiusState* state, uint64_t value);
void mobius_stack_pushFloat32(MobiusState* state, float value);
void mobius_stack_pushFloat64(MobiusState* state, double value);
void mobius_stack_pushBool(MobiusState* state, bool value);

/**
 * Push string onto stack (creates internal copy)
 */
void mobius_stack_pushString(MobiusState* state, const char* str);

/**
 * Push nil onto stack
 */
void mobius_stack_pushNil(MobiusState* state);

/**
 * Create and push a new empty table onto stack
 * @param capacity Initial capacity hint (0 for default)
 */
void mobius_stack_pushNewTable(MobiusState* state, size_t capacity);

/**
 * Create and push a new empty array onto stack
 * @param capacity Initial capacity hint (0 for default)
 */
void mobius_stack_pushNewArray(MobiusState* state, size_t capacity);

// ============================================================================
// VARIABLE OPERATIONS
// ============================================================================

/**
 * Get a variable from the environment and push onto stack
 * Searches from current scope up to global scope
 * Pushes nil if variable not found
 * Stack: ... -> ... [value]
 * 
 * @param state The Mobius state
 * @param name Variable name
 */
void mobius_stack_getVariable(MobiusState* state, const char* name);

/**
 * Get a global variable and push onto stack
 * Only searches global scope
 * Pushes nil if variable not found
 * Stack: ... -> ... [value]
 * 
 * @param state The Mobius state
 * @param name Variable name
 */
void mobius_stack_getGlobal(MobiusState* state, const char* name);

/**
 * Pop value from stack and set as variable in current scope
 * Stack: ... [value] -> ...
 * 
 * @param state The Mobius state
 * @param name Variable name
 */
void mobius_stack_setVariable(MobiusState* state, const char* name);

/**
 * Pop value from stack and set as global variable
 * Stack: ... [value] -> ...
 * 
 * @param state The Mobius state
 * @param name Variable name
 */
void mobius_stack_setGlobal(MobiusState* state, const char* name);

// ============================================================================
// TABLE OPERATIONS (on stack)
// ============================================================================

/**
 * Set field in table at index, using value at top of stack
 * Pops the value from stack
 * Stack: ... [table] ... [value] -> ... [table] ...
 * 
 * @param state The Mobius state
 * @param table_idx Index of table on stack
 * @param key Field name
 */
void mobius_stack_setTableField(MobiusState* state, int table_idx, const char* key);

/**
 * Get field from table at index and push onto stack
 * Stack: ... [table] ... -> ... [table] ... [value]
 * 
 * @param state The Mobius state
 * @param table_idx Index of table on stack
 * @param key Field name
 */
void mobius_stack_getTableField(MobiusState* state, int table_idx, const char* key);

// ============================================================================
// ARRAY OPERATIONS (on stack)
// ============================================================================

/**
 * Set element in array at index, using value at top of stack
 * Pops the value from stack
 * Stack: ... [array] ... [value] -> ... [array] ...
 * 
 * @param state The Mobius state
 * @param array_idx Index of array on stack
 * @param element_idx Array element index (0-based)
 */
void mobius_stack_setArrayElement(MobiusState* state, int array_idx, size_t element_idx);

/**
 * Get element from array at index and push onto stack
 * Stack: ... [array] ... -> ... [array] ... [value]
 * 
 * @param state The Mobius state
 * @param array_idx Index of array on stack
 * @param element_idx Array element index (0-based)
 */
void mobius_stack_getArrayElement(MobiusState* state, int array_idx, size_t element_idx);

/**
 * Get length of array at index
 * 
 * @param state The Mobius state
 * @param array_idx Index of array on stack
 * @return Array length, or 0 if not an array
 */
size_t mobius_stack_getArrayLength(MobiusState* state, int array_idx);

// ============================================================================
// STACK MANIPULATION
// ============================================================================

/**
 * Pop N values from stack
 */
void mobius_stack_pop(MobiusState* state, int count);

/**
 * Duplicate value at index and push onto top
 * Stack: ... [value] ... -> ... [value] ... [value]
 */
void mobius_stack_copy(MobiusState* state, int idx);

#endif // MOBIUS_STACK_H

