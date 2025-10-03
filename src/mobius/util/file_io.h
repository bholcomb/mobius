#ifndef MOBIUS_FILE_IO_H
#define MOBIUS_FILE_IO_H

#include <stddef.h>
#include <stdbool.h>

// File reading result
typedef struct {
    char* content;      // File content (null-terminated)
    size_t size;        // Size of content in bytes
    bool success;       // Whether reading was successful
    const char* error;  // Error message if unsuccessful
} FileResult;

// File I/O functions
FileResult read_file(const char* path);
void free_file_result(FileResult* result);
bool file_exists(const char* path);
const char* get_file_extension(const char* path);

// Script execution
int execute_script_file(const char* path);
int execute_script_string(const char* source, const char* filename);

#endif // MOBIUS_FILE_IO_H
