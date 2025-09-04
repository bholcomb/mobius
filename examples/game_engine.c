/*
 * Game Engine with Mobius Scripting
 * 
 * This example demonstrates a practical use case for embedding Mobius:
 * a simple 2D game engine where game logic is written in Mobius scripts.
 * 
 * Features demonstrated:
 * - Game state management through Mobius
 * - Custom C functions exposed to scripts
 * - Event handling via scripts
 * - Configuration loading
 * - Real-time script execution
 * 
 * Compile: gcc -o game_engine game_engine.c -I../src -L../build -lmobius -lm -ldl
 * Run: ./game_engine
 */

#define _POSIX_C_SOURCE 200809L  // For usleep
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../src/mobius/mobius.h"

// ============================================================================
// GAME STATE STRUCTURES
// ============================================================================

typedef struct {
    float x, y;
    float velocity_x, velocity_y;
    int health;
    int score;
} Player;

typedef struct {
    float x, y;
    int active;
    int type;
} Enemy;

typedef struct {
    Player player;
    Enemy enemies[10];
    int enemy_count;
    int game_running;
    int level;
    MobiusState* script_state;
} GameEngine;

// Global game instance (in a real engine, this would be passed as context)
static GameEngine* g_game = NULL;

// ============================================================================
// GAME API FUNCTIONS (CALLABLE FROM MOBIUS)
// ============================================================================

/**
 * Get player position: get_player_pos() -> [x, y]
 */
int game_get_player_pos(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    (void)args; // Suppress warning
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    // For this example, we'll return a string representation
    // In a real implementation, you might return an array or object
    char pos_str[64];
    snprintf(pos_str, sizeof(pos_str), "%.1f,%.1f", g_game->player.x, g_game->player.y);
    
    *result = mobius_create_string(state, pos_str);
    return MOBIUS_OK;
}

/**
 * Set player position: set_player_pos(x, y)
 */
int game_set_player_pos(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(2);
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    // Accept both integers and floats
    double x = mobius_convert_to_float(args[0]);
    double y = mobius_convert_to_float(args[1]);
    
    g_game->player.x = (float)x;
    g_game->player.y = (float)y;
    
    *result = mobius_create_nil(state);
    return MOBIUS_OK;
}

/**
 * Get player health: get_player_health() -> integer
 */
int game_get_player_health(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    (void)args;
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    *result = mobius_create_integer(state, g_game->player.health);
    return MOBIUS_OK;
}

/**
 * Set player health: set_player_health(health)
 */
int game_set_player_health(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_integer, "integer");
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    int health = (int)mobius_to_integer(args[0]);
    if (health < 0) health = 0;
    if (health > 100) health = 100;
    
    g_game->player.health = health;
    
    *result = mobius_create_nil(state);
    return MOBIUS_OK;
}

/**
 * Get player score: get_score() -> integer
 */
int game_get_score(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    (void)args;
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    *result = mobius_create_integer(state, g_game->player.score);
    return MOBIUS_OK;
}

/**
 * Add to score: add_score(points)
 */
int game_add_score(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_integer, "integer");
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    int points = (int)mobius_to_integer(args[0]);
    g_game->player.score += points;
    
    *result = mobius_create_integer(state, g_game->player.score);
    return MOBIUS_OK;
}

/**
 * Spawn enemy: spawn_enemy(x, y, type)
 */
int game_spawn_enemy(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(3);
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    if (g_game->enemy_count >= 10) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Too many enemies");
    }
    
    double x = mobius_convert_to_float(args[0]);
    double y = mobius_convert_to_float(args[1]);
    int type = (int)mobius_convert_to_integer(args[2]);
    
    Enemy* enemy = &g_game->enemies[g_game->enemy_count];
    enemy->x = (float)x;
    enemy->y = (float)y;
    enemy->type = type;
    enemy->active = 1;
    
    g_game->enemy_count++;
    
    *result = mobius_create_integer(state, g_game->enemy_count);
    return MOBIUS_OK;
}

/**
 * Get current level: get_level() -> integer
 */
int game_get_level(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    (void)args;
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    *result = mobius_create_integer(state, g_game->level);
    return MOBIUS_OK;
}

/**
 * Game log function: game_log(message)
 */
int game_log(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_string, "string");
    
    const char* message = mobius_to_string(args[0]);
    printf("[GAME LOG] %s\n", message);
    
    *result = mobius_create_nil(state);
    return MOBIUS_OK;
}

// ============================================================================
// GAME ENGINE FUNCTIONS
// ============================================================================

