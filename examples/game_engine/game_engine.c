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
 */

#define _POSIX_C_SOURCE 200809L  // For usleep
#define _DEFAULT_SOURCE  // Additional feature for usleep
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Include Mobius headers
#include "../../src/mobius/state/mobius_state.h"
#include "../../src/mobius/state/environment.h"
#include "../../src/mobius/data/value.h"
#include "../../src/mobius/library/library.h"
#include "../../src/mobius/eval/evaluator.h"

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
 * Get player position: get_player_pos() -> string "x,y"
 */
int game_get_player_pos(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 0) {
        return mobius_error(state, "get_player_pos takes no arguments");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    char pos_str[64];
    snprintf(pos_str, sizeof(pos_str), "%.1f,%.1f", g_game->player.x, g_game->player.y);
    
    ctx_push(ctx, make_string_value_from_cstr(state, pos_str));
    return 1;
}

/**
 * Set player position: set_player_pos(x, y)
 */
int game_set_player_pos(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 2) {
        return mobius_error(state, "set_player_pos requires 2 arguments");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    Value y_val = ctx_pop(ctx);
    Value x_val = ctx_pop(ctx);
    
    double x = (x_val.type == VAL_FLOAT64) ? x_val.as.float64_val : (double)x_val.as.integer.value.i64;
    double y = (y_val.type == VAL_FLOAT64) ? y_val.as.float64_val : (double)y_val.as.integer.value.i64;
    
    free_value(x_val);
    free_value(y_val);
    
    g_game->player.x = (float)x;
    g_game->player.y = (float)y;
    
    ctx_push(ctx, make_nil_value());
    return 1;
}

/**
 * Get player health: get_player_health() -> integer
 */
int game_get_player_health(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 0) {
        return mobius_error(state, "get_player_health takes no arguments");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    ctx_push(ctx, make_integer_value(NUM_INT32, g_game->player.health));
    return 1;
}

/**
 * Set player health: set_player_health(health)
 */
int game_set_player_health(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 1) {
        return mobius_error(state, "set_player_health requires 1 argument");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    Value health_val = ctx_pop(ctx);
    int health = (health_val.type == VAL_INTEGER) ? 
        (int)health_val.as.integer.value.i32 : (int)health_val.as.float64_val;
    free_value(health_val);
    
    if (health < 0) health = 0;
    if (health > 100) health = 100;
    
    g_game->player.health = health;
    
    ctx_push(ctx, make_nil_value());
    return 1;
}

/**
 * Get player score: get_score() -> integer
 */
int game_get_score(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 0) {
        return mobius_error(state, "get_score takes no arguments");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    ctx_push(ctx, make_integer_value(NUM_INT32, g_game->player.score));
    return 1;
}

/**
 * Add to score: add_score(points)
 */
int game_add_score(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 1) {
        return mobius_error(state, "add_score requires 1 argument");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    Value points_val = ctx_pop(ctx);
    int points = (points_val.type == VAL_INTEGER) ? 
        (int)points_val.as.integer.value.i32 : (int)points_val.as.float64_val;
    free_value(points_val);
    
    g_game->player.score += points;
    
    ctx_push(ctx, make_integer_value(NUM_INT32, g_game->player.score));
    return 1;
}

/**
 * Spawn enemy: spawn_enemy(x, y, type)
 */
int game_spawn_enemy(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 3) {
        return mobius_error(state, "spawn_enemy requires 3 arguments");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    if (g_game->enemy_count >= 10) {
        return mobius_error(state, "Too many enemies");
    }
    
    Value type_val = ctx_pop(ctx);
    Value y_val = ctx_pop(ctx);
    Value x_val = ctx_pop(ctx);
    
    double x = (x_val.type == VAL_FLOAT64) ? x_val.as.float64_val : (double)x_val.as.integer.value.i64;
    double y = (y_val.type == VAL_FLOAT64) ? y_val.as.float64_val : (double)y_val.as.integer.value.i64;
    int type = (type_val.type == VAL_INTEGER) ? (int)type_val.as.integer.value.i32 : (int)type_val.as.float64_val;
    
    free_value(x_val);
    free_value(y_val);
    free_value(type_val);
    
    Enemy* enemy = &g_game->enemies[g_game->enemy_count];
    enemy->x = (float)x;
    enemy->y = (float)y;
    enemy->type = type;
    enemy->active = 1;
    
    g_game->enemy_count++;
    
    ctx_push(ctx, make_integer_value(NUM_INT32, g_game->enemy_count));
    return 1;
}

/**
 * Get current level: get_level() -> integer
 */
int game_get_level(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 0) {
        return mobius_error(state, "get_level takes no arguments");
    }
    
    if (!g_game) {
        return mobius_error(state, "Game not initialized");
    }
    
    ctx_push(ctx, make_integer_value(NUM_INT32, g_game->level));
    return 1;
}

/**
 * Game log function: game_log(message)
 */
int game_log(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 1) {
        return mobius_error(state, "game_log requires 1 argument");
    }
    
    Value msg_val = ctx_pop(ctx);
    
    if (msg_val.type != VAL_STRING) {
        free_value(msg_val);
        return mobius_error(state, "game_log requires a string argument");
    }
    
    printf("[GAME LOG] %s\n", msg_val.as.string->data);
    free_value(msg_val);
    
    ctx_push(ctx, make_nil_value());
    return 1;
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
    game->script_state = mobius_new_state(NULL);
    if (!game->script_state) {
        printf("❌ Failed to create Mobius state\n");
        exit(1);
    }
    
    if (mobius_init_stdlib(game->script_state) != MOBIUS_OK) {
        printf("❌ Failed to initialize Mobius stdlib\n");
        exit(1);
    }
    
    // Register game API functions
    define_variable(game->script_state->global_env, "get_player_pos", make_native_function_value(game_get_player_pos));
    define_variable(game->script_state->global_env, "set_player_pos", make_native_function_value(game_set_player_pos));
    define_variable(game->script_state->global_env, "get_player_health", make_native_function_value(game_get_player_health));
    define_variable(game->script_state->global_env, "set_player_health", make_native_function_value(game_set_player_health));
    define_variable(game->script_state->global_env, "get_score", make_native_function_value(game_get_score));
    define_variable(game->script_state->global_env, "add_score", make_native_function_value(game_add_score));
    define_variable(game->script_state->global_env, "spawn_enemy", make_native_function_value(game_spawn_enemy));
    define_variable(game->script_state->global_env, "get_level", make_native_function_value(game_get_level));
    define_variable(game->script_state->global_env, "game_log", make_native_function_value(game_log));
    
    printf("🎮 Game engine initialized with %zu total functions available\n", 
           get_library_function_count() + 9); // stdlib + 9 game functions
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
        printf("Script error in event '%s'\n", event_name);
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
    int result = system("mkdir -p scripts");
    if (result != 0) {
        fprintf(stderr, "Warning: Failed to create scripts directory\n");
    }
    
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
