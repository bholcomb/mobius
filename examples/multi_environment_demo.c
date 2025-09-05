#include <stdio.h>
#include <stdlib.h>
#include "mobius/embedding.h"

int main() {
    printf("=== Multi-Environment Demo ===\n");
    printf("Demonstrating isolated environments for game entities\n\n");
    
    // Create three independent environments (like for 3 game entities)
    MobiusState* entity1 = mobius_new_state();
    MobiusState* entity2 = mobius_new_state();
    MobiusState* entity3 = mobius_new_state();
    
    if (!entity1 || !entity2 || !entity3) {
        printf("Failed to create Mobius states\n");
        return 1;
    }
    
    printf("Entity 1 - Loading AI script...\n");
    mobius_exec_string(entity1, 
        "load(\"examples/entity_ai.mob\");\n"
        "var entity_id = 1;\n"
        "var health = 100;\n"
        "print(\"Entity\", entity_id, \"initialized with health:\", health);\n"
        "ai_think();\n");
    
    printf("\nEntity 2 - Loading AI script...\n");
    mobius_exec_string(entity2,
        "load(\"examples/entity_ai.mob\");\n"
        "var entity_id = 2;\n"
        "var health = 75;\n"
        "print(\"Entity\", entity_id, \"initialized with health:\", health);\n"
        "ai_think();\n");
        
    printf("\nEntity 3 - Loading different behavior...\n");
    mobius_exec_string(entity3,
        "load(\"examples/entity_ai.mob\");\n"
        "var entity_id = 3;\n"
        "var health = 50;\n"
        "print(\"Entity\", entity_id, \"initialized with health:\", health);\n"
        "ai_think();\n");
        
    printf("\n=== Demonstrating Isolation ===\n");
    printf("Entity 1 modifying its health...\n");
    mobius_exec_string(entity1, "health = 90; print(\"Entity 1 health:\", health);");
    
    printf("Entity 2 health should be unchanged...\n");
    mobius_exec_string(entity2, "print(\"Entity 2 health:\", health);");
    
    printf("Entity 3 health should also be unchanged...\n");
    mobius_exec_string(entity3, "print(\"Entity 3 health:\", health);");
    
    // Show error handling for one entity doesn't affect others
    printf("\n=== Error Isolation ===\n");
    printf("Causing error in Entity 1...\n");
    mobius_exec_string(entity1, "undefined_function(); // This will error");
    
    printf("Entity 2 should still work fine...\n");
    mobius_exec_string(entity2, "print(\"Entity 2 still working! Health:\", health);");
    
    // Cleanup
    mobius_free_state(entity1);
    mobius_free_state(entity2);
    mobius_free_state(entity3);
    
    printf("\n=== Multi-Environment Demo Complete ===\n");
    printf("Each entity has its own isolated script environment!\n");
    
    return 0;
}