void init_game_engine(GameEngine* game) {
    // Initialize game state
    game->player.x = 50.0f;
    game->player.y = 50.0f;
    game->player.velocity_x = 0.0f;
    game->player.velocity_y = 0.0f;
    game->player.health = 100;
    game->player.score = 0;
    
    game->enemy_count = 0;
    game->game_running = 1;
    game->level = 1;
    
    // Initialize scripting
    game->script_state = mobius_new_state();
    if (!game->script_state) {
        printf("❌ Failed to create Mobius state\n");
        exit(1);
    }
    
    if (mobius_init_core(game->script_state) != MOBIUS_OK) {
        printf("❌ Failed to initialize Mobius core\n");
        exit(1);
    }
    
    // Register game API functions
    mobius_register_function(game->script_state, "get_player_pos", game_get_player_pos, 0, "Get player position");
    mobius_register_function(game->script_state, "set_player_pos", game_set_player_pos, 2, "Set player position");
    mobius_register_function(game->script_state, "get_player_health", game_get_player_health, 0, "Get player health");
    mobius_register_function(game->script_state, "set_player_health", game_set_player_health, 1, "Set player health");
    mobius_register_function(game->script_state, "get_score", game_get_score, 0, "Get player score");
    mobius_register_function(game->script_state, "add_score", game_add_score, 1, "Add to player score");
    mobius_register_function(game->script_state, "spawn_enemy", game_spawn_enemy, 3, "Spawn an enemy");
    mobius_register_function(game->script_state, "get_level", game_get_level, 0, "Get current level");
    mobius_register_function(game->script_state, "game_log", game_log, 1, "Log a game message");
    
    // Try to load math plugin for game calculations
    if (mobius_load_plugin(game->script_state, "../bin/modules/math.so") == MOBIUS_OK) {
        printf("✅ Math plugin loaded for advanced game calculations\n");
    }
    
    printf("🎮 Game engine initialized with %zu total functions available\n", 
           mobius_function_count(game->script_state));
}

void cleanup_game_engine(GameEngine* game) {
    if (game->script_state) {
        mobius_free_state(game->script_state);
    }
}

void load_game_scripts(GameEngine* game) {
    printf("📜 Loading game scripts...\n");
    
    // Load initialization script
    if (mobius_exec_file(game->script_state, "scripts/game_init.mob") == MOBIUS_OK) {
        printf("✅ Loaded game initialization script\n");
    } else {
        printf("⚠️  Game init script not found, using defaults\n");
    }
    
    // Load game configuration
    const char* config_script = 
        "// Game Configuration\n"
        "var game_title = \"Mobius Adventure\";\n"
        "var max_health = 100;\n"
        "var starting_level = 1;\n"
        "game_log(\"Game configured: \" + game_title);\n";
    
    mobius_exec_string(game->script_state, config_script);
}

void run_game_event(GameEngine* game, const char* event_name, const char* event_data) {
    char script[512];
    
    // Set event variables
    snprintf(script, sizeof(script), 
        "var event_name = \"%s\";\n"
        "var event_data = \"%s\";\n", 
        event_name, event_data ? event_data : "");
    
    mobius_exec_string(game->script_state, script);
    
    // Try to call event handler
    snprintf(script, sizeof(script), 
        "if (typeof(on_%s) == \"function\") {\n"
        "    on_%s(event_data);\n"
        "} else {\n"
        "    game_log(\"No handler for event: %s\");\n"
        "}", 
        event_name, event_name, event_name);
    
    int result = mobius_exec_string(game->script_state, script);
    if (result != MOBIUS_OK) {
        MobiusError* error = mobius_get_last_error(game->script_state);
        if (error) {
            printf("❌ Script error in event '%s': %s\n", event_name, error->message);
            mobius_free_error(error);
        }
    }
}

void update_game_logic(GameEngine* game) {
    // Run per-frame game logic script
    const char* update_script = 
        "// Per-frame update logic\n"
        "var current_health = get_player_health();\n"
        "if (current_health <= 20) {\n"
        "    game_log(\"Warning: Low health!\");\n"
        "}\n"
        "\n"
        "// Level progression\n"
        "var score = get_score();\n"
        "var expected_level = int(score / 1000) + 1;\n"
        "var current_level = get_level();\n"
        "if (expected_level > current_level) {\n"
        "    game_log(\"Level up! Now at level \" + str(expected_level));\n"
        "}\n";
    
    mobius_exec_string(game->script_state, update_script);
}

