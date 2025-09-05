#include "src/mobius/table.h"
#include "src/mobius/ast.h"
#include <stdio.h>

int main() {
    printf("Testing basic table functionality...\n");
    
    // Create a table
    Table* table = create_table(8);
    if (!table) {
        printf("Failed to create table\n");
        return 1;
    }
    
    printf("✅ Table created successfully\n");
    printf("Initial size: %zu, capacity: %zu\n", table->size, table->capacity);
    
    // Create some test values
    char* key1_str = malloc(6);
    strcpy(key1_str, "name");
    Value key1 = make_string_value(key1_str);
    
    char* value1_str = malloc(6);
    strcpy(value1_str, "Alice");
    Value value1 = make_string_value(value1_str);
    
    // Set a value
    bool success = table_set(table, key1, value1);
    printf("✅ table_set result: %s\n", success ? "success" : "failed");
    printf("Table size after insert: %zu\n", table->size);
    
    // Get the value back
    Value retrieved = table_get(table, key1);
    if (retrieved.type == VAL_STRING && retrieved.as.string) {
        printf("✅ Retrieved value: %s\n", retrieved.as.string);
    } else {
        printf("❌ Failed to retrieve value\n");
    }
    
    // Test table_has_key
    bool has_key = table_has_key(table, key1);
    printf("✅ table_has_key result: %s\n", has_key ? "true" : "false");
    
    // Print table debug info
    printf("\nTable debug info:\n");
    print_table_debug(table);
    
    // Clean up
    free_table(table);
    printf("✅ Table cleanup completed\n");
    
    return 0;
}
