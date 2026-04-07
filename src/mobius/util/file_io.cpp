#include "util/file_io.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Read entire file into memory
FileResult read_file(const char* path) {
    FileResult result = {0};
    
    if (!path) {
        result.error = "Invalid file path";
        return result;
    }
    
    FILE* file = fopen(path, "r");
    if (!file) {
        result.error = "Could not open file";
        return result;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(file);
        result.error = "Could not determine file size";
        return result;
    }
    
    // Allocate buffer
    result.content = (char*)malloc(file_size + 1);
    if (!result.content) {
        fclose(file);
        result.error = "Memory allocation failed";
        return result;
    }
    
    // Read file content
    size_t bytes_read = fread(result.content, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(result.content);
        result.content = NULL;
        result.error = "Failed to read entire file";
        return result;
    }
    
    // Null-terminate the content
    result.content[file_size] = '\0';
    result.size = file_size;
    result.success = true;
    
    return result;
}

// Free file result
void free_file_result(FileResult* result) {
    if (result && result->content) {
        free(result->content);
        result->content = NULL;
        result->size = 0;
        result->success = false;
    }
}

// Check if file exists
bool file_exists(const char* path) {
    if (!path) return false;
    
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Get file extension
const char* get_file_extension(const char* path) {
    if (!path) return NULL;
    
    const char* last_dot = strrchr(path, '.');
    if (!last_dot || last_dot == path) return NULL;
    
    return last_dot + 1;
}