void print_game_status(GameEngine* game) {
    printf("\n🎮 GAME STATUS:\n");
    printf("   Player: (%.1f, %.1f) Health: %d Score: %d Level: %d\n", 
           game->player.x, game->player.y, game->player.health, 
           game->player.score, game->level);
    printf("   Enemies: %d active\n", game->enemy_count);
}

// ============================================================================
// DEMO GAME SCRIPTS
// ============================================================================

void create_demo_scripts(void) {
    // Create scripts directory if it doesn't exist
    system("mkdir -p scripts");
    
    // Create game initialization script
    FILE* init_script = fopen("scripts/game_init.mob", "w");
    if (init_script) {
        fprintf(init_script, 
            "// Game Initialization Script\n"
            "game_log(\"Initializing Mobius Adventure...\");\n"
            "\n"
            "// Set up initial game state\n"
            "set_player_pos(25, 25);\n"
            "set_player_health(100);\n"
            "\n"
            "// Define event handlers\n"
            "func on_player_move(direction) {\n"
            "    var pos = get_player_pos();\n"
            "    game_log(\"Player moved \" + direction + \" to \" + pos);\n"
            "    \n"
            "    // Award points for movement\n"
            "    add_score(10);\n"
            "}\n"
            "\n"
            "func on_enemy_spawn(enemy_type) {\n"
            "    var x = random() * 100;\n"
            "    var y = random() * 100;\n"
            "    spawn_enemy(x, y, int(enemy_type));\n"
            "    game_log(\"Spawned enemy type \" + enemy_type + \" at (\" + str(x) + \", \" + str(y) + \")\");\n"
            "}\n"
            "\n"
            "func on_damage(amount) {\n"
            "    var current_health = get_player_health();\n"
            "    var new_health = current_health - int(amount);\n"
            "    set_player_health(new_health);\n"
            "    \n"
            "    if (new_health <= 0) {\n"
            "        game_log(\"Game Over! Final score: \" + str(get_score()));\n"
            "    } else {\n"
            "        game_log(\"Player took \" + amount + \" damage. Health: \" + str(new_health));\n"
            "    }\n"
            "}\n"
            "\n"
            "func on_powerup(type) {\n"
            "    if (type == \"health\") {\n"
            "        var current_health = get_player_health();\n"
            "        set_player_health(min(100, current_health + 25));\n"
            "        game_log(\"Health restored!\");\n"
            "    } else if (type == \"score\") {\n"
            "        add_score(500);\n"
            "        game_log(\"Bonus points!\");\n"
            "    }\n"
            "}\n"
            "\n"
            "game_log(\"Game initialization complete!\");\n");
        fclose(init_script);
        printf("✅ Created demo game initialization script\n");
    }
}

// ============================================================================
// MAIN PROGRAM
// ============================================================================

int main(void) {
    printf("🎮 Mobius Game Engine Example\n");
    printf("==============================\n\n");
    
    // Create demo scripts
    create_demo_scripts();
    
    // Initialize game
    GameEngine game = {0};
    g_game = &game;
    
    init_game_engine(&game);
    load_game_scripts(&game);
    
    printf("\n🚀 Starting game simulation...\n");
    
    // Simulate some game events
    const char* events[][2] = {
        {"player_move", "north"},
        {"enemy_spawn", "1"},
        {"player_move", "east"},
        {"damage", "15"},
        {"powerup", "health"},
        {"enemy_spawn", "2"},
        {"player_move", "south"},
        {"damage", "10"},
        {"powerup", "score"},
        {"player_move", "west"}
    };
    
    size_t event_count = sizeof(events) / sizeof(events[0]);
    
    for (size_t i = 0; i < event_count; i++) {
        printf("\n📡 Event %zu: %s(%s)\n", i + 1, events[i][0], events[i][1]);
        run_game_event(&game, events[i][0], events[i][1]);
        
        // Update game logic
        update_game_logic(&game);
        
        // Print status
        print_game_status(&game);
        
        // Small delay for demonstration
        usleep(500000); // 0.5 seconds
    }
    
    printf("\n🎯 Game simulation complete!\n");
    printf("\n💡 This example demonstrates:\n");
    printf("   ✅ Embedding Mobius in a game engine\n");
    printf("   ✅ Exposing C functions to Mobius scripts\n");
    printf("   ✅ Event-driven scripting architecture\n");
    printf("   ✅ Dynamic script loading and execution\n");
    printf("   ✅ Real-time game state management\n");
    printf("   ✅ Error handling in embedded scenarios\n");
    
    // Cleanup
    cleanup_game_engine(&game);
    printf("\n🧹 Game engine cleaned up successfully!\n");
    
    return 0;
}
